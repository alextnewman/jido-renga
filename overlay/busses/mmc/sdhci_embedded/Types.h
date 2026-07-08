// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot
#pragma once

#include <stdint.h>

// Foundational value types for the sdhci_embedded stack.
//
// Everything in this header is pure: no kernel headers, no side effects. The
// pure core (Types, Command, Adma2, Convergence, Transaction, Csd, Matcher,
// Personality) is host-buildable so it can be unit tested off-target. The
// kernel shell (Engine, Controller, Card, Disk, glue) translates these values
// to/from Haiku's C ABI at its boundary.

namespace jr::sdhci {


// Which protocol dialect a card speaks. Chosen up front by the ExplicitMatcher
// (from ACPI UID), never guessed at runtime.
enum class CardDialect : uint8_t {
	Unknown = 0,
	Sd,			// SD / SDHC (ACMD41, SCR, ACMD6 bus width)
	Mmc,		// eMMC (CMD1, EXT_CSD, MMC_SWITCH)
};


// How bytes move between host and card. This picks the Disk subclass and the
// DMA restrictions we advertise to the IOScheduler -- a matched pair.
enum class DmaStrategy : uint8_t {
	None = 0,
	Pio,		// buffer-port programmed IO (bring-up only, never selected)
	Sdma,		// single-segment DMA
	Adma2,		// scatter/gather DMA (the Bay Trail target)
};


// Concrete card classification, resolved during identification.
enum class CardType : uint8_t {
	Unknown = 0,
	MmcStandard,
	MmcHighCapacity,	// >2 GiB eMMC, sector addressing
	Sd,
	Sdhc,				// high capacity SD, sector addressing
};


// True when the card addresses storage in 512-byte sectors rather than bytes.
constexpr bool
UsesSectorAddressing(CardType type) noexcept
{
	return type == CardType::MmcHighCapacity || type == CardType::Sdhc;
}


// A short human label for a dialect, for pretty names and traces.
constexpr const char*
DialectLabel(CardDialect dialect) noexcept
{
	switch (dialect) {
		case CardDialect::Sd:	return "SD";
		case CardDialect::Mmc:	return "eMMC";
		default:				return "?";
	}
}


} // namespace jr::sdhci
