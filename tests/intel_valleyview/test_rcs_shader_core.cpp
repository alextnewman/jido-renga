// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot

#include "framework/jr_test.h"

#include <common/intel_valleyview/RcsShaderCore.h>

#include <stdlib.h>
#include <string.h>


using namespace valleyview;


JR_TEST(intel_valleyview_rcs_shader, matches_the_valleyview_clear_layout)
{
	JR_CHECK_EQ(kRcsShaderMaxThreads, 36u);
	JR_CHECK_EQ(kIvbClearKernelBytes, 832u);
	JR_CHECK_EQ(kRcsShaderStateStart, 4096u);
	JR_CHECK_EQ(kRcsShaderSurfaceStart, 8192u);
	JR_CHECK_EQ(kRcsShaderSurfaceBytes, 65536u);
	JR_CHECK_EQ(kRcsShaderObjectBytes, 73728u);
	JR_CHECK_EQ(kRcsShaderTotalBytes, 77824u);
	JR_CHECK_EQ(kRcsShaderTotalPages, 19u);
}


JR_TEST(intel_valleyview_rcs_shader, builds_the_linux_gen7_batch)
{
	uint8* memory = static_cast<uint8*>(calloc(1, kRcsShaderTotalBytes));
	JR_CHECK(memory != NULL);
	if (memory == NULL)
		return;

	RcsShaderLayout layout = {};
	JR_CHECK(BuildRcsShaderBatch(memory, kRcsShaderTotalBytes, 0x03000000,
		layout));
	JR_CHECK_EQ(layout.commandBytes, 1808u);
	JR_CHECK_EQ(layout.kernelOffset, 4096u);
	JR_CHECK_EQ(layout.surfaceStateOffset, 4928u);
	JR_CHECK_EQ(layout.bindingTableOffset, 4960u);
	JR_CHECK_EQ(layout.interfaceDescriptorOffset, 4992u);
	JR_CHECK_EQ(layout.surfaceOffset, 8192u);
	JR_CHECK_EQ(layout.guardOffset, 73728u);

	const uint32* words = reinterpret_cast<const uint32*>(memory);
	JR_CHECK_EQ(words[0], 0x7a000002u);
	JR_CHECK_EQ(words[1], 0x00101021u);
	JR_CHECK_EQ(words[14], 0x11000003u);
	JR_CHECK_EQ(words[15], 0x00007000u);
	JR_CHECK_EQ(words[16], 0xffff0004u);
	JR_CHECK_EQ(words[17], 0x00007004u);
	JR_CHECK_EQ(words[18], 0xffff0040u);
	JR_CHECK_EQ(words[43], 0x69040001u);
	JR_CHECK_EQ(words[59], 0x61010008u);
	JR_CHECK_EQ(words[61], 0x03001381u);

	const uint32* kernel = reinterpret_cast<const uint32*>(
		memory + layout.kernelOffset);
	JR_CHECK_EQ(kernel[0], kIvbClearKernel[0]);
	JR_CHECK_EQ(kernel[207], kIvbClearKernel[207]);

	const uint32* surface = reinterpret_cast<const uint32*>(
		memory + layout.surfaceStateOffset);
	JR_CHECK_EQ(surface[0], 0x23000100u);
	JR_CHECK_EQ(surface[1], 0x03002000u);
	JR_CHECK_EQ(surface[2], 0x001f007fu);
	JR_CHECK_EQ(surface[3], 0x00000200u);

	const uint32* binding = reinterpret_cast<const uint32*>(
		memory + layout.bindingTableOffset);
	JR_CHECK_EQ(binding[0], 0x00000340u);
	const uint32* descriptor = reinterpret_cast<const uint32*>(
		memory + layout.interfaceDescriptorOffset);
	JR_CHECK_EQ(descriptor[0], 0x00001000u);
	JR_CHECK_EQ(descriptor[1], 0x00002080u);
	JR_CHECK_EQ(descriptor[3], 0x00000361u);

	JR_CHECK(!BuildRcsShaderBatch(memory, kRcsShaderTotalBytes - 1,
		0x03000000, layout));
	JR_CHECK(!BuildRcsShaderBatch(memory, kRcsShaderTotalBytes,
		0x03000001, layout));
	free(memory);
}


JR_TEST(intel_valleyview_rcs_shader, analyzes_output_and_guard)
{
	uint32* surface = static_cast<uint32*>(
		malloc(kRcsShaderSurfaceBytes));
	uint32* guard = static_cast<uint32*>(malloc(kRcsShaderGuardBytes));
	JR_CHECK(surface != NULL);
	JR_CHECK(guard != NULL);
	if (surface == NULL || guard == NULL) {
		free(surface);
		free(guard);
		return;
	}

	for (uint32 index = 0;
			index < kRcsShaderSurfaceBytes / sizeof(uint32); index++) {
		surface[index] = kRcsShaderSentinel;
	}
	for (uint32 index = 0;
			index < kRcsShaderGuardBytes / sizeof(uint32); index++) {
		guard[index] = kRcsShaderSentinel;
	}
	for (uint32 index = 0;
			index < kRcsShaderSurfaceBytes / sizeof(uint32); index++) {
		if (IsExpectedRcsShaderZeroDword(index))
			surface[index] = 0;
	}

	RcsShaderAnalysis analysis = {};
	JR_CHECK(AnalyzeRcsShaderOutput(surface, kRcsShaderSurfaceBytes, guard,
		kRcsShaderGuardBytes, kRcsShaderSentinel, analysis));
	JR_CHECK_EQ(kRcsShaderExpectedZeroDwords, 2048u);
	JR_CHECK_EQ(kRcsShaderExpectedSentinelDwords, 14336u);
	JR_CHECK_EQ(analysis.zeroDwords, kRcsShaderExpectedZeroDwords);
	JR_CHECK_EQ(analysis.sentinelDwords,
		kRcsShaderExpectedSentinelDwords);
	JR_CHECK_EQ(analysis.unexpectedDwords, 0u);
	JR_CHECK_EQ(analysis.firstChangedOffset, 0u);
	JR_CHECK_EQ(analysis.lastChangedOffset, 16348u);
	JR_CHECK_EQ(analysis.firstUnexpectedOffset, UINT32_MAX);
	JR_CHECK_EQ(analysis.guardMismatchOffset, UINT32_MAX);
	JR_CHECK_NE(analysis.checksum, 0ull);

	for (uint32 index = 0;
			index < kRcsShaderSurfaceBytes / sizeof(uint32); index++) {
		surface[index] = index % 8 == 7 ? 0 : kRcsShaderSentinel;
	}
	JR_CHECK(AnalyzeRcsShaderOutput(surface, kRcsShaderSurfaceBytes, guard,
		kRcsShaderGuardBytes, kRcsShaderSentinel, analysis));
	JR_CHECK_EQ(analysis.zeroDwords, kRcsShaderExpectedZeroDwords);
	JR_CHECK_EQ(analysis.sentinelDwords,
		kRcsShaderExpectedSentinelDwords);
	JR_CHECK_EQ(analysis.unexpectedDwords, 0u);
	JR_CHECK_EQ(analysis.firstChangedOffset, 28u);
	JR_CHECK_EQ(analysis.lastChangedOffset,
		kRcsShaderSurfaceBytes - sizeof(uint32));
	JR_CHECK_EQ(analysis.firstUnexpectedOffset, UINT32_MAX);

	const uint32 invalidZeroDwordCounts[] = {64, 128, 2047, 2049};
	for (uint32 zeroDwords : invalidZeroDwordCounts) {
		for (uint32 index = 0;
				index < kRcsShaderSurfaceBytes / sizeof(uint32); index++) {
			surface[index] = index < zeroDwords ? 0 : kRcsShaderSentinel;
		}
		JR_CHECK(!AnalyzeRcsShaderOutput(surface, kRcsShaderSurfaceBytes,
			guard, kRcsShaderGuardBytes, kRcsShaderSentinel, analysis));
		JR_CHECK_EQ(analysis.zeroDwords, zeroDwords);
		JR_CHECK_EQ(analysis.unexpectedDwords, 0u);
	}

	for (uint32 index = 0;
			index < kRcsShaderSurfaceBytes / sizeof(uint32); index++) {
		surface[index] = index % 8 == 7 ? 0 : kRcsShaderSentinel;
	}
	const uint32 unexpectedIndex = 3;
	surface[unexpectedIndex] = 0x12345678;
	JR_CHECK(!AnalyzeRcsShaderOutput(surface, kRcsShaderSurfaceBytes, guard,
		kRcsShaderGuardBytes, kRcsShaderSentinel, analysis));
	JR_CHECK_EQ(analysis.unexpectedDwords, 1u);
	JR_CHECK_EQ(analysis.firstUnexpectedOffset,
		unexpectedIndex * sizeof(uint32));
	JR_CHECK_EQ(analysis.firstUnexpectedValue, 0x12345678u);
	surface[unexpectedIndex] = kRcsShaderSentinel;

	guard[3] = 0;
	JR_CHECK(!AnalyzeRcsShaderOutput(surface, kRcsShaderSurfaceBytes, guard,
		kRcsShaderGuardBytes, kRcsShaderSentinel, analysis));
	JR_CHECK_EQ(analysis.guardMismatchOffset, 12u);
	JR_CHECK_EQ(analysis.guardObserved, 0u);

	free(surface);
	free(guard);
}
