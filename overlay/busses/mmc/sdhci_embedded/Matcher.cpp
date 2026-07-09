// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot

#include "Matcher.h"

#include <string.h>

namespace jr::sdhci {


// The entire boot-time truth table, in one place. Adding a new supported
// controller is a single row -- no probing code changes.
namespace {

constexpr Quirk kByteEmmcQuirks =
	Quirk::BrokenPresetValues
	| Quirk::PowerOnDelay
	| Quirk::StopTransmissionBusy
	| Quirk::TimeoutClockFromSdClock
	| Quirk::CardOnNeedsBusOn
	| Quirk::EmmcHardwareReset
	| Quirk::NeedsIosfOcpFixup;

constexpr Quirk kByteSdQuirks =
	Quirk::BrokenPresetValues
	| Quirk::PowerOnDelay
	| Quirk::TimeoutClockFromSdClock
	| Quirk::CardOnNeedsBusOn
	| Quirk::NeedsIosfOcpFixup;

const MatchProfile kProfiles[] = {
	// Intel Bay Trail eMMC (SCC eMMC controller), UID 1. Soldered: not removable,
	// no card-detect line -> no hot-plug watcher, powered on unconditionally.
	{
		"80860F14", 1,
		CardDialect::Mmc, false, DmaStrategy::Adma2, kByteEmmcQuirks,
		PersonalityKind::BayTrail,
		"Intel Bay Trail eMMC Host (sdhci_emb)",
	},
	// Intel Bay Trail SD (SCC SD controller), any UID. Removable slot -> the
	// Controller starts the lazy insert/remove watcher after boot.
	{
		"80860F16", kAnyUid,
		CardDialect::Sd, true, DmaStrategy::Adma2, kByteSdQuirks,
		PersonalityKind::BayTrail,
		"Intel Bay Trail SD Host (sdhci_emb)",
	},
};

constexpr uint32_t kProfileCount = sizeof(kProfiles) / sizeof(kProfiles[0]);

} // unnamed namespace


const MatchProfile*
MatchProfileFor(const char* hid, uint32_t uid) noexcept
{
	if (hid == nullptr)
		return nullptr;

	for (uint32_t i = 0; i < kProfileCount; i++) {
		const MatchProfile& p = kProfiles[i];
		if (strcmp(p.hid, hid) != 0)
			continue;
		if (p.uid != kAnyUid && p.uid != uid)
			continue;
		return &p;
	}
	return nullptr;
}


float
ScoreFor(const char* hid, uint32_t uid) noexcept
{
	return MatchProfileFor(hid, uid) != nullptr ? kMatchScore : kNoMatchScore;
}


const MatchProfile*
ProfileTable() noexcept
{
	return kProfiles;
}


uint32_t
ProfileCount() noexcept
{
	return kProfileCount;
}


} // namespace jr::sdhci
