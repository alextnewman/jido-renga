// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot
#ifndef INTEL_VALLEYVIEW_RENDER_QUEUE_CORE_H
#define INTEL_VALLEYVIEW_RENDER_QUEUE_CORE_H

#include <common/intel_valleyview/P0Core.h>
#include <common/intel_valleyview/RenderMemoryCore.h>


namespace valleyview {

struct RenderClientQueueState {
	uint64	nextFence;
	uint64	lastStartedFence;
	uint64	lastRetiredFence;
	uint32	queuedJobs;
	bool	running;
};


inline RenderClientQueueState
InitializeRenderClientQueueState()
{
	RenderClientQueueState state = {};
	state.nextFence = 1;
	return state;
}


inline bool
ValidateRenderObjectAccess(uint32 access)
{
	return (access & ~kRenderObjectAccessMask) == 0
		&& (access & (kRenderObjectRead | kRenderObjectWrite)) != 0
		&& ((access & kRenderObjectExecute) == 0
			|| (access & kRenderObjectRead) != 0);
}


inline bool
ValidateRenderObjectReferences(const RenderObjectReference* references,
	uint32 count, uint32 batchHandle)
{
	if (references == NULL || count == 0 || count > kRenderSubmitMaxObjects
		|| batchHandle == 0) {
		return false;
	}

	uint32 executableCount = 0;
	bool foundBatch = false;
	for (uint32 index = 0; index < count; index++) {
		const RenderObjectReference& reference = references[index];
		if (reference.handle == 0
			|| !ValidateRenderObjectAccess(reference.access)) {
			return false;
		}
		const bool executable
			= (reference.access & kRenderObjectExecute) != 0;
		if (executable) {
			executableCount++;
			if (reference.handle != batchHandle
				|| (reference.access & kRenderObjectWrite) != 0) {
				return false;
			}
			foundBatch = true;
		} else if (reference.handle == batchHandle) {
			return false;
		}
		for (uint32 previous = 0; previous < index; previous++) {
			if (references[previous].handle == reference.handle)
				return false;
		}
	}
	return foundBatch && executableCount == 1;
}


inline bool
CanQueueRenderJob(const RenderClientQueueState& state,
	uint32 deviceQueuedJobs)
{
	return state.nextFence != kInvalidRenderFence
		&& state.nextFence != UINT64_MAX
		&& state.queuedJobs < kRenderMaxQueuedJobsPerClient
		&& deviceQueuedJobs < kRenderMaxQueuedJobsPerDevice;
}


inline bool
QueueRenderJob(RenderClientQueueState& state, uint32 deviceQueuedJobs,
	uint64& fence)
{
	fence = kInvalidRenderFence;
	if (!CanQueueRenderJob(state, deviceQueuedJobs))
		return false;

	fence = state.nextFence++;
	state.queuedJobs++;
	return true;
}


inline bool
StartRenderJob(RenderClientQueueState& state, uint64 fence)
{
	if (fence == kInvalidRenderFence || state.running || state.queuedJobs == 0
		|| fence != state.lastRetiredFence + 1) {
		return false;
	}
	state.queuedJobs--;
	state.running = true;
	state.lastStartedFence = fence;
	return true;
}


inline bool
FinishRunningRenderJob(RenderClientQueueState& state, uint64 fence)
{
	if (!state.running || fence == kInvalidRenderFence
		|| fence != state.lastStartedFence
		|| fence != state.lastRetiredFence + 1) {
		return false;
	}
	state.running = false;
	state.lastRetiredFence = fence;
	return true;
}


inline bool
CompleteRenderJob(RenderClientQueueState& state, uint64 fence)
{
	return FinishRunningRenderJob(state, fence);
}


inline bool
FailRenderJob(RenderClientQueueState& state, uint64 fence)
{
	return FinishRunningRenderJob(state, fence);
}


inline bool
CancelQueuedRenderJob(RenderClientQueueState& state, uint64 fence)
{
	if (state.running || state.queuedJobs == 0
		|| fence == kInvalidRenderFence
		|| fence != state.lastRetiredFence + 1) {
		return false;
	}
	state.queuedJobs--;
	state.lastRetiredFence = fence;
	return true;
}


inline bool
RenderFenceIsRetired(const RenderClientQueueState& state, uint64 fence)
{
	return fence != kInvalidRenderFence && fence <= state.lastRetiredFence;
}

inline bool
ShouldDropQueuedPresentation(uint64 presentFence, uint64 presentStream,
	uint64 newestFence, uint64 newestStream)
{
	return presentFence != kInvalidRenderFence
		&& newestFence != kInvalidRenderFence
		&& presentStream != 0 && presentStream == newestStream
		&& presentFence < newestFence;
}

inline uint64
RenderDirectPresentSourceBytes(const RenderDirectPresent& request)
{
	if (request.sourceWidth == 0 || request.sourceHeight == 0)
		return 0;
	return static_cast<uint64>(request.sourceStride)
			* (request.sourceHeight - 1)
		+ static_cast<uint64>(request.sourceWidth) * sizeof(uint32);
}


inline bool
ValidateRenderDirectPresentGeometry(const RenderDirectPresent& request)
{
	if (request.reserved != 0 || request.sourceHandle == 0
		|| request.sourceWidth == 0 || request.sourceHeight == 0
		|| request.sourceWidth > UINT16_MAX
		|| request.sourceHeight > UINT16_MAX
		|| static_cast<uint64>(request.sourceStride)
			< static_cast<uint64>(request.sourceWidth) * sizeof(uint32)
		|| request.sourceStride > UINT16_MAX
		|| (request.sourceOffset & (sizeof(uint32) - 1)) != 0
		|| request.rectCount == 0
		|| request.rectCount > kRenderMaxPresentRects) {
		return false;
	}
	for (uint32 index = 0; index < request.rectCount; index++) {
		const RenderPresentRect& rect = request.rects[index];
		if (static_cast<uint32>(rect.sourceLeft) + rect.width
				>= request.sourceWidth
			|| static_cast<uint32>(rect.sourceTop) + rect.height
				>= request.sourceHeight
			|| static_cast<uint32>(rect.destinationLeft) + rect.width
				>= kP0Width
			|| static_cast<uint32>(rect.destinationTop) + rect.height
				>= kP0Height) {
			return false;
		}
	}
	return true;
}


inline bool
SelectNextReadyRenderClient(const bool* ready, uint32 clientCount,
	uint32& nextHint, uint32& selected)
{
	if (ready == NULL || clientCount == 0)
		return false;

	nextHint %= clientCount;
	for (uint32 offset = 0; offset < clientCount; offset++) {
		const uint32 index = (nextHint + offset) % clientCount;
		if (!ready[index])
			continue;
		selected = index;
		nextHint = (index + 1) % clientCount;
		return true;
	}
	return false;
}


} // namespace valleyview

#endif
