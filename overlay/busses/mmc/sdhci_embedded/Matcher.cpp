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
	| Quirk::EmmcHardwareReset
	| Quirk::NeedsIosfOcpFixup
	| Quirk::ClockPllSequence
	| Quirk::Fixed1MHzTimeoutClock;

constexpr Quirk kByteSdQuirks =
	Quirk::BrokenPresetValues
	| Quirk::PowerOnDelay
	| Quirk::StopTransmissionBusy
	| Quirk::CardOnNeedsBusOn
	| Quirk::NeedsIosfOcpFixup
	| Quirk::ClockPllSequence;

const MatchProfile kProfiles[] = {
	// Intel Bay Trail eMMC (SCC eMMC controller). Soldered: not removable, no
	// card-detect line -> no hot-plug watcher, powered on unconditionally. _UID
	// is an instance identifier; the HID alone identifies the eMMC role.
	{
		"80860F14",
		CardDialect::Mmc, false, DmaStrategy::Adma2, kByteEmmcQuirks,
		PersonalityKind::BayTrail,
		"Intel Bay Trail eMMC Host (sdhci_emb)",
	},
	// Intel Bay Trail SD (SCC SD controller). Removable slot -> the Controller
	// starts the lazy insert/remove watcher after boot.
	{
		"80860F16",
		CardDialect::Sd, true, DmaStrategy::Sdma, kByteSdQuirks,
		PersonalityKind::BayTrail,
		"Intel Bay Trail SD Host (sdhci_emb)",
	},
};

constexpr uint32_t kProfileCount = sizeof(kProfiles) / sizeof(kProfiles[0]);

} // unnamed namespace


const MatchProfile*
MatchProfileFor(const char* hid) noexcept
{
	if (hid == nullptr)
		return nullptr;

	for (uint32_t i = 0; i < kProfileCount; i++) {
		const MatchProfile& p = kProfiles[i];
		if (strcmp(p.hid, hid) != 0)
			continue;
		return &p;
	}
	return nullptr;
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
