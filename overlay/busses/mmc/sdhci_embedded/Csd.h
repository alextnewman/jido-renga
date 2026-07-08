// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot
#pragma once

#include <stdint.h>

#include "Types.h"

// Pure decoding of card geometry from the CSD register (SD/MMC) and EXT_CSD
// (eMMC). Split into two concerns so each is independently testable:
//
//   ReassembleR2()  -- undo the SDHCI response-register >>8 shift, producing a
//                      CSD in natural spec bit order.
//   Csd128::Bits()  -- extract a spec-numbered bit field.
//   DecodeCsd()     -- apply the spec capacity formulas (no hardware knowledge).
//   DecodeEmmcSectorCount() -- eMMC EXT_CSD SEC_COUNT.
//
// No kernel dependencies.

namespace jr::sdhci {


// Resulting geometry the Disk layer needs. The block device always presents
// 512-byte logical blocks regardless of the card's native read block length.
struct CardGeometry {
	uint64_t	capacityBytes = 0;
	uint32_t	blockSize = 512;
	uint64_t	blockCount = 0;		// capacityBytes / 512
	uint8_t		csdStructure = 0;	// 0 = v1 (standard), 1 = v2 (high capacity)
	bool		valid = false;
};


// A 128-bit CSD in natural spec bit order: words[0] holds bits [31:0],
// words[3] holds bits [127:96]. Matches every datasheet CSD table directly.
struct Csd128 {
	uint32_t	words[4] = {0, 0, 0, 0};

	// Extract the inclusive bit field [hi:lo] (width up to 32, may cross one
	// 32-bit word boundary).
	uint32_t
	Bits(unsigned hi, unsigned lo) const noexcept
	{
		const unsigned width = hi - lo + 1;
		const unsigned wordLo = lo / 32;
		const unsigned bitLo = lo % 32;
		const uint64_t low = words[wordLo];
		const uint64_t high = (wordLo + 1 < 4) ? words[wordLo + 1] : 0;
		const uint64_t window = low | (high << 32);
		const uint64_t v = window >> bitLo;
		const uint32_t mask
			= (width >= 32) ? 0xffffffffu : ((1u << width) - 1u);
		return static_cast<uint32_t>(v & mask);
	}
};


// The SDHCI controller stores R2 (136-bit) minus its CRC+stop bit in the four
// 32-bit RESPONSE registers, so response[0] holds true CSD bits [39:8]. Shift
// everything left by 8 to recover spec bit order (bits [7:0] are unknowable and
// unused by the fields we decode).
inline Csd128
ReassembleR2(const uint32_t response[4]) noexcept
{
	Csd128 csd;
	csd.words[0] = (response[0] << 8);
	csd.words[1] = (response[1] << 8) | (response[0] >> 24);
	csd.words[2] = (response[2] << 8) | (response[1] >> 24);
	csd.words[3] = (response[3] << 8) | (response[2] >> 24);
	return csd;
}


// Decode capacity from a spec-ordered CSD. Handles CSD v1.0 (C_SIZE /
// C_SIZE_MULT / READ_BL_LEN) and v2.0 (22-bit C_SIZE in 512 KiB units).
inline CardGeometry
DecodeCsd(const Csd128& csd) noexcept
{
	CardGeometry g;
	g.csdStructure = static_cast<uint8_t>(csd.Bits(127, 126));

	if (g.csdStructure == 1) {
		// v2.0: capacity = (C_SIZE + 1) * 512 KiB.
		const uint64_t cSize = csd.Bits(69, 48);
		g.capacityBytes = (cSize + 1) * (512ull * 1024ull);
	} else {
		// v1.0: capacity = (C_SIZE + 1) * 2^(C_SIZE_MULT + 2) * 2^READ_BL_LEN.
		const uint64_t readBlLen = csd.Bits(83, 80);
		const uint64_t cSize = csd.Bits(73, 62);
		const uint64_t cSizeMult = csd.Bits(49, 47);
		const uint64_t blockNr = (cSize + 1) << (cSizeMult + 2);
		g.capacityBytes = blockNr << readBlLen;
	}

	g.blockSize = 512;
	g.blockCount = g.capacityBytes / 512;
	g.valid = g.capacityBytes > 0;
	return g;
}


// Convenience: reassemble a raw R2 response and decode it in one step.
inline CardGeometry
DecodeSdCsd(const uint32_t response[4]) noexcept
{
	return DecodeCsd(ReassembleR2(response));
}


// eMMC cards >2 GiB report capacity via EXT_CSD SEC_COUNT (little-endian
// 32-bit at byte offset 212), counted in 512-byte sectors.
inline CardGeometry
DecodeEmmcSectorCount(const uint8_t extCsd[512]) noexcept
{
	const uint32_t secCount = static_cast<uint32_t>(extCsd[212])
		| (static_cast<uint32_t>(extCsd[213]) << 8)
		| (static_cast<uint32_t>(extCsd[214]) << 16)
		| (static_cast<uint32_t>(extCsd[215]) << 24);

	CardGeometry g;
	g.csdStructure = 1;
	g.blockSize = 512;
	g.blockCount = secCount;
	g.capacityBytes = static_cast<uint64_t>(secCount) * 512ull;
	g.valid = secCount > 0;
	return g;
}


} // namespace jr::sdhci
