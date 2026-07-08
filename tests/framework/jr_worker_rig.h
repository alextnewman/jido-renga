// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot
#pragma once

#include <atomic>
#include <chrono>
#include <functional>
#include <thread>

#include "framework/jr_concurrency.h"

// A driver-agnostic host model of the Haiku "ISR bottom half": an interrupt (or
// a timer) nudges a plain worker thread that drains a mailbox of ref-counted
// tickets and, on completion, nudges the waiting caller. Jidō-renga's rails are
// all this shape -- a serialized caller front, a lock-free mailbox, one worker,
// a counting completion nudge -- so we exercise them off-target with real
// std::thread preemption instead of emulating the kernel.
//
// Plug your driver in via three type/behaviour holes:
//
//   * Ticket   -- your ref-counted work item. Must provide:
//                     void Retain();
//                     void Release();
//                     bool IsDone() const;      // terminal-state fence
//                 (your service marks it done however it likes.)
//   * Mailbox  -- your single-slot handoff. Must provide:
//                     bool     Post(Ticket*);   // caller publishes
//                     Ticket*  Claim();         // worker takes (nullptr if none)
//                     bool     Reclaim(Ticket*);// caller pulls back on timeout
//                     bool     Empty() const;
//   * Service  -- std::function<void(Ticket*)>: do the work and mark the ticket
//                 done. This is the ONE driver-specific line the worker runs.
//
// sdhci_embedded's Transaction / TransactionMailbox satisfy this as-is; a new
// driver either matches the surface or wraps its types in a thin adapter.

namespace jr::test {


// Signals are hints; state is truth. A Fixed caller believes only its own
// ticket's IsDone(); a Naive caller trusts the raw completion nudge (which is
// the shape of bug #1 -- kept so tests can demonstrate the lie).
enum class WaitProtocol { Naive, Fixed };


struct SubmitOutcome {
	bool	completed = false;		// the protocol reported "done"
	bool	ticketWasDone = false;	// ...and was the ticket actually terminal?
	bool	timedOut = false;
};


template<class Ticket, class Mailbox>
class WorkerRig {
public:
	using Service = std::function<void(Ticket*)>;

	explicit WorkerRig(Service service) noexcept : fService(std::move(service)) {}

	WorkerRig(const WorkerRig&) = delete;
	WorkerRig& operator=(const WorkerRig&) = delete;

	~WorkerRig() { Stop(); }

	void
	Start()
	{
		fRunning.store(true, std::memory_order_release);
		fWorker = std::thread(&WorkerRig::Loop, this);
	}

	// Unblock and join the worker. Safe to call more than once. The worker never
	// abandons a claimed ticket -- it always finishes servicing (and releases
	// its reference) before re-checking the run flag -- so a caller that joined
	// its threads first leaves no dangling references (leak-clean under LSan).
	void
	Stop()
	{
		if (!fWorker.joinable())
			return;
		fRunning.store(false, std::memory_order_release);
		fGate.Release(64);				// free a worker parked at the gate
		fWakeup.Release(64);			// free a worker waiting for work
		fWorker.join();
	}

	// ---- Black-box caller path (mirrors SdhciEngine::Execute) ---------------

	// Submit a ticket the caller already owns one reference to. The rig adds the
	// worker's reference and balances it (worker Release on completion, or caller
	// Release on a successful reclaim). The caller keeps its own reference and is
	// free to read the ticket's result once the outcome reports completed.
	template<class Rep, class Period>
	SubmitOutcome
	Submit(Ticket* ticket, WaitProtocol proto,
		std::chrono::duration<Rep, Period> budget)
	{
		SubmitOutcome out;

		while (fCompletion.TryAcquire())	// drain stale nudges
			;

		ticket->Retain();
		fMailbox.Post(ticket);
		fWakeup.Release();

		const auto deadline = std::chrono::steady_clock::now() + budget;
		for (;;) {
			const auto now = std::chrono::steady_clock::now();
			const auto remaining = deadline > now
				? std::chrono::duration_cast<std::chrono::microseconds>(
					deadline - now)
				: std::chrono::microseconds(0);
			if (fCompletion.TryAcquireFor(remaining)) {
				if (proto == WaitProtocol::Naive) {
					out.completed = true;
					out.ticketWasDone = ticket->IsDone();
					return out;
				}
				if (ticket->IsDone()) {
					out.completed = true;
					out.ticketWasDone = true;
					return out;
				}
				continue;				// stale nudge; wait for our own state
			}
			if (fMailbox.Reclaim(ticket))
				ticket->Release();		// worker never saw it; drop its ref
			out.timedOut = true;
			return out;
		}
	}

	// ---- White-box controls (for deterministic interleaving tests) ----------

	Mailbox& mailbox() noexcept { return fMailbox; }

	// When gated, the worker parks before servicing each claimed ticket until a
	// token is handed to it, so a test can force an exact interleaving.
	void SetGated(bool gated) noexcept
	{
		fGated.store(gated, std::memory_order_release);
	}
	void ReleaseNext(int count = 1) { fGate.Release(count); }

	void Nudge(int count = 1) { fWakeup.Release(count); }

	bool DrainCompletion() { return fCompletion.TryAcquire(); }

	template<class Rep, class Period>
	bool WaitCompletion(std::chrono::duration<Rep, Period> timeout)
	{
		return fCompletion.TryAcquireFor(timeout);
	}

	// Spin until the worker has claimed / completed at least `target` tickets.
	// The counters are release/acquire ordered, so a test may safely read fields
	// the worker published once the corresponding count is observed.
	void WaitClaimedAtLeast(int target) const { SpinUntil(fClaimed, target); }
	void WaitCompletedAtLeast(int target) const { SpinUntil(fCompleted, target); }

	int claimed() const { return fClaimed.load(std::memory_order_acquire); }
	int completed() const { return fCompleted.load(std::memory_order_acquire); }

private:
	static void
	SpinUntil(const std::atomic<int>& counter, int target)
	{
		while (counter.load(std::memory_order_acquire) < target)
			std::this_thread::yield();
	}

	void
	Loop()
	{
		while (fRunning.load(std::memory_order_acquire)) {
			fWakeup.TryAcquireFor(std::chrono::milliseconds(2));
			Ticket* ticket = fMailbox.Claim();
			if (ticket == nullptr)
				continue;
			fClaimed.fetch_add(1, std::memory_order_release);
			if (fGated.load(std::memory_order_acquire))
				fGate.Acquire();			// the test decides WHEN this lands
			fService(ticket);				// driver-specific: do work + mark done
			fCompleted.fetch_add(1, std::memory_order_release);
			fCompletion.Release();
			ticket->Release();				// balance the caller's pre-Post Retain
		}
	}

	Service				fService;
	Mailbox				fMailbox;
	HostSemaphore		fWakeup;			// caller/ISR -> worker: "work queued"
	HostSemaphore		fCompletion;		// worker -> caller: counting nudge
	HostSemaphore		fGate;				// test -> worker: gate one completion
	std::atomic<bool>	fRunning{false};
	std::atomic<bool>	fGated{false};
	std::atomic<int>	fClaimed{0};
	std::atomic<int>	fCompleted{0};
	std::thread			fWorker;
};


} // namespace jr::test
