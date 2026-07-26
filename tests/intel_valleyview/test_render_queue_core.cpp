// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot

#include "framework/jr_test.h"

#include <common/intel_valleyview/RenderQueueCore.h>


using namespace valleyview;


JR_TEST(intel_valleyview_render_queue, allocates_and_retires_ordered_fences)
{
	RenderClientQueueState state = InitializeRenderClientQueueState();
	uint64 first;
	uint64 second;
	JR_CHECK(QueueRenderJob(state, 0, first));
	JR_CHECK(QueueRenderJob(state, 1, second));
	JR_CHECK_EQ(first, 1u);
	JR_CHECK_EQ(second, 2u);
	JR_CHECK_EQ(state.queuedJobs, 2u);
	JR_CHECK(!RenderFenceIsRetired(state, first));

	JR_CHECK(!StartRenderJob(state, second));
	JR_CHECK(StartRenderJob(state, first));
	JR_CHECK(!StartRenderJob(state, second));
	JR_CHECK(!CompleteRenderJob(state, second));
	JR_CHECK(CompleteRenderJob(state, first));
	JR_CHECK(RenderFenceIsRetired(state, first));

	JR_CHECK(StartRenderJob(state, second));
	JR_CHECK(CompleteRenderJob(state, second));
	JR_CHECK(RenderFenceIsRetired(state, first));
	JR_CHECK(RenderFenceIsRetired(state, second));
}


JR_TEST(intel_valleyview_render_queue, bounds_queues_and_refuses_fence_wrap)
{
	RenderClientQueueState state = InitializeRenderClientQueueState();
	uint64 fence;
	for (uint32 index = 0; index < kRenderMaxQueuedJobsPerClient; index++)
		JR_CHECK(QueueRenderJob(state, index, fence));
	JR_CHECK(!QueueRenderJob(state, state.queuedJobs, fence));
	JR_CHECK_EQ(fence, kInvalidRenderFence);

	state = InitializeRenderClientQueueState();
	JR_CHECK(!QueueRenderJob(state, kRenderMaxQueuedJobsPerDevice, fence));
	state.nextFence = UINT64_MAX - 1;
	JR_CHECK(QueueRenderJob(state, 0, fence));
	JR_CHECK_EQ(fence, UINT64_MAX - 1);
	JR_CHECK(!QueueRenderJob(state, 1, fence));
	state.nextFence = UINT64_MAX;
	JR_CHECK(!QueueRenderJob(state, 0, fence));
}


JR_TEST(intel_valleyview_render_queue, validates_explicit_object_access)
{
	const RenderObjectReference valid[] = {
		{7, kRenderObjectRead | kRenderObjectExecute},
		{11, kRenderObjectRead},
		{19, kRenderObjectRead | kRenderObjectWrite}
	};
	JR_CHECK(ValidateRenderObjectReferences(valid, 3, 7));

	const RenderObjectReference writableBatch[] = {
		{7, kRenderObjectRead | kRenderObjectWrite | kRenderObjectExecute}
	};
	const RenderObjectReference secondExecutable[] = {
		{7, kRenderObjectRead | kRenderObjectExecute},
		{11, kRenderObjectRead | kRenderObjectExecute}
	};
	const RenderObjectReference duplicate[] = {
		{7, kRenderObjectRead | kRenderObjectExecute},
		{7, kRenderObjectRead}
	};
	const RenderObjectReference unknownAccess[] = {
		{7, kRenderObjectRead | kRenderObjectExecute | (1u << 12)}
	};
	JR_CHECK(!ValidateRenderObjectReferences(writableBatch, 1, 7));
	JR_CHECK(!ValidateRenderObjectReferences(secondExecutable, 2, 7));
	JR_CHECK(!ValidateRenderObjectReferences(duplicate, 2, 7));
	JR_CHECK(!ValidateRenderObjectReferences(unknownAccess, 1, 7));
}


JR_TEST(intel_valleyview_render_queue, schedules_ready_clients_round_robin)
{
	const bool ready[] = {true, true, false, true};
	uint32 nextHint = 0;
	uint32 selected = UINT32_MAX;
	JR_CHECK(SelectNextReadyRenderClient(ready, 4, nextHint, selected));
	JR_CHECK_EQ(selected, 0u);
	JR_CHECK(SelectNextReadyRenderClient(ready, 4, nextHint, selected));
	JR_CHECK_EQ(selected, 1u);
	JR_CHECK(SelectNextReadyRenderClient(ready, 4, nextHint, selected));
	JR_CHECK_EQ(selected, 3u);
	JR_CHECK(SelectNextReadyRenderClient(ready, 4, nextHint, selected));
	JR_CHECK_EQ(selected, 0u);

	const bool blocked[] = {false, false};
	JR_CHECK(!SelectNextReadyRenderClient(blocked, 2, nextHint, selected));
}


JR_TEST(intel_valleyview_render_queue, fails_and_cancels_fences_in_order)
{
	RenderClientQueueState state = InitializeRenderClientQueueState();
	uint64 fences[3];
	for (uint32 index = 0; index < 3; index++)
		JR_CHECK(QueueRenderJob(state, index, fences[index]));

	JR_CHECK(StartRenderJob(state, fences[0]));
	JR_CHECK(!CancelQueuedRenderJob(state, fences[1]));
	JR_CHECK(FailRenderJob(state, fences[0]));
	JR_CHECK(RenderFenceIsRetired(state, fences[0]));

	JR_CHECK(!CancelQueuedRenderJob(state, fences[2]));
	JR_CHECK(CancelQueuedRenderJob(state, fences[1]));
	JR_CHECK(CancelQueuedRenderJob(state, fences[2]));
	JR_CHECK_EQ(state.queuedJobs, 0u);
	JR_CHECK(RenderFenceIsRetired(state, fences[2]));

	uint64 nextFence;
	JR_CHECK(QueueRenderJob(state, 0, nextFence));
	JR_CHECK_EQ(nextFence, 4u);
	JR_CHECK(StartRenderJob(state, nextFence));
	JR_CHECK(CompleteRenderJob(state, nextFence));
}
