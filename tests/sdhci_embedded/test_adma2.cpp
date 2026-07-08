// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot

#include "framework/jr_test.h"

#include "Adma2.h"

using namespace jr::sdhci;


JR_TEST(adma2, single_segment_finalizes_with_end)
{
	Adma2Descriptor table[4];
	Adma2Builder b(table, 4);
	JR_CHECK(b.AddSegment(0x1000, 512));
	JR_CHECK(b.Finalize());

	JR_CHECK_EQ(b.Count(), 1u);
	JR_CHECK_EQ(table[0].length, 512);
	JR_CHECK_EQ(table[0].address, 0x1000u);
	JR_CHECK_EQ(table[0].attributes, static_cast<uint8_t>(Adma2Attr::End));
	JR_CHECK_EQ(table[0].reserved, 0);
}


JR_TEST(adma2, zero_length_is_noop)
{
	Adma2Descriptor table[4];
	Adma2Builder b(table, 4);
	JR_CHECK(b.AddSegment(0x2000, 0));
	JR_CHECK_EQ(b.Count(), 0u);
	// Nothing to finalize.
	JR_CHECK(!b.Finalize());
}


JR_TEST(adma2, oversized_segment_splits)
{
	Adma2Descriptor table[8];
	Adma2Builder b(table, 8);
	// 100000 bytes = 65536 + 34464.
	JR_CHECK(b.AddSegment(0x2000, 100000));
	JR_CHECK(b.Finalize());

	JR_CHECK_EQ(b.Count(), 2u);
	JR_CHECK_EQ(table[0].length, 0);				// 65536 wraps to 0
	JR_CHECK_EQ(table[0].address, 0x2000u);
	JR_CHECK_EQ(table[0].attributes, static_cast<uint8_t>(Adma2Attr::Valid));
	JR_CHECK_EQ(table[1].length, 34464);
	JR_CHECK_EQ(table[1].address, 0x2000u + 65536u);
	JR_CHECK_EQ(table[1].attributes, static_cast<uint8_t>(Adma2Attr::End));
}


JR_TEST(adma2, exact_max_segment_encodes_zero_length)
{
	Adma2Descriptor table[2];
	Adma2Builder b(table, 2);
	JR_CHECK(b.AddSegment(0x4000, kAdma2MaxSegmentBytes));
	JR_CHECK_EQ(b.Count(), 1u);
	JR_CHECK_EQ(table[0].length, 0);
}


JR_TEST(adma2, multi_segment_only_last_is_end)
{
	Adma2Descriptor table[4];
	Adma2Builder b(table, 4);
	JR_CHECK(b.AddSegment(0x1000, 2048));
	JR_CHECK(b.AddSegment(0x8000, 4096));
	JR_CHECK(b.Finalize());

	JR_CHECK_EQ(b.Count(), 2u);
	JR_CHECK_EQ(table[0].attributes, static_cast<uint8_t>(Adma2Attr::Valid));
	JR_CHECK_EQ(table[1].attributes, static_cast<uint8_t>(Adma2Attr::End));
	JR_CHECK_EQ(table[0].address, 0x1000u);
	JR_CHECK_EQ(table[1].address, 0x8000u);
}


JR_TEST(adma2, overflow_is_reported_not_written)
{
	Adma2Descriptor table[1];
	Adma2Builder b(table, 1);
	JR_CHECK(b.AddSegment(0x1000, 512));		// fills the single slot
	JR_CHECK(!b.AddSegment(0x2000, 512));		// no room
	JR_CHECK(b.Overflowed());
	JR_CHECK(!b.Finalize());					// refuses to finalize a bad table
}
