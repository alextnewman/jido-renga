// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot

#include "framework/jr_test.h"

#include <common/intel_valleyview/RcsCore.h>


using namespace valleyview;


JR_TEST(intel_valleyview_rcs, uses_valleyview_render_registers)
{
	JR_CHECK_EQ(kRcsRingBase, 0x02000u);
	JR_CHECK_EQ(kRcsRingHws, 0x04080u);
	JR_CHECK_EQ(kRcsRingMiMode, 0x0209cu);
	JR_CHECK_EQ(kRcsRingMode, 0x0229cu);
	JR_CHECK_EQ(kRcsRingTimestamp, 0x02358u);
	JR_CHECK_EQ(kRcsRingCcid, 0x02180u);
	JR_CHECK_EQ(kRcsRingFault, 0x04094u);
	JR_CHECK_EQ(kGen6ResetRender, 1u << 1);
	JR_CHECK_EQ(kRcsModeIdle, 1u << 9);
}


JR_TEST(intel_valleyview_rcs, accepts_only_an_idle_ggtt_ring)
{
	RcsRegisterSnapshot snapshot = {};
	snapshot.miMode = kRcsModeIdle;
	JR_CHECK(IsRcsRingAvailable(snapshot));

	snapshot.control = kRingValid;
	JR_CHECK(!IsRcsRingAvailable(snapshot));
	snapshot.control = 0;
	snapshot.miMode = kRcsModeIdle | kRingStop;
	JR_CHECK(!IsRcsRingAvailable(snapshot));
	snapshot.miMode = kRcsModeIdle;
	snapshot.mode = kRingPpgttEnable;
	JR_CHECK(!IsRcsRingAvailable(snapshot));
	snapshot.mode = 0;
	snapshot.head = 8;
	JR_CHECK(!IsRcsRingAvailable(snapshot));
	snapshot.head = 0;
	snapshot.miMode = 0;
	JR_CHECK(!IsRcsRingAvailable(snapshot));
	snapshot.miMode = kRcsModeIdle;
	snapshot.ccid = kRcsCcidEnable;
	JR_CHECK(!IsRcsRingAvailable(snapshot));
}


JR_TEST(intel_valleyview_rcs, compares_only_restored_ring_state)
{
	RcsRegisterSnapshot expected = {};
	expected.start = 0x1000;
	expected.hws = 0x2000;
	expected.head = 8;
	expected.tail = 8;
	expected.control = 1;
	expected.miMode = 2;
	expected.mode = 3;

	RcsRegisterSnapshot observed = expected;
	observed.timestamp = 42;
	observed.acthd = 0x3000;
	JR_CHECK(IsRcsRingRestored(expected, observed));
	observed.start = 0;
	JR_CHECK(!IsRcsRingRestored(expected, observed));
	observed = expected;
	observed.ppDirBase = 0x4000;
	JR_CHECK(!IsRcsRingRestored(expected, observed));
	observed = expected;
	observed.ccid = 1;
	JR_CHECK(!IsRcsRingRestored(expected, observed));
}


JR_TEST(intel_valleyview_rcs, decodes_the_pre_broadwell_fault_register)
{
	const uint32 fault = 0x12345000u | kRcsFaultGgtt
		| (0x5au << kRcsFaultSourceShift)
		| (2u << kRcsFaultTypeShift) | kRcsFaultValid;
	JR_CHECK(RcsFaultIsValid(fault));
	JR_CHECK_EQ(RcsFaultAddress(fault), 0x12345000u);
	JR_CHECK_EQ(RcsFaultSource(fault), 0x5au);
	JR_CHECK_EQ(RcsFaultType(fault), 2u);
	JR_CHECK((fault & kRcsFaultGgtt) != 0);
}


JR_TEST(intel_valleyview_rcs, builds_a_bounded_kernel_batch)
{
	uint32 commands[kRcsBatchCommandCount] = {};
	const size_t count = BuildRcsDiagnosticBatch(commands,
		kRcsBatchCommandCount, 0x3000, kRcsBatchMarker);

	JR_CHECK_EQ(count, kRcsBatchCommandCount);
	JR_CHECK_EQ(commands[0], 0x10400002u);
	JR_CHECK_EQ(commands[1], 0u);
	JR_CHECK_EQ(commands[2], 0x3000u);
	JR_CHECK_EQ(commands[3], kRcsBatchMarker);
	JR_CHECK_EQ(commands[4], 0x05000000u);
	JR_CHECK_EQ(commands[5], 0u);
	JR_CHECK_EQ(BuildRcsDiagnosticBatch(commands, count - 1, 0x3000,
		kRcsBatchMarker), 0u);
}


JR_TEST(intel_valleyview_rcs, builds_a_batch_chain_and_completion)
{
	uint32 commands[kRcsRingCommandCount] = {};
	const size_t count = BuildRcsDiagnosticRing(commands,
		kRcsRingCommandCount, 0x2000, 0x3000, kRcsCompletionMarker);

	JR_CHECK_EQ(count, kRcsRingCommandCount);
	JR_CHECK_EQ((count * sizeof(uint32)) & 7, 0u);
	JR_CHECK_EQ(commands[0], 0x18800000u);
	JR_CHECK_EQ(commands[1], 0x2000u);
	JR_CHECK_EQ(commands[2], 0x12400001u);
	JR_CHECK_EQ(commands[3], 0x02358u);
	JR_CHECK_EQ(commands[4], 0x3004u);
	JR_CHECK_EQ(commands[5], 0x7a000002u);
	JR_CHECK_EQ(commands[6], 0x011050a1u);
	JR_CHECK_EQ(commands[7], 0x3008u);
	JR_CHECK_EQ(commands[8], kRcsCompletionMarker);
	JR_CHECK_EQ(commands[9], 0u);
}


JR_TEST(intel_valleyview_rcs, builds_an_isolated_ggtt_shadow_submission)
{
	uint32 commands[kRcsSubmitRingCommandCount] = {};
	const uint32 batch = 0x00203000;
	const uint32 result = 0x00202000;
	const uint32 marker = kRcsSubmitMarkerBase | 7;
	JR_CHECK_EQ(BuildRcsSubmitRing(commands, kRcsSubmitRingCommandCount,
		batch, result, marker), kRcsSubmitRingCommandCount);
	JR_CHECK_EQ(commands[0], kMiBatchBufferStart);
	JR_CHECK_EQ(commands[1], batch);
	JR_CHECK_EQ(commands[2], kMiStoreRegisterMem | kMiUseGgtt);
	JR_CHECK_EQ(commands[4], result + kRcsTimestampOffset);
	JR_CHECK_EQ(commands[5], kGen7PipeControl);
	JR_CHECK_EQ(commands[6], kRcsCompletionFlags);
	JR_CHECK_EQ(commands[7], result + kRcsCompletionOffset);
	JR_CHECK_EQ(commands[8], marker);
	JR_CHECK_EQ(commands[9], kMiNoop);

	JR_CHECK_EQ(BuildRcsSubmitRing(NULL, kRcsSubmitRingCommandCount,
		batch, result, marker), 0u);
	JR_CHECK_EQ(BuildRcsSubmitRing(commands, kRcsSubmitRingCommandCount - 1,
		batch, result, marker), 0u);
	JR_CHECK_EQ(BuildRcsSubmitRing(commands, kRcsSubmitRingCommandCount,
		batch + 4, result, marker), 0u);
	JR_CHECK_EQ(BuildRcsSubmitRing(commands, kRcsSubmitRingCommandCount,
		batch, result, 0), 0u);
}


JR_TEST(intel_valleyview_rcs, builds_the_combined_shader_chain)
{
	uint32 commands[kRcsCombinedRingCommandCount] = {};
	const size_t count = BuildRcsCombinedDiagnosticRing(commands,
		kRcsCombinedRingCommandCount, 0x2000, 0x4000, 0x3000,
		kRcsShaderCompletionMarker);

	JR_CHECK_EQ(count, kRcsCombinedRingCommandCount);
	JR_CHECK_EQ((count * sizeof(uint32)) & 7, 0u);
	JR_CHECK_EQ(commands[0], 0x18800000u);
	JR_CHECK_EQ(commands[1], 0x2000u);
	JR_CHECK_EQ(commands[2], 0x12400001u);
	JR_CHECK_EQ(commands[3], 0x02358u);
	JR_CHECK_EQ(commands[4], 0x3004u);
	JR_CHECK_EQ(commands[5], 0x18800000u);
	JR_CHECK_EQ(commands[6], 0x4000u);
	JR_CHECK_EQ(commands[7], 0x7a000002u);
	JR_CHECK_EQ(commands[8], 0x011050a1u);
	JR_CHECK_EQ(commands[9], 0x3008u);
	JR_CHECK_EQ(commands[10], kRcsShaderCompletionMarker);
	JR_CHECK_EQ(commands[11], 0u);
	JR_CHECK_EQ(BuildRcsCombinedDiagnosticRing(commands, count - 1,
		0x2000, 0x4000, 0x3000, kRcsShaderCompletionMarker), 0u);
}


JR_TEST(intel_valleyview_rcs, handles_timestamp_wrap)
{
	JR_CHECK(RcsTimestampInWindow(10, 15, 20));
	JR_CHECK(!RcsTimestampInWindow(10, 21, 20));
	JR_CHECK(RcsTimestampInWindow(0xfffffff0u, 3, 8));
}
