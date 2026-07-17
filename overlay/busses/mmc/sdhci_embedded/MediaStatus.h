// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GPT-5.6 Sol
#pragma once

#include <SupportDefs.h>

namespace jr::sdhci {


// Tracks both the I/O epoch and Haiku's one-shot removable-media notification.
// Consuming the notification does not change the epoch captured by queued I/O.
class MediaStatusTracker {
public:
	explicit MediaStatusTracker(bool online = true)
		:
		fState(online ? 1 : 0)
	{
	}

	bool IsOnline() const { return (atomic_get(&fState) & 1) != 0; }
	int32 State() const { return atomic_get(&fState); }

	void SetOnline(bool online)
	{
		const int32 onlineBit = online ? 1 : 0;
		while (true) {
			const int32 oldState = atomic_get(&fState);
			if ((oldState & 1) == onlineBit)
				return;

			const uint32 nextGeneration
				= (static_cast<uint32>(oldState) & ~1u) + 2u;
			const int32 newState = static_cast<int32>(nextGeneration)
				| onlineBit;
			if (atomic_test_and_set(&fState, newState, oldState) != oldState)
				continue;

			if (online)
				atomic_set(&fChangePending, 1);
			return;
		}
	}

	status_t ConsumeStatus()
	{
		const bool changed = atomic_get_and_set(&fChangePending, 0) != 0;
		if (!IsOnline())
			return B_DEV_NO_MEDIA;
		return changed ? B_DEV_MEDIA_CHANGED : B_OK;
	}

	void RestoreChange()
	{
		if (IsOnline())
			atomic_set(&fChangePending, 1);
	}

private:
	mutable int32	fState;
	mutable int32	fChangePending = 0;
};


} // namespace jr::sdhci
