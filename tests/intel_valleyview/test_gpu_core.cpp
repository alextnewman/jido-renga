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


JR_TEST(intel_valleyview_gpu, requires_bcs_to_drop_all_runtime_addresses)
{
	GpuRegisterSnapshot snapshot = {};
	JR_CHECK(IsBcsRingQuiesced(snapshot));

	snapshot.bcsControl = kRingValid;
	JR_CHECK(!IsBcsRingQuiesced(snapshot));
	snapshot.bcsControl = 0;
	snapshot.bcsStart = 0x0fffc000;
	JR_CHECK(!IsBcsRingQuiesced(snapshot));
	snapshot.bcsStart = 0;
	snapshot.bcsHws = 0x0fffd000;
	JR_CHECK(!IsBcsRingQuiesced(snapshot));
	snapshot.bcsHws = 0;
	snapshot.bcsHead = 8;
	JR_CHECK(!IsBcsRingQuiesced(snapshot));
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


JR_TEST(intel_valleyview_gpu, builds_runtime_fill_and_copy_commands)
{
	uint32 commands[18] = {};
	size_t count = 0;
	JR_CHECK(AppendBcsFill(commands, 18, count, 0x100000, 5504,
		0x11223344, 3, 4, 9, 12));
	JR_CHECK(AppendBcsCopy(commands, 18, count, 0x100000, 5504,
		20, 30, 40, 50, 7, 8));
	JR_CHECK(AppendBcsCompletion(commands, 18, count, 0x12345678));
	JR_CHECK_EQ(count, 18u);
	JR_CHECK_EQ(commands[0], 0x54300004u);
	JR_CHECK_EQ(commands[2], 0x00040003u);
	JR_CHECK_EQ(commands[3], 0x000d000au);
	JR_CHECK_EQ(commands[4], 0x00100000u);
	JR_CHECK_EQ(commands[5], 0x11223344u);
	JR_CHECK_EQ(commands[6], 0x54f00006u);
	JR_CHECK_EQ(commands[8], 0x00320028u);
	JR_CHECK_EQ(commands[9], 0x003b0030u);
	JR_CHECK_EQ(commands[11], 0x001e0014u);
	JR_CHECK_EQ(commands[14], 0x13204001u);
	JR_CHECK_EQ(commands[16], 0x12345678u);
}
