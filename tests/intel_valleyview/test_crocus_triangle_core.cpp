// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot

#include "framework/jr_test.h"

#include <common/intel_valleyview/CrocusTriangleCore.h>

#include <vector>


using namespace valleyview;


JR_TEST(intel_valleyview_crocus_triangle, preserves_the_linear_corpus_layout)
{
	JR_CHECK_EQ(kCrocusTriangleBatchBytes,
		sizeof(kCrocusTriangleCommandWords));
	JR_CHECK_EQ(kCrocusTriangleStride, kCrocusTriangleWidth * sizeof(uint32));
	JR_CHECK_EQ(kCrocusTriangleTargetBytes, 393216u);
	JR_CHECK_EQ(kCrocusTriangleBoSizes[kCrocusTriangleTargetBo],
		kCrocusTriangleTargetBytes);
	JR_CHECK_EQ(kCrocusTriangleStateWords[0x1a0 / sizeof(uint32)],
		0x3300043fu);
	JR_CHECK_EQ(kCrocusTriangleStateWords[0x1ac / sizeof(uint32)],
		kCrocusTriangleStride - 1);
	JR_CHECK_EQ(kCrocusTriangleStateWords[0x2a0 / sizeof(uint32)],
		0x3300043fu);
}


JR_TEST(intel_valleyview_crocus_triangle, patches_and_parses_the_exact_batch)
{
	std::vector<std::vector<uint8> > storage(kCrocusTriangleBoCount);
	void* addresses[kCrocusTriangleBoCount] = {};
	uint32 renderAddresses[kCrocusTriangleBoCount] = {};
	size_t byteCounts[kCrocusTriangleBoCount] = {};
	for (uint32 index = 0; index < kCrocusTriangleBoCount; index++) {
		storage[index].resize(kCrocusTriangleBoSizes[index]);
		addresses[index] = storage[index].data();
		byteCounts[index] = storage[index].size();
		renderAddresses[index] = 0x00100000 + index * 0x00100000;
		JR_CHECK(InitializeCrocusTriangleBo(index, addresses[index],
			byteCounts[index]));
	}
	JR_CHECK(PatchCrocusTriangleRelocations(addresses, renderAddresses,
		byteCounts, kCrocusTriangleBoCount));

	for (size_t index = 0;
			index < sizeof(kCrocusTriangleRelocations)
				/ sizeof(kCrocusTriangleRelocations[0]); index++) {
		const CrocusTriangleRelocation& relocation
			= kCrocusTriangleRelocations[index];
		const uint32 value = *reinterpret_cast<const uint32*>(
			static_cast<const uint8*>(addresses[relocation.sourceBo])
				+ relocation.byteOffset);
		JR_CHECK_EQ(value,
			renderAddresses[relocation.targetBo] + relocation.delta);
	}

	const RenderCommandResult result = ParseRenderCommands(
		addresses[kCrocusTriangleCommandBo], kCrocusTriangleBatchBytes);
	JR_CHECK_EQ(result.status, kRenderCommandStatusAccepted);
	JR_CHECK_EQ(result.reason, kRenderCommandReasonNone);
	JR_CHECK_EQ(result.primitiveCount, 3u);
	JR_CHECK(result.sawEnd);
}


JR_TEST(intel_valleyview_crocus_triangle, verifies_color_and_padding_invariants)
{
	std::vector<uint32> target(
		kCrocusTriangleTargetBytes / sizeof(uint32),
		kCrocusTriangleSentinel);
	const uint32 visiblePixels = kCrocusTriangleWidth * kCrocusTriangleHeight;
	for (uint32 pixel = 0; pixel < visiblePixels; pixel++)
		target[pixel] = 0xff4d1a4d;
	for (uint32 y = 15; y < 285; y++) {
		const float vertical = static_cast<float>(y - 15) / 270.0f;
		const uint32 halfWidth
			= static_cast<uint32>(135.0f * vertical);
		const uint32 first = 150 - halfWidth;
		const uint32 last = 150 + halfWidth;
		for (uint32 x = first; x <= last; x++) {
			const float horizontal = last == first ? 0.5f
				: static_cast<float>(x - first) / (last - first);
			const uint32 red
				= static_cast<uint32>((1.0f - vertical) * 255.0f);
			const uint32 green = static_cast<uint32>(
				vertical * (1.0f - horizontal) * 255.0f);
			const uint32 blue = static_cast<uint32>(
				vertical * horizontal * 255.0f);
			target[y * kCrocusTriangleWidth + x]
				= 0xff000000 | (red << 16) | (green << 8) | blue;
		}
	}

	CrocusTriangleAnalysis analysis = {};
	JR_CHECK(AnalyzeCrocusTriangle(target.data(),
		target.size() * sizeof(uint32), analysis));
	JR_CHECK_EQ(analysis.visibleSentinelPixels, 0u);
	JR_CHECK_EQ(analysis.paddingMismatchDwords, 0u);
	JR_CHECK_EQ(analysis.opaquePixels, visiblePixels);
	JR_CHECK(analysis.clearPixels >= 50000);
	JR_CHECK(analysis.trianglePixels >= 30000);
	JR_CHECK_EQ(analysis.coverageRowsMatched, 6u);
	JR_CHECK_EQ(analysis.interpolationSamplesMatched, 3u);

	target[visiblePixels] = 0;
	JR_CHECK(!AnalyzeCrocusTriangle(target.data(),
		target.size() * sizeof(uint32), analysis));
	JR_CHECK_EQ(analysis.paddingMismatchDwords, 1u);

	target[visiblePixels] = kCrocusTriangleSentinel;
	for (uint32 pixel = 0; pixel < visiblePixels; pixel++)
		target[pixel] = pixel < 36000 ? 0xffff0000 : 0xff4d1a4d;
	JR_CHECK(!AnalyzeCrocusTriangle(target.data(),
		target.size() * sizeof(uint32), analysis));
	JR_CHECK(analysis.coverageRowsMatched < 6);
}
