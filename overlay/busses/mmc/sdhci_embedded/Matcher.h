// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot
#pragma once

#include <stdint.h>

#include "Command.h"
#include "Types.h"

// The Bay Trail profile table maps a controller HID to its card role and host
// policy. BytAcpi applies ACPI's generic SDHCI preconditions before this pure,
// host-testable table is consulted.

namespace jr::sdhci {


// Which host-quirk personality a profile selects. The Controller maps this to
// a concrete HostPersonality instance (kept out of the table so the matcher
// stays free of vtables and kernel deps).
enum class PersonalityKind : uint8_t {
	Generic,
	BayTrail,
};


struct MatchProfile {
	const char*		hid;			// ACPI _HID identifying this controller role
	CardDialect		dialect;
	bool			removable;		// physical slot: soldered eMMC is false, so
									// the Controller starts no hot-plug watcher
	DmaStrategy		dma;
	Quirk			quirks;
	PersonalityKind	personality;
	const char*		prettyName;		// B_DEVICE_PRETTY_NAME for node #1
};


// Look up the profile for a Bay Trail controller HID. Returns nullptr on no
// match. _UID identifies an ACPI instance, not this controller's card role.
const MatchProfile* MatchProfileFor(const char* hid) noexcept;


// Access to the static table, for tests and diagnostics.
const MatchProfile* ProfileTable() noexcept;
uint32_t ProfileCount() noexcept;


} // namespace jr::sdhci
