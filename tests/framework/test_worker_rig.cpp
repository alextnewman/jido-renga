// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot

#include "framework/jr_test.h"
#include "framework/jr_worker_rig.h"

#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
#include <vector>

using jr::test::WorkerRig;
using jr::test::WaitProtocol;
using jr::test::SubmitOutcome;

// A toy ticket + mailbox that share NOTHING with sdhci's Transaction beyond the
// required surface. This proves WorkerRig is generic (not accidentally coupled
// to one driver) and doubles as the minimal template a new jidō-renga driver
// copies: satisfy Retain/Release/IsDone on the ticket and Post/Claim/Reclaim/
// Empty on the mailbox, hand the rig a Service, and the thread-daisy-chain
// harness works.

namespace {


struct ToyTicket {
	std::atomic<int>	refCount{1};
	std::atomic<bool>	done{false};
	int					seed = 0;
	int					result = 0;

	static ToyTicket* New(int seed)
	{
		ToyTicket* t = new ToyTicket();
		t->seed = seed;
		return t;
	}

	void Retain() { refCount.fetch_add(1, std::memory_order_relaxed); }

	void
	Release()
	{
		if (refCount.fetch_sub(1, std::memory_order_acq_rel) == 1)
			delete this;
	}

	bool IsDone() const { return done.load(std::memory_order_acquire); }

	void
	MarkDone(int value)
	{
		result = value;
		done.store(true, std::memory_order_release);
	}
};


struct ToyMailbox {
	std::atomic<ToyTicket*>	slot{nullptr};

	bool
	Post(ToyTicket* ticket)
	{
		ToyTicket* expected = nullptr;
		return slot.compare_exchange_strong(expected, ticket);
	}

	ToyTicket* Claim() { return slot.exchange(nullptr); }

	bool
	Reclaim(ToyTicket* ticket)
	{
		ToyTicket* expected = ticket;
		return slot.compare_exchange_strong(expected, nullptr);
	}

	bool Empty() const { return slot.load() == nullptr; }
};


using ToyRig = WorkerRig<ToyTicket, ToyMailbox>;


void
ServiceToy(ToyTicket* ticket)
{
	ticket->MarkDone(ticket->seed * 7 + 1);
}


} // namespace


// The generic rig drives a foreign ticket type end-to-end under real threads.
JR_TEST(worker_rig, drives_a_foreign_ticket_type)
{
	ToyRig rig(ServiceToy);
	rig.Start();

	constexpr int kThreads = 3;
	constexpr int kPerThread = 400;

	std::mutex busLock;
	std::atomic<int> seedSource{0};
	std::atomic<int> completed{0};
	std::atomic<int> mismatched{0};

	auto body = [&]() {
		for (int i = 0; i < kPerThread; i++) {
			const int seed = seedSource.fetch_add(1, std::memory_order_relaxed);
			ToyTicket* t = ToyTicket::New(seed);

			std::lock_guard<std::mutex> lock(busLock);
			const SubmitOutcome out = rig.Submit(
				t, WaitProtocol::Fixed, std::chrono::milliseconds(200));

			if (out.completed) {
				completed.fetch_add(1, std::memory_order_relaxed);
				if (t->result != seed * 7 + 1)
					mismatched.fetch_add(1, std::memory_order_relaxed);
			}
			t->Release();					// caller's own reference
		}
	};

	std::vector<std::thread> threads;
	for (int t = 0; t < kThreads; t++)
		threads.emplace_back(body);
	for (auto& th : threads)
		th.join();

	rig.Stop();

	JR_CHECK_EQ(mismatched.load(), 0);
	JR_CHECK_EQ(completed.load(), kThreads * kPerThread);	// generous budget
}


// The same white-box gate controls work for any ticket type: force the worker to
// park mid-flight, observe not-done, release, observe done.
JR_TEST(worker_rig, gate_forces_a_deterministic_interleaving)
{
	ToyRig rig(ServiceToy);
	rig.SetGated(true);
	rig.Start();

	ToyTicket* t = ToyTicket::New(6);
	t->Retain();							// worker's reference
	rig.mailbox().Post(t);
	rig.Nudge();
	rig.WaitClaimedAtLeast(1);

	JR_CHECK(!t->IsDone());					// worker is parked at the gate

	rig.ReleaseNext();
	rig.WaitCompletedAtLeast(1);
	JR_CHECK(t->IsDone());
	JR_CHECK_EQ(t->result, 6 * 7 + 1);

	rig.Stop();
	t->Release();							// caller's reference
}


// Cover the Naive protocol on the happy path: with no stale nudge in flight, the
// completion nudge really is ours, so even the naive rail reports a correct,
// done result. Naive only lies in the specific stale-nudge race that
// sdhci's concurrency.stale_completion_nudge_is_not_our_success forces.
JR_TEST(worker_rig, naive_protocol_is_correct_without_a_stale_nudge)
{
	ToyRig rig(ServiceToy);
	rig.Start();

	ToyTicket* t = ToyTicket::New(9);
	const SubmitOutcome out = rig.Submit(
		t, WaitProtocol::Naive, std::chrono::milliseconds(200));

	JR_CHECK(out.completed);
	JR_CHECK(out.ticketWasDone);			// no race here -> the nudge is ours
	JR_CHECK_EQ(t->result, 9 * 7 + 1);

	rig.Stop();
	t->Release();
}
