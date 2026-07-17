// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GPT-5.6 Sol
#pragma once

#include <SupportDefs.h>

namespace jr::sdhci {


enum class HotplugAction : uint8 {
	None,
	Remove,
	Identify,
};


// The watcher owns fPresent. Eject may run concurrently through the disk ioctl,
// so its latch is atomic and remains set until physical removal.
class HotplugState {
public:
	explicit HotplugState(bool present = false)
		:
		fPresent(present)
	{
	}

	void SetPresent(bool present) { fPresent = present; }
	void MarkEjected() { atomic_set(&fEjected, 1); }
	void CancelEject() { atomic_set(&fEjected, 0); }
	bool IsEjected() const { return atomic_get(&fEjected) != 0; }

	HotplugAction Observe(bool present, bool online)
	{
		if (!present) {
			const bool removed = fPresent || online || IsEjected();
			fPresent = false;
			CancelEject();
			return removed ? HotplugAction::Remove : HotplugAction::None;
		}

		const bool inserted = !fPresent;
		fPresent = true;
		if (IsEjected())
			return HotplugAction::None;
		return inserted || !online
			? HotplugAction::Identify : HotplugAction::None;
	}

private:
	bool			fPresent;
	mutable int32	fEjected = 0;
};


} // namespace jr::sdhci
