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


// WorkerRig service callback: publish an epoch-specific response and mark the
// ticket terminal.
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


// A late completion for an abandoned transaction must not satisfy the next
// caller's wait.
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
	JR_CHECK(nudged);					// completion belongs to the older ticket
	JR_CHECK(!mine->IsDone());

	// The submitted ticket becomes complete only after its own service pass.
	rig.ReleaseNext();
	rig.WaitCompletedAtLeast(2);
	JR_CHECK(mine->IsDone());
	JR_CHECK(ResponseMatches(mine, 2));

	rig.Stop();
}


// Exercise timeout, reclaim, and late completion under real thread preemption.
// Successful submissions must carry their own response and preserve ticket
// lifetime across Retain/Reclaim/Release.
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
