// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot
#ifndef INTEL_VALLEYVIEW_RENDER_SUBMIT_CORE_H
#define INTEL_VALLEYVIEW_RENDER_SUBMIT_CORE_H

#include <common/intel_valleyview/RenderCommandCore.h>
#include <common/intel_valleyview/RenderMemoryCore.h>


namespace valleyview {

inline bool
ValidateRenderSubmitBatchRange(uint64 bufferSize, uint32 batchOffset,
	uint32 batchLength)
{
	return batchLength != 0
		&& batchLength <= kRenderCommandMaxBatchBytes
		&& (batchOffset & (sizeof(uint32) - 1)) == 0
		&& (batchLength & (sizeof(uint32) - 1)) == 0
		&& batchOffset <= bufferSize
		&& batchLength <= bufferSize - batchOffset;
}


inline bool
ValidateRenderSubmitObjectHandles(const uint32* handles, uint32 count,
	uint32 batchHandle)
{
	if (handles == NULL || count == 0 || count > kRenderMaxClientBuffers
		|| batchHandle == 0) {
		return false;
	}

	bool foundBatch = false;
	for (uint32 index = 0; index < count; index++) {
		if (handles[index] == 0)
			return false;
		if (handles[index] == batchHandle)
			foundBatch = true;
		for (uint32 previous = 0; previous < index; previous++) {
			if (handles[previous] == handles[index])
				return false;
		}
	}
	return foundBatch;
}


} // namespace valleyview

#endif
