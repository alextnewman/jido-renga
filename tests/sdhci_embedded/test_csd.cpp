// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot

#include "framework/jr_test.h"

#include "Csd.h"

using namespace jr::sdhci;


namespace {
// Independent bit-field writer (OR/shift based), deliberately not sharing code
// with Csd128::Bits (window/extract based) so a transposed index shows up.
void
SetField(Csd128& csd, unsigned hi, unsigned lo, uint64_t value)
{
	const unsigned width = hi - lo + 1;
	const unsigned wordLo = lo / 32;
	const unsigned bitLo = lo % 32;
	uint64_t window = static_cast<uint64_t>(csd.words[wordLo])
		| (static_cast<uint64_t>(wordLo + 1 < 4 ? csd.words[wordLo + 1] : 0) << 32);
	const uint64_t mask = (width >= 64) ? ~0ull : ((1ull << width) - 1ull);
	window &= ~(mask << bitLo);
	window |= (value & mask) << bitLo;
	csd.words[wordLo] = static_cast<uint32_t>(window & 0xffffffffu);
	if (wordLo + 1 < 4)
		csd.words[wordLo + 1] = static_cast<uint32_t>(window >> 32);
}
}


JR_TEST(csd, bits_extraction_within_and_across_words)
{
	Csd128 csd;
	csd.words[0] = 0xDEADBEEFu;
	JR_CHECK_EQ(csd.Bits(7, 0), 0xEFu);
	JR_CHECK_EQ(csd.Bits(15, 8), 0xBEu);
	JR_CHECK_EQ(csd.Bits(31, 24), 0xDEu);
	JR_CHECK_EQ(csd.Bits(31, 0), 0xDEADBEEFu);

	// Field spanning the word0/word1 boundary: bit31=1, bit32=1 -> 0b11.
	Csd128 span;
	span.words[0] = 0x80000000u;	// bit 31
	span.words[1] = 0x00000001u;	// bit 32
	JR_CHECK_EQ(span.Bits(32, 31), 0x3u);
}


JR_TEST(csd, decode_v2_high_capacity)
{
	Csd128 csd;
	SetField(csd, 127, 126, 1);			// CSD structure v2
	SetField(csd, 69, 48, 15200);		// C_SIZE

	JR_CHECK_EQ(csd.Bits(127, 126), 1u);
	JR_CHECK_EQ(csd.Bits(69, 48), 15200u);

	const CardGeometry g = DecodeCsd(csd);
	JR_CHECK(g.valid);
	JR_CHECK_EQ(g.csdStructure, 1);
	// (C_SIZE + 1) * 512 KiB.
	const uint64_t expected = static_cast<uint64_t>(15200 + 1) * 512ull * 1024ull;
	JR_CHECK_EQ(g.capacityBytes, expected);
	JR_CHECK_EQ(g.blockSize, 512u);
	JR_CHECK_EQ(g.blockCount, expected / 512ull);
}


JR_TEST(csd, decode_v1_standard_capacity)
{
	Csd128 csd;
	SetField(csd, 127, 126, 0);			// CSD structure v1
	SetField(csd, 83, 80, 9);			// READ_BL_LEN = 9 (512 bytes)
	SetField(csd, 73, 62, 3751);		// C_SIZE
	SetField(csd, 49, 47, 7);			// C_SIZE_MULT

	const CardGeometry g = DecodeCsd(csd);
	JR_CHECK(g.valid);
	JR_CHECK_EQ(g.csdStructure, 0);
	// blockNr = (C_SIZE + 1) << (C_SIZE_MULT + 2); capacity = blockNr << READ_BL_LEN.
	const uint64_t blockNr = static_cast<uint64_t>(3751 + 1) << (7 + 2);
	const uint64_t expected = blockNr << 9;
	JR_CHECK_EQ(g.capacityBytes, expected);
	JR_CHECK_EQ(g.blockCount, expected / 512ull);
}


JR_TEST(csd, reassemble_r2_shifts_left_by_eight)
{
	uint32_t resp[4] = {0xAABBCCDDu, 0x11223344u, 0x55667788u, 0x99AABBCCu};
	const Csd128 csd = ReassembleR2(resp);
	JR_CHECK_EQ(csd.words[0], 0xBBCCDD00u);					// resp0 << 8
	JR_CHECK_EQ(csd.words[1], (0x11223344u << 8) | 0xAAu);	// resp1<<8 | resp0>>24
	JR_CHECK_EQ(csd.words[2], (0x55667788u << 8) | 0x11u);
	JR_CHECK_EQ(csd.words[3], (0x99AABBCCu << 8) | 0x55u);
}


JR_TEST(csd, emmc_sector_count)
{
	uint8_t extCsd[512] = {0};
	// SEC_COUNT at bytes 212..215, little-endian = 0x01D59000 (30,777,344).
	extCsd[212] = 0x00;
	extCsd[213] = 0x90;
	extCsd[214] = 0xD5;
	extCsd[215] = 0x01;

	const CardGeometry g = DecodeEmmcSectorCount(extCsd);
	JR_CHECK(g.valid);
	JR_CHECK_EQ(g.blockCount, 0x01D59000ull);
	JR_CHECK_EQ(g.capacityBytes, 0x01D59000ull * 512ull);
	JR_CHECK_EQ(g.blockSize, 512u);
}
