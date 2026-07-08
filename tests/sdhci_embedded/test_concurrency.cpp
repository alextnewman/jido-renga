// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot

#include "framework/jr_test.h"
#include "framework/jr_worker_rig.h"

#include "Transaction.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>

using namespace jr::sdhci;
using jr::test::WorkerRig;
using jr::test::WaitProtocol;
using jr::test::SubmitOutcome;

namespace {


// The one driver-specific line: the worker publishes a response into the ticket
// (never into caller storage) and marks it done. The value is a pure function of
// the epoch so any caller can confirm it received *its own* result. This is the
// hole the generic WorkerRig calls; everything else -- the mailbox handoff, the
// completion protocol, the timeout/reclaim dance, the gate -- is the framework's.
void
PublishResponse(Transaction* ticket)
{
	for (uint32_t i = 0; i < 4; i++)
		ticket->responseWords[i] = ticket->epoch * 4u + i;
	ticket->MarkDone(0);
}


using SdhciRig = WorkerRig<Transaction, TransactionMailbox>;


bool
ResponseMatches(const Transaction* ticket, uint32_t epoch) noexcept
{
	for (uint32_t i = 0; i < 4; i++) {
		if (ticket->responseWords[i] != epoch * 4u + i)
			return false;
	}
	return true;
}


} // namespace


// Deterministic teeth for bug #1, now driven through the generic rig. A worker
// completes an *earlier, abandoned* command after a new caller has drained and
// posted; the resulting completion nudge is real, but not for the new caller's
// ticket. A naive rail returns success on a not-done ticket; the fixed rail keeps
// waiting for its own IsDone(). This is the exact interleaving the review caught
// and the pure per-function tests cannot reach.
JR_TEST(concurrency, stale_completion_nudge_is_not_our_success)
{
	SdhciRig rig(PublishResponse);
	rig.SetGated(true);
	rig.Start();

	// An earlier command O that its caller will abandon while the worker holds it
	// (so it completes late). Manual refcounting mirrors Execute's timeout path:
	// New()=1, Retain()=2; on Reclaim failure the caller drops its own ref.
	Transaction* older = Transaction::New(Cmd::GoIdleState);
	older->epoch = 1;
	older->Retain();
	rig.mailbox().Post(older);
	rig.Nudge();
	rig.WaitClaimedAtLeast(1);					// worker now holds O at the gate

	JR_CHECK(!rig.mailbox().Reclaim(older));		// too late: worker owns it
	older->Release();							// caller abandons its reference

	// The new caller drains (O has NOT completed yet), then posts its ticket B.
	Transaction* mine = Transaction::New(Cmd::SendCsd);
	mine->epoch = 2;
	TicketHolder holder(mine);
	while (rig.DrainCompletion())
		;
	mine->Retain();
	rig.mailbox().Post(mine);
	rig.Nudge();

	// Let O complete. Its nudge lands in the completion sem while B is still
	// queued behind it and provably not done.
	rig.ReleaseNext();
	rig.WaitCompletedAtLeast(1);

	const bool nudged = rig.WaitCompletion(std::chrono::seconds(1));
	JR_CHECK(nudged);					// a real completion nudge is present...
	JR_CHECK(!mine->IsDone());			// ...but it is NOT ours -> naive lies here

	// The fixed rail treats that nudge as a hint and waits. Release the gate for
	// B and confirm it now completes correctly, with its OWN response.
	rig.ReleaseNext();
	rig.WaitCompletedAtLeast(2);
	JR_CHECK(mine->IsDone());
	JR_CHECK(ResponseMatches(mine, 2));

	rig.Stop();
}


// Stress the real ownership/completion protocol under genuine preemption. Many
// caller threads (serialized by a bus lock, exactly as the engine serializes
// Execute) drive thousands of commands against one worker, a quarter with a zero
// budget to hammer the timeout + Reclaim + late-completion path (bug #2
// territory). Invariants: the fixed rail never reports a false success, every
// success carries the caller's own response, and -- under ASan -- no ticket is
// used-after-free or double-freed across the Retain/Reclaim/Release handoff.
JR_TEST(concurrency, stress_caller_worker_rail_under_threads)
{
	SdhciRig rig(PublishResponse);
	rig.Start();

	constexpr int kThreads = 4;
	constexpr int kPerThread = 1500;

	std::mutex busLock;						// mirrors SdhciEngine::fBusLock
	std::atomic<uint32_t> epochSource{100};
	std::atomic<int> falseSuccess{0};
	std::atomic<int> responseMismatch{0};
	std::atomic<int> successes{0};
	std::atomic<int> timeouts{0};

	auto callerBody = [&](int threadIndex) {
		for (int i = 0; i < kPerThread; i++) {
			const bool impatient = ((threadIndex + i) & 3) == 0;
			const auto budget = impatient
				? std::chrono::microseconds(0)
				: std::chrono::microseconds(50 * 1000);
			const uint32_t epoch =
				epochSource.fetch_add(1, std::memory_order_relaxed);

			Transaction* txn = Transaction::New(Cmd::SendCsd);
			txn->epoch = epoch;
			TicketHolder holder(txn);

			std::lock_guard<std::mutex> lock(busLock);
			const SubmitOutcome out =
				rig.Submit(txn, WaitProtocol::Fixed, budget);

			if (out.completed) {
				successes.fetch_add(1, std::memory_order_relaxed);
				if (!out.ticketWasDone)
					falseSuccess.fetch_add(1, std::memory_order_relaxed);
				if (!ResponseMatches(txn, epoch))
					responseMismatch.fetch_add(1, std::memory_order_relaxed);
			} else if (out.timedOut) {
				timeouts.fetch_add(1, std::memory_order_relaxed);
			}
		}
	};

	std::vector<std::thread> callers;
	for (int t = 0; t < kThreads; t++)
		callers.emplace_back(callerBody, t);
	for (auto& c : callers)
		c.join();

	rig.Stop();

	JR_CHECK_EQ(falseSuccess.load(), 0);
	JR_CHECK_EQ(responseMismatch.load(), 0);
	JR_CHECK_EQ(successes.load() + timeouts.load(), kThreads * kPerThread);
	JR_CHECK(successes.load() > 0);			// the rail actually did work
}
