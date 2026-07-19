// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot

#include "framework/jr_test.h"

#include <common/intel_valleyview/RenderMemoryCore.h>


using namespace valleyview;


JR_TEST(intel_valleyview_render, normalizes_bounded_buffer_sizes)
{
	uint64 size = 0;
	JR_CHECK(!NormalizeRenderBufferSize(0, size));
	JR_CHECK(NormalizeRenderBufferSize(1, size));
	JR_CHECK_EQ(size, (uint64)kPageSize);
	JR_CHECK(NormalizeRenderBufferSize(kPageSize + 1, size));
	JR_CHECK_EQ(size, (uint64)kPageSize * 2);
	JR_CHECK(NormalizeRenderBufferSize(kRenderMaxBufferSize, size));
	JR_CHECK_EQ(size, kRenderMaxBufferSize);
	JR_CHECK(!NormalizeRenderBufferSize(kRenderMaxBufferSize + 1, size));
	JR_CHECK(!NormalizeRenderBufferSize(UINT64_MAX, size));
}


JR_TEST(intel_valleyview_render, finds_only_complete_free_ggtt_runs)
{
	const uint32 ptes[] = {
		kGen7PtePresent, 0, 0, kGen7PtePresent, 0, 0, 0, 0
	};
	RenderGgttSearch search = {};
	JR_CHECK(InitializeRenderGgttSearch(search, 1, 8, 3, 0));
	for (uint32 page = 1; page < 8; page++)
		AdvanceRenderGgttSearch(search, page, ptes[page]);
	JR_CHECK(search.found);
	JR_CHECK_EQ(search.offset, 4u * kPageSize);
}


JR_TEST(intel_valleyview_render, rejects_fragmented_or_reserved_ggtt_space)
{
	RenderGgttSearch search = {};
	JR_CHECK(!InitializeRenderGgttSearch(search, 8, 8, 1, 0));
	JR_CHECK(!InitializeRenderGgttSearch(search, 4, 8, 5, 0));
	JR_CHECK(InitializeRenderGgttSearch(search, 2, 7, 3, 0));
	AdvanceRenderGgttSearch(search, 1, 0);
	AdvanceRenderGgttSearch(search, 2, 0);
	AdvanceRenderGgttSearch(search, 4, 0);
	AdvanceRenderGgttSearch(search, 5, kGen7PtePresent);
	AdvanceRenderGgttSearch(search, 6, 0);
	JR_CHECK(!search.found);
	JR_CHECK_EQ(search.offset, kInvalidRenderGgttOffset);
}


JR_TEST(intel_valleyview_render, recognizes_the_firmware_scratch_pte)
{
	const uint32 scratch = 0x7fdfc401;
	RenderGgttSearch search = {};
	JR_CHECK(InitializeRenderGgttSearch(search, 1, 5, 2, scratch));
	AdvanceRenderGgttSearch(search, 1, scratch);
	AdvanceRenderGgttSearch(search, 2, 0);
	AdvanceRenderGgttSearch(search, 3, scratch);
	AdvanceRenderGgttSearch(search, 4, scratch);
	JR_CHECK(search.found);
	JR_CHECK_EQ(search.offset, 3u * kPageSize);
}


JR_TEST(intel_valleyview_render, requires_coherent_supported_domains)
{
	JR_CHECK(CanTransitionRenderBufferDomain(kRenderDomainCpu,
		kRenderDomainBcs, kRenderBufferCpuCached));
	JR_CHECK(CanTransitionRenderBufferDomain(kRenderDomainBcs,
		kRenderDomainCpu, kRenderBufferCpuCached));
	JR_CHECK(!CanTransitionRenderBufferDomain(
		static_cast<RenderBufferDomain>(0), kRenderDomainCpu,
		kRenderBufferCpuCached));
	JR_CHECK(!CanTransitionRenderBufferDomain(kRenderDomainCpu,
		kRenderDomainBcs, 0));
	JR_CHECK(!CanTransitionRenderBufferDomain(kRenderDomainCpu,
		kRenderDomainBcs, kRenderBufferCpuCached | (1u << 31)));
}


JR_TEST(intel_valleyview_render, builds_coordinate_dependent_test_words)
{
	const uint32 first = RenderMemoryTestWord(0,
		kRenderMemoryTestDefaultSeed);
	const uint32 second = RenderMemoryTestWord(1,
		kRenderMemoryTestDefaultSeed);
	JR_CHECK_NE(first, second);
	JR_CHECK_EQ(RenderMemoryTestDestinationWord(0,
		kRenderMemoryTestDefaultSeed), ~first);
	JR_CHECK_EQ(kRenderMemoryTestBytes, kPageSize);
	JR_CHECK_EQ(kRenderMemoryTestWords, kPageSize / sizeof(uint32));
}
