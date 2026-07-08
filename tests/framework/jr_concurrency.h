// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot
#pragma once

#include <chrono>
#include <condition_variable>
#include <mutex>

// Reductive host concurrency primitives for jidō-renga's off-target tests. The
// goal is NOT to emulate the Haiku kernel: it is to run a driver's real
// caller<->worker ownership protocol under genuine std::thread preemption, using
// portable stand-ins for the handful of kernel sync objects a rail touches.
// C++17, no C++20 <semaphore> dependency, so it retrofits across drivers and CI.

namespace jr::test {


// A counting semaphore standing in for a Haiku sem (release_sem/acquire_sem).
// Release() is the "meow"/completion nudge; TryAcquireFor() is a timed wait;
// TryAcquire() drains one unit without blocking (mirrors the engine's stale-
// signal drain). Deliberately simple: a mutex, a condvar, and an int.
class HostSemaphore {
public:
	explicit HostSemaphore(int initial = 0) noexcept : fCount(initial) {}

	HostSemaphore(const HostSemaphore&) = delete;
	HostSemaphore& operator=(const HostSemaphore&) = delete;

	void
	Release(int n = 1)
	{
		{
			std::lock_guard<std::mutex> lock(fMutex);
			fCount += n;
		}
		if (n == 1)
			fCondition.notify_one();
		else
			fCondition.notify_all();
	}

	void
	Acquire()
	{
		std::unique_lock<std::mutex> lock(fMutex);
		fCondition.wait(lock, [this] { return fCount > 0; });
		fCount--;
	}

	// Non-blocking: take one unit if available. Returns false if none.
	bool
	TryAcquire()
	{
		std::lock_guard<std::mutex> lock(fMutex);
		if (fCount > 0) {
			fCount--;
			return true;
		}
		return false;
	}

	template<class Rep, class Period>
	bool
	TryAcquireFor(std::chrono::duration<Rep, Period> timeout)
	{
		std::unique_lock<std::mutex> lock(fMutex);
		if (!fCondition.wait_for(lock, timeout, [this] { return fCount > 0; }))
			return false;
		fCount--;
		return true;
	}

private:
	std::mutex				fMutex;
	std::condition_variable	fCondition;
	int						fCount;
};


// One-shot countdown latch (a C++17-portable slice of std::latch), handy for
// releasing a herd of worker threads at once to maximize contention.
class Latch {
public:
	explicit Latch(int count) noexcept : fCount(count) {}

	Latch(const Latch&) = delete;
	Latch& operator=(const Latch&) = delete;

	void
	CountDown()
	{
		std::lock_guard<std::mutex> lock(fMutex);
		if (fCount > 0 && --fCount == 0)
			fCondition.notify_all();
	}

	void
	Wait()
	{
		std::unique_lock<std::mutex> lock(fMutex);
		fCondition.wait(lock, [this] { return fCount == 0; });
	}

private:
	std::mutex				fMutex;
	std::condition_variable	fCondition;
	int						fCount;
};


} // namespace jr::test
