// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot

#include "framework/jr_test.h"

#include <common/intel_valleyview/PpgttCore.h>

#include <cstring>


using namespace valleyview;


JR_TEST(intel_valleyview_ppgtt, exposes_gen6_two_level_geometry)
{
	JR_CHECK_EQ(kPpgttVirtualAddressBytes, 0x80000000ull);
	JR_CHECK_EQ(kPpgttPageBytes, 4096u);
	JR_CHECK_EQ(kPpgttPageCount, 524288u);
	JR_CHECK_EQ(kPpgttPdeCount, 512u);
	JR_CHECK_EQ(kPpgttPtesPerTable, 1024u);
	JR_CHECK_EQ(kPpgttPdeShift, 22u);
	JR_CHECK_EQ(kPpgttPdeSpanBytes, 4u * 1024 * 1024);
	JR_CHECK_EQ(kPpgttDirectoryGgttPages, 512u);
	JR_CHECK_EQ(kPpgttDirectoryBytes, 2u * 1024 * 1024);
	JR_CHECK_EQ(kPpgttDirectoryAlignment, 64u * 1024);
	JR_CHECK_EQ(kPpgttBitmapBytes, 65536u);
}


JR_TEST(intel_valleyview_ppgtt, validates_reserved_and_bounded_va_ranges)
{
	JR_CHECK(!ValidatePpgttVaRange(0, kPpgttPageBytes));
	JR_CHECK(!ValidatePpgttVaRange(kPpgttPageBytes, 0));
	JR_CHECK(!ValidatePpgttVaRange(kPpgttPageBytes + 1, kPpgttPageBytes));
	JR_CHECK(!ValidatePpgttVaRange(kPpgttPageBytes, kPpgttPageBytes - 1));
	JR_CHECK(ValidatePpgttVaRange(kPpgttPageBytes, kPpgttPageBytes));
	JR_CHECK(ValidatePpgttVaRange(
		kPpgttVirtualAddressBytes - kPpgttPageBytes, kPpgttPageBytes));
	JR_CHECK(!ValidatePpgttVaRange(
		kPpgttVirtualAddressBytes - kPpgttPageBytes, 2 * kPpgttPageBytes));
	JR_CHECK(!ValidatePpgttVaRange(UINT64_MAX - kPpgttPageBytes + 1,
		kPpgttPageBytes));
}


JR_TEST(intel_valleyview_ppgtt, computes_pde_and_pte_boundaries)
{
	JR_CHECK_EQ(PpgttPdeIndex(0), 0u);
	JR_CHECK_EQ(PpgttPteIndex(0), 0u);
	JR_CHECK_EQ(PpgttPdeIndex(0x003fffff), 0u);
	JR_CHECK_EQ(PpgttPteIndex(0x003fffff), 1023u);
	JR_CHECK_EQ(PpgttPdeIndex(0x00400000), 1u);
	JR_CHECK_EQ(PpgttPteIndex(0x00400000), 0u);
	JR_CHECK_EQ(PpgttPdeIndex(0x7ffff000), 511u);
	JR_CHECK_EQ(PpgttPteIndex(0x7ffff000), 1023u);
}


JR_TEST(intel_valleyview_ppgtt, validates_pp_dir_ggtt_placement)
{
	uint32 ppDirBase = UINT32_MAX;
	JR_CHECK(EncodePpgttDirectoryBase(0x00110000,
		0x00400000, ppDirBase));
	JR_CHECK_EQ(ppDirBase, 0x00110000u);
	JR_CHECK(!EncodePpgttDirectoryBase(0x00111000,
		0x00400000, ppDirBase));
	JR_CHECK(!EncodePpgttDirectoryBase(0x00110000,
		0x0030ffff, ppDirBase));
	JR_CHECK(!EncodePpgttDirectoryBase(0x100000000ull,
		0x100200000ull, ppDirBase));
	JR_CHECK(!EncodePpgttDirectoryBase(0xffff0000,
		0x100200000ull, ppDirBase));
}


JR_TEST(intel_valleyview_ppgtt, encodes_physical_page_tables_and_data)
{
	uint32 entry = 0;
	JR_CHECK(EncodeGen6GttAddress(0x12345000, entry));
	JR_CHECK_EQ(entry, 0x12345000u);
	JR_CHECK(EncodeGen6GttAddress(0x100000000ull, entry));
	JR_CHECK_EQ(entry, 0x00000010u);
	JR_CHECK(EncodeGen6GttAddress(0xfffffff000ull, entry));
	JR_CHECK_EQ(entry, 0xfffffff0u);
	JR_CHECK(!EncodeGen6GttAddress(0x10000000000ull, entry));
	JR_CHECK(!EncodeGen6GttAddress(0x100000001ull, entry));

	JR_CHECK(EncodePpgttPde(0x12345000, entry));
	JR_CHECK_EQ(entry, 0x12345001u);
	JR_CHECK(EncodePpgttPde(0, entry));
	JR_CHECK_EQ(entry, 1u);
	JR_CHECK(!EncodePpgttPde(0x12345001, entry));
	JR_CHECK(EncodePpgttPde(0x100000000ull, entry));
	JR_CHECK_EQ(entry, 0x00000011u);
	JR_CHECK(EncodePpgttPde(0xfffffff000ull, entry));
	JR_CHECK_EQ(entry, 0xfffffff1u);
	JR_CHECK(!EncodePpgttPde(0x10000000000ull, entry));

	JR_CHECK(EncodePpgttDataPte(0x76543000, true, true, entry));
	JR_CHECK_EQ(entry, 0x76543007u);
	JR_CHECK(EncodePpgttDataPte(0x76543000, false, false, entry));
	JR_CHECK_EQ(entry, 0x76543001u);
	JR_CHECK(EncodePpgttDataPte(0x100000000ull, true, true, entry));
	JR_CHECK_EQ(entry, 0x00000017u);
	JR_CHECK(EncodePpgttDataPte(0xfffffff000ull, false, false, entry));
	JR_CHECK_EQ(entry, 0xfffffff1u);
	JR_CHECK(!EncodePpgttDataPte(0x76543001, true, true, entry));
	JR_CHECK(!EncodePpgttDataPte(0x10000000000ull, true, true, entry));
}


JR_TEST(intel_valleyview_ppgtt, bounds_bitmap_bit_operations)
{
	uint8 bitmap[kPpgttBitmapBytes] = {};
	bool allocated = true;
	JR_CHECK(GetPpgttBitmapBit(bitmap, sizeof(bitmap), 0, allocated));
	JR_CHECK(!allocated);
	JR_CHECK(SetPpgttBitmapBit(bitmap, sizeof(bitmap),
		kPpgttPageCount - 1));
	JR_CHECK(GetPpgttBitmapBit(bitmap, sizeof(bitmap),
		kPpgttPageCount - 1, allocated));
	JR_CHECK(allocated);
	JR_CHECK(ClearPpgttBitmapBit(bitmap, sizeof(bitmap),
		kPpgttPageCount - 1));
	JR_CHECK(GetPpgttBitmapBit(bitmap, sizeof(bitmap),
		kPpgttPageCount - 1, allocated));
	JR_CHECK(!allocated);
	JR_CHECK(!GetPpgttBitmapBit(bitmap, sizeof(bitmap),
		kPpgttPageCount, allocated));
	JR_CHECK(!SetPpgttBitmapBit(bitmap, sizeof(bitmap) - 1,
		kPpgttPageCount - 1));
	JR_CHECK(!ClearPpgttBitmapBit(NULL, sizeof(bitmap), 0));
}


JR_TEST(intel_valleyview_ppgtt, allocates_reuses_and_aligns_bitmap_runs)
{
	uint8 bitmap[kPpgttBitmapBytes] = {};
	uint32 firstPage = kInvalidPpgttPage;

	JR_CHECK(FindFreePpgttRun(bitmap, sizeof(bitmap), 3, 1, firstPage));
	JR_CHECK_EQ(firstPage, 1u);
	for (uint32 page = firstPage; page < firstPage + 3; page++)
		JR_CHECK(SetPpgttBitmapBit(bitmap, sizeof(bitmap), page));

	JR_CHECK(FindFreePpgttRun(bitmap, sizeof(bitmap), 2, 1, firstPage));
	JR_CHECK_EQ(firstPage, 4u);
	JR_CHECK(ClearPpgttBitmapBit(bitmap, sizeof(bitmap), 2));
	JR_CHECK(FindFreePpgttRun(bitmap, sizeof(bitmap), 1, 1, firstPage));
	JR_CHECK_EQ(firstPage, 2u);

	std::memset(bitmap, 0, sizeof(bitmap));
	JR_CHECK(FindFreePpgttRun(bitmap, sizeof(bitmap), 4, 8, firstPage));
	JR_CHECK_EQ(firstPage, 8u);
	for (uint32 page = 8; page < 12; page++)
		JR_CHECK(SetPpgttBitmapBit(bitmap, sizeof(bitmap), page));
	JR_CHECK(FindFreePpgttRun(bitmap, sizeof(bitmap), 4, 8, firstPage));
	JR_CHECK_EQ(firstPage, 16u);
}


JR_TEST(intel_valleyview_ppgtt, reports_exhaustion_without_using_page_zero)
{
	uint8 bitmap[kPpgttBitmapBytes];
	std::memset(bitmap, 0xff, sizeof(bitmap));
	JR_CHECK(ClearPpgttBitmapBit(bitmap, sizeof(bitmap), 0));

	uint32 firstPage = 0;
	JR_CHECK(!FindFreePpgttRun(bitmap, sizeof(bitmap), 1, 1, firstPage));
	JR_CHECK_EQ(firstPage, kInvalidPpgttPage);

	JR_CHECK(ClearPpgttBitmapBit(bitmap, sizeof(bitmap),
		kPpgttPageCount - 1));
	JR_CHECK(FindFreePpgttRun(bitmap, sizeof(bitmap), 1, 1, firstPage));
	JR_CHECK_EQ(firstPage, kPpgttPageCount - 1);
}


JR_TEST(intel_valleyview_ppgtt, rejects_invalid_bitmap_requests)
{
	uint8 bitmap[kPpgttBitmapBytes] = {};
	uint32 firstPage = 7;
	JR_CHECK(!FindFreePpgttRun(bitmap, sizeof(bitmap), 0, 1, firstPage));
	JR_CHECK_EQ(firstPage, kInvalidPpgttPage);
	JR_CHECK(!FindFreePpgttRun(bitmap, sizeof(bitmap),
		kPpgttPageCount, 1, firstPage));
	JR_CHECK(!FindFreePpgttRun(bitmap, sizeof(bitmap), UINT32_MAX,
		1, firstPage));
	JR_CHECK(!FindFreePpgttRun(bitmap, sizeof(bitmap), 1, 0, firstPage));
	JR_CHECK(!FindFreePpgttRun(bitmap, sizeof(bitmap), 1, 3, firstPage));
	JR_CHECK(!FindFreePpgttRun(bitmap, sizeof(bitmap), 1,
		0x80000000u, firstPage));
	JR_CHECK(!FindFreePpgttRun(bitmap, sizeof(bitmap) - 1,
		1, 1, firstPage));
	JR_CHECK(!FindFreePpgttRun(NULL, sizeof(bitmap), 1, 1, firstPage));
}
