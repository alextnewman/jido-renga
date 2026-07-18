// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot

#include "framework/jr_test.h"

#include <common/intel_valleyview/GpuCore.h>


using namespace valleyview;


JR_TEST(intel_valleyview_gpu, encodes_writable_bay_trail_ptes)
{
	uint32 pte = 0;
	JR_CHECK(EncodeBytPte(0x7c123000, true, false, pte));
	JR_CHECK_EQ(pte, 0x7c123003u);
	JR_CHECK(EncodeBytPte(0x7c123000, true, true, pte));
	JR_CHECK_EQ(pte, 0x7c123007u);
	JR_CHECK(!EncodeBytPte(0x7c123001, true, false, pte));
	JR_CHECK(!EncodeBytPte(0x100000000ull, true, false, pte));
}


JR_TEST(intel_valleyview_gpu, uses_gen7_bcs_register_layout)
{
	JR_CHECK_EQ(kBcsRingBase, 0x22000u);
	JR_CHECK_EQ(kRingHws, 0x4280u);
	JR_CHECK_EQ(kRingMiMode, 0x2209cu);
	JR_CHECK_EQ(kRingMode, 0x2229cu);
	JR_CHECK_EQ(kRingStop, 1u << 8);
	JR_CHECK_EQ(kRingPpgttEnable, 1u << 9);
}


JR_TEST(intel_valleyview_gpu, reserves_the_top_four_ggtt_pages)
{
	uint32 offset = 0;
	JR_CHECK(SelectGpuTestGgttOffset(256u * 1024 * 1024, 0,
		3u * 1024 * 1024, offset));
	JR_CHECK_EQ(offset, 0x0fffc000u);
	JR_CHECK(!SelectGpuTestGgttOffset(kGpuTestBytes, 0, kPageSize, offset));
	JR_CHECK(!SelectGpuTestGgttOffset(kGpuTestBytes - 1, 0, 0, offset));
}


JR_TEST(intel_valleyview_gpu, builds_bounded_gen7_bcs_fill_and_copy)
{
	uint32 commands[kBcsSelfTestCommandCount] = {};
	const size_t count = BuildBcsSelfTestCommands(commands,
		kBcsSelfTestCommandCount, 0x0fffd000, 0x0fffe000, 0x0ffff000,
		kGpuTestPattern, kGpuCompletionMarker);

	JR_CHECK_EQ(count, kBcsSelfTestCommandCount);
	JR_CHECK_EQ((count * sizeof(uint32)) & 7, 0u);
	JR_CHECK_EQ(commands[0], 0x54300004u);
	JR_CHECK_EQ(commands[1], 0x03f00040u);
	JR_CHECK_EQ(commands[3], 0x00100010u);
	JR_CHECK_EQ(commands[4], 0x0fffd000u);
	JR_CHECK_EQ(commands[5], kGpuTestPattern);
	JR_CHECK_EQ(commands[6], 0x54f00006u);
	JR_CHECK_EQ(commands[7], 0x03cc0040u);
	JR_CHECK_EQ(commands[10], 0x0fffe000u);
	JR_CHECK_EQ(commands[13], 0x0fffd000u);
	JR_CHECK_EQ(commands[14], 0x13204001u);
	JR_CHECK_EQ(commands[15], 0x104u);
	JR_CHECK_EQ(commands[16], kGpuCompletionMarker);
	JR_CHECK_EQ(BuildBcsSelfTestCommands(commands, count - 1, 0x1000,
		0x2000, 0x3000, 0, 0), 0u);
}


JR_TEST(intel_valleyview_gpu, reports_the_first_mismatching_dword)
{
	uint32 words[4] = {7, 7, 3, 7};
	uint32 offset = UINT32_MAX;
	uint32 observed = UINT32_MAX;
	JR_CHECK(FindWordMismatch(words, 4, 7, offset, observed));
	JR_CHECK_EQ(offset, 8u);
	JR_CHECK_EQ(observed, 3u);
	JR_CHECK(!FindWordMismatch(words, 2, 7, offset, observed));
}
