// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot

#include "framework/jr_test.h"

#include <common/intel_valleyview/RenderSubmitCore.h>


using namespace valleyview;


JR_TEST(intel_valleyview_render_submit, validates_bounded_batch_ranges)
{
	JR_CHECK_EQ(kRcsSubmitBatchPages * kPageSize,
		kRenderCommandMaxBatchBytes);
	JR_CHECK_EQ(kRenderSubmitMaxObjects, kRenderMaxClientBuffers);
	JR_CHECK(ValidateRenderSubmitBatchRange(2 * kPageSize, 0, 4));
	JR_CHECK(ValidateRenderSubmitBatchRange(2 * kPageSize, kPageSize,
		kPageSize));
	JR_CHECK(!ValidateRenderSubmitBatchRange(kPageSize, 0, 0));
	JR_CHECK(!ValidateRenderSubmitBatchRange(kPageSize, 2, 4));
	JR_CHECK(!ValidateRenderSubmitBatchRange(kPageSize, 0, 6));
	JR_CHECK(!ValidateRenderSubmitBatchRange(kPageSize, kPageSize - 4, 8));
	JR_CHECK(!ValidateRenderSubmitBatchRange(
		kRenderCommandMaxBatchBytes + kPageSize, 0,
		kRenderCommandMaxBatchBytes + 4));
}


JR_TEST(intel_valleyview_render_submit, requires_unique_owned_object_handles)
{
	const uint32 valid[] = {7, 11, 19};
	JR_CHECK(ValidateRenderSubmitObjectHandles(valid, 3, 11));
	JR_CHECK(!ValidateRenderSubmitObjectHandles(NULL, 3, 11));
	JR_CHECK(!ValidateRenderSubmitObjectHandles(valid, 0, 11));
	JR_CHECK(!ValidateRenderSubmitObjectHandles(valid, 3, 0));
	JR_CHECK(!ValidateRenderSubmitObjectHandles(valid, 3, 23));

	const uint32 zero[] = {7, 0, 19};
	const uint32 duplicate[] = {7, 11, 7};
	JR_CHECK(!ValidateRenderSubmitObjectHandles(zero, 3, 7));
	JR_CHECK(!ValidateRenderSubmitObjectHandles(duplicate, 3, 7));
	JR_CHECK(!ValidateRenderSubmitObjectHandles(valid,
		kRenderMaxClientBuffers + 1, 7));
}
