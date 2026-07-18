// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot
#ifndef INTEL_VALLEYVIEW_GPU_CORE_H
#define INTEL_VALLEYVIEW_GPU_CORE_H

#include <common/intel_valleyview/FirmwareState.h>

#include <stddef.h>


namespace valleyview {

constexpr uint32 kBcsRingBase = 0x22000;
constexpr uint32 kRingTail = kBcsRingBase + 0x30;
constexpr uint32 kRingHead = kBcsRingBase + 0x34;
constexpr uint32 kRingStart = kBcsRingBase + 0x38;
constexpr uint32 kRingControl = kBcsRingBase + 0x3c;
constexpr uint32 kRingActhd = kBcsRingBase + 0x74;
constexpr uint32 kRingHws = 0x4280;
constexpr uint32 kRingIpeir = kBcsRingBase + 0x64;
constexpr uint32 kRingIpehr = kBcsRingBase + 0x68;
constexpr uint32 kRingInstdone = kBcsRingBase + 0x6c;
constexpr uint32 kRingMiMode = kBcsRingBase + 0x9c;
constexpr uint32 kRingMode = kBcsRingBase + 0x29c;

constexpr uint32 kGfxFlushControl = 0x101008;
constexpr uint32 kGtFifoDebug = 0x120000;
constexpr uint32 kGtFifoControl = 0x120008;
constexpr uint32 kForcewakeRender = 0x1300b0;
constexpr uint32 kForcewakeAckRender = 0x1300b4;
constexpr uint32 kForcewakeMedia = 0x1300b8;
constexpr uint32 kForcewakeAckMedia = 0x1300bc;
constexpr uint32 kGtlcWakeControl = 0x130090;
constexpr uint32 kGtlcPowerStatus = 0x130094;
constexpr uint32 kGtThreadStatus = 0x13805c;
constexpr uint32 kRenderC0Count = 0x138118;
constexpr uint32 kMediaC0Count = 0x13811c;
constexpr uint32 kGen6Gdrst = 0x941c;

constexpr uint32 kForcewakeKernel = 1u << 0;
constexpr uint32 kGtlcAllowWakeRequest = 1u << 0;
constexpr uint32 kGtlcAllowWakeAck = 1u << 0;
constexpr uint32 kGtThreadStatusMask = 0x7;
constexpr uint32 kGtFifoFreeEntriesMask = 0x7f;
constexpr uint32 kGtFifoReservedEntries = 20;
constexpr uint32 kRingAddressMask = 0x001ffffc;
constexpr uint32 kRingValid = 1u << 0;
constexpr uint32 kRingStop = 1u << 8;
constexpr uint32 kRingPpgttEnable = 1u << 9;
constexpr uint32 kGfxFlushEnable = 1u << 0;
constexpr uint32 kGen6ResetBlt = 1u << 3;
constexpr uint32 kBytPteWriteable = 1u << 1;
constexpr uint32 kBytPteSnooped = 1u << 2;

constexpr uint32 kGpuTestPageCount = 4;
constexpr uint32 kGpuTestBytes = kGpuTestPageCount * kPageSize;
constexpr uint32 kGpuRingPage = 0;
constexpr uint32 kGpuSourcePage = 1;
constexpr uint32 kGpuDestinationPage = 2;
constexpr uint32 kGpuStatusPage = 3;
constexpr uint32 kGpuTestWidth = 16;
constexpr uint32 kGpuTestHeight = 16;
constexpr uint32 kGpuTestStride = kGpuTestWidth * sizeof(uint32);
constexpr uint32 kGpuTestPixels = kGpuTestWidth * kGpuTestHeight;
constexpr uint32 kGpuCompletionOffset = 0x100;
constexpr uint32 kGpuTestPattern = 0x5aa5c33c;
constexpr uint32 kGpuCompletionMarker = 0xc001b170;

constexpr size_t kBcsSelfTestCommandCount = 18;


inline bool
IsBcsRingQuiesced(const GpuRegisterSnapshot& snapshot)
{
	return (snapshot.bcsControl & kRingValid) == 0
		&& snapshot.bcsStart == 0
		&& snapshot.bcsHws == 0
		&& (snapshot.bcsHead & kRingAddressMask) == 0
		&& (snapshot.bcsTail & kRingAddressMask) == 0;
}


inline bool
EncodeBytPte(uint64 physical, bool writable, bool snooped, uint32& pte)
{
	if (physical >= 0x100000000ull || (physical & kPageMask) != 0)
		return false;

	pte = static_cast<uint32>(physical) | kGen7PtePresent;
	if (writable)
		pte |= kBytPteWriteable;
	if (snooped)
		pte |= kBytPteSnooped;
	return true;
}


inline bool
RangesOverlap(uint64 leftOffset, uint64 leftLength, uint64 rightOffset,
	uint64 rightLength)
{
	if (leftLength == 0 || rightLength == 0)
		return false;
	return leftOffset < rightOffset + rightLength
		&& rightOffset < leftOffset + leftLength;
}


inline bool
SelectGpuTestGgttOffset(uint64 gmadrSize, uint64 liveOffset,
	uint64 liveLength, uint32& offset)
{
	if (gmadrSize > UINT32_MAX || gmadrSize < kGpuTestBytes
		|| (gmadrSize & kPageMask) != 0) {
		return false;
	}

	const uint64 candidate = gmadrSize - kGpuTestBytes;
	if (RangesOverlap(candidate, kGpuTestBytes, liveOffset, liveLength))
		return false;

	offset = static_cast<uint32>(candidate);
	return true;
}


inline size_t
BuildBcsSelfTestCommands(uint32* commands, size_t capacity,
	uint32 sourceOffset, uint32 destinationOffset, uint32 statusOffset,
	uint32 pattern, uint32 completionMarker)
{
	if (commands == NULL || capacity < kBcsSelfTestCommandCount
		|| (sourceOffset & kPageMask) != 0
		|| (destinationOffset & kPageMask) != 0
		|| (statusOffset & kPageMask) != 0) {
		return 0;
	}

	size_t index = 0;
	commands[index++] = 0x54300004;
	commands[index++] = (3u << 24) | (0xf0u << 16) | kGpuTestStride;
	commands[index++] = 0;
	commands[index++] = (kGpuTestHeight << 16) | kGpuTestWidth;
	commands[index++] = sourceOffset;
	commands[index++] = pattern;

	commands[index++] = 0x54f00006;
	commands[index++] = (3u << 24) | (0xccu << 16) | kGpuTestStride;
	commands[index++] = 0;
	commands[index++] = (kGpuTestHeight << 16) | kGpuTestWidth;
	commands[index++] = destinationOffset;
	commands[index++] = 0;
	commands[index++] = kGpuTestStride;
	commands[index++] = sourceOffset;

	commands[index++] = 0x13204001;
	commands[index++] = kGpuCompletionOffset | (1u << 2);
	commands[index++] = completionMarker;
	commands[index++] = 0;
	return index;
}


inline bool
AppendBcsFill(uint32* commands, size_t capacity, size_t& count,
	uint32 destinationOffset, uint32 stride, uint32 color,
	uint16 left, uint16 top, uint16 right, uint16 bottom)
{
	if (commands == NULL || count > capacity || capacity - count < 6
		|| stride == 0 || left > right || top > bottom) {
		return false;
	}

	commands[count++] = 0x54300004;
	commands[count++] = (3u << 24) | (0xf0u << 16) | stride;
	commands[count++] = (static_cast<uint32>(top) << 16) | left;
	commands[count++] = (static_cast<uint32>(bottom + 1) << 16)
		| static_cast<uint16>(right + 1);
	commands[count++] = destinationOffset;
	commands[count++] = color;
	return true;
}


inline bool
AppendBcsCopy(uint32* commands, size_t capacity, size_t& count,
	uint32 framebufferOffset, uint32 stride, uint16 sourceLeft,
	uint16 sourceTop, uint16 destinationLeft, uint16 destinationTop,
	uint16 width, uint16 height)
{
	if (commands == NULL || count > capacity || capacity - count < 8
		|| stride == 0 || width == UINT16_MAX || height == UINT16_MAX) {
		return false;
	}

	commands[count++] = 0x54f00006;
	commands[count++] = (3u << 24) | (0xccu << 16) | stride;
	commands[count++] = (static_cast<uint32>(destinationTop) << 16)
		| destinationLeft;
	commands[count++] = (static_cast<uint32>(
			destinationTop + height + 1) << 16)
		| static_cast<uint16>(destinationLeft + width + 1);
	commands[count++] = framebufferOffset;
	commands[count++] = (static_cast<uint32>(sourceTop) << 16)
		| sourceLeft;
	commands[count++] = stride;
	commands[count++] = framebufferOffset;
	return true;
}


inline bool
AppendBcsCompletion(uint32* commands, size_t capacity, size_t& count,
	uint32 marker)
{
	if (commands == NULL || count > capacity || capacity - count < 4)
		return false;
	commands[count++] = 0x13204001;
	commands[count++] = kGpuCompletionOffset | (1u << 2);
	commands[count++] = marker;
	commands[count++] = 0;
	return true;
}


inline bool
FindWordMismatch(const uint32* words, size_t count, uint32 expected,
	uint32& byteOffset, uint32& observed)
{
	if (words == NULL)
		return false;

	for (size_t index = 0; index < count; index++) {
		if (words[index] != expected) {
			byteOffset = static_cast<uint32>(index * sizeof(uint32));
			observed = words[index];
			return true;
		}
	}
	return false;
}

} // namespace valleyview

#endif
