// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot

#include "framework/jr_test.h"

#include "StagedRequest.h"

using namespace jr::sdhci;


JR_TEST(staged_request, aligned_request_uses_the_full_window)
{
	const StagedRequestChunk chunk
		= PlanStagedRequestChunk(4096, 512 * 1024, 512, 512 * 1024);

	JR_CHECK(chunk.IsValid());
	JR_CHECK_EQ(chunk.mediaOffset, 4096u);
	JR_CHECK_EQ(chunk.bufferOffset, 0u);
	JR_CHECK_EQ(chunk.requestBytes, 512u * 1024u);
	JR_CHECK_EQ(chunk.mediaBytes, 512u * 1024u);
	JR_CHECK(!chunk.NeedsReadBeforeWrite());
}


JR_TEST(staged_request, unaligned_request_preserves_both_edge_sectors)
{
	const StagedRequestChunk chunk
		= PlanStagedRequestChunk(4096 + 17, 1000, 512, 512 * 1024);

	JR_CHECK(chunk.IsValid());
	JR_CHECK_EQ(chunk.mediaOffset, 4096u);
	JR_CHECK_EQ(chunk.bufferOffset, 17u);
	JR_CHECK_EQ(chunk.requestBytes, 1000u);
	JR_CHECK_EQ(chunk.mediaBytes, 1024u);
	JR_CHECK(chunk.NeedsReadBeforeWrite());
}


JR_TEST(staged_request, unaligned_head_limits_the_first_chunk_to_staging)
{
	const StagedRequestChunk chunk
		= PlanStagedRequestChunk(1, 1024 * 1024, 512, 512 * 1024);

	JR_CHECK(chunk.IsValid());
	JR_CHECK_EQ(chunk.mediaOffset, 0u);
	JR_CHECK_EQ(chunk.bufferOffset, 1u);
	JR_CHECK_EQ(chunk.requestBytes, 512u * 1024u - 1u);
	JR_CHECK_EQ(chunk.mediaBytes, 512u * 1024u);
}


JR_TEST(staged_request, invalid_geometry_produces_no_chunk)
{
	JR_CHECK(!PlanStagedRequestChunk(0, 512, 0, 512 * 1024).IsValid());
	JR_CHECK(!PlanStagedRequestChunk(0, 512, 512, 511).IsValid());
	JR_CHECK(!PlanStagedRequestChunk(0, 512, 512, 513).IsValid());
}
