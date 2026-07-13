// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot
#pragma once

#include <cstddef>
#include <cstdint>


namespace jr::sdhci {

struct StagedRequestChunk {
	uint64_t	mediaOffset = 0;
	size_t		bufferOffset = 0;
	size_t		requestBytes = 0;
	size_t		mediaBytes = 0;

	bool IsValid() const
	{
		return requestBytes != 0 && mediaBytes != 0;
	}

	bool NeedsReadBeforeWrite() const
	{
		return bufferOffset != 0 || requestBytes != mediaBytes;
	}
};


inline StagedRequestChunk
PlanStagedRequestChunk(uint64_t requestOffset, size_t remainingBytes,
	size_t blockSize, size_t stagingSize)
{
	StagedRequestChunk chunk;
	if (remainingBytes == 0 || blockSize == 0 || stagingSize < blockSize
		|| stagingSize % blockSize != 0) {
		return chunk;
	}

	chunk.mediaOffset = requestOffset - requestOffset % blockSize;
	chunk.bufferOffset = static_cast<size_t>(requestOffset - chunk.mediaOffset);
	chunk.requestBytes = remainingBytes;
	const size_t capacity = stagingSize - chunk.bufferOffset;
	if (chunk.requestBytes > capacity)
		chunk.requestBytes = capacity;

	const size_t occupied = chunk.bufferOffset + chunk.requestBytes;
	chunk.mediaBytes = (occupied + blockSize - 1) / blockSize * blockSize;
	if (chunk.mediaBytes > stagingSize)
		return {};
	return chunk;
}

} // namespace jr::sdhci
