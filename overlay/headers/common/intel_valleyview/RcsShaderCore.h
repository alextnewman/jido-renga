// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot
#ifndef INTEL_VALLEYVIEW_RCS_SHADER_CORE_H
#define INTEL_VALLEYVIEW_RCS_SHADER_CORE_H

#include <common/intel_valleyview/GpuCore.h>
#include <common/intel_valleyview/IvbClearKernel.h>

#include <stddef.h>
#include <string.h>


namespace valleyview {

constexpr uint32 kMiNoop = 0;
constexpr uint32 kMiBatchBufferEnd = 0x05000000;
constexpr uint32 kMiBatchBufferStart = 0x18800000;
constexpr uint32 kMiBatchNonSecureI965 = 1u << 8;
constexpr uint32 kMiStoreDwordImmGen4 = 0x10000002;
constexpr uint32 kMiStoreRegisterMem = 0x12000001;
constexpr uint32 kMiUseGgtt = 1u << 22;
constexpr uint32 kGen7PipeControl = 0x7a000002;
constexpr uint32 kPipeControlDepthCacheFlush = 1u << 0;
constexpr uint32 kPipeControlDcFlush = 1u << 5;
constexpr uint32 kPipeControlFlushEnable = 1u << 7;
constexpr uint32 kPipeControlRenderTargetFlush = 1u << 12;
constexpr uint32 kPipeControlQwordWrite = 1u << 14;
constexpr uint32 kPipeControlCsStall = 1u << 20;
constexpr uint32 kPipeControlGlobalGttIvb = 1u << 24;

constexpr uint32 kRcsShaderMaxThreads = 36;
constexpr uint32 kRcsShaderCommandRegionBytes = kPageSize;
constexpr uint32 kRcsShaderStateStart = kPageSize;
constexpr uint32 kRcsShaderStateBytes = kPageSize;
constexpr uint32 kRcsShaderSurfaceStart = 2 * kPageSize;
constexpr uint32 kRcsShaderSurfaceWidth = 512;
constexpr uint32 kRcsShaderSurfaceHeight = 128;
constexpr uint32 kRcsShaderSurfaceBytes
	= kRcsShaderSurfaceWidth * kRcsShaderSurfaceHeight;
constexpr uint32 kRcsShaderObjectBytes
	= kRcsShaderSurfaceStart + kRcsShaderSurfaceBytes;
constexpr uint32 kRcsShaderObjectPages
	= kRcsShaderObjectBytes / kPageSize;
constexpr uint32 kRcsShaderGuardBytes = kPageSize;
constexpr uint32 kRcsShaderTotalBytes
	= kRcsShaderObjectBytes + kRcsShaderGuardBytes;
constexpr uint32 kRcsShaderTotalPages = kRcsShaderTotalBytes / kPageSize;
constexpr uint32 kRcsShaderSentinel = 0x6d5aa5c3;
constexpr uint32 kRcsShaderExpectedOutputRows = 32;
constexpr uint32 kRcsShaderOutputCellBytes = 64;
constexpr uint32 kRcsShaderOutputBytesPerCell = 32;
constexpr uint32 kRcsShaderExpectedZeroDwords
	= kRcsShaderExpectedOutputRows
		* (kRcsShaderSurfaceWidth / kRcsShaderOutputCellBytes)
		* (kRcsShaderOutputBytesPerCell / sizeof(uint32));
constexpr uint32 kRcsShaderExpectedSentinelDwords
	= kRcsShaderSurfaceBytes / sizeof(uint32)
		- kRcsShaderExpectedZeroDwords;

constexpr uint32 kGen7CacheMode0 = 0x7000;
constexpr uint32 kGen7CacheMode1 = 0x7004;
constexpr uint32 kGen7HizRawStallDisable = 1u << 2;
constexpr uint32 kGen7PixelSubspanCollectDisable = 1u << 6;

constexpr uint32 kStateBaseAddress = 0x61010000;
constexpr uint32 kBaseAddressModify = 1u << 0;
constexpr uint32 kPipelineSelectMedia = 0x69040001;
constexpr uint32 kMediaVfeState = 0x70000000;
constexpr uint32 kMediaInterfaceDescriptorLoad = 0x70020000;
constexpr uint32 kMediaObject = 0x71000000;
constexpr uint32 kMiLoadRegisterImm2 = 0x11000003;
constexpr uint32 kPipeControlStallScoreboard = 1u << 1;
constexpr uint32 kPipeControlStateInvalidate = 1u << 2;
constexpr uint32 kRcsShaderPipelineFlushFlags
	= kPipeControlRenderTargetFlush
		| kPipeControlDepthCacheFlush
		| kPipeControlDcFlush
		| kPipeControlCsStall;
constexpr uint32 kRcsShaderPipelineStallFlags
	= kPipeControlCsStall | kPipeControlStallScoreboard;


struct RcsShaderLayout {
	uint32	commandBytes;
	uint32	kernelOffset;
	uint32	surfaceStateOffset;
	uint32	bindingTableOffset;
	uint32	interfaceDescriptorOffset;
	uint32	surfaceOffset;
	uint32	surfaceBytes;
	uint32	guardOffset;
	uint32	guardBytes;
};


struct RcsShaderAnalysis {
	uint32	zeroDwords;
	uint32	sentinelDwords;
	uint32	unexpectedDwords;
	uint32	firstChangedOffset;
	uint32	lastChangedOffset;
	uint32	firstUnexpectedOffset;
	uint32	firstUnexpectedValue;
	uint32	guardMismatchOffset;
	uint32	guardObserved;
	uint64	checksum;
};


struct RcsShaderWriter {
	uint8*	base;
	uint32	offset;
	uint32	limit;
	bool	valid;
};


inline uint32
AlignRcsShaderOffset(uint32 offset, uint32 alignment)
{
	return (offset + alignment - 1) & ~(alignment - 1);
}


inline void
InitializeRcsShaderWriter(RcsShaderWriter& writer, void* base, uint32 start,
	uint32 limit)
{
	writer.base = static_cast<uint8*>(base);
	writer.offset = start;
	writer.limit = limit;
	writer.valid = base != NULL && start <= limit;
}


inline uint32*
AllocateRcsShaderWords(RcsShaderWriter& writer, uint32 alignment,
	uint32 count, uint32& offset)
{
	if (!writer.valid || alignment == 0
		|| (alignment & (alignment - 1)) != 0
		|| count > UINT32_MAX / sizeof(uint32)) {
		writer.valid = false;
		return NULL;
	}

	const uint32 aligned = AlignRcsShaderOffset(writer.offset, alignment);
	const uint32 bytes = count * sizeof(uint32);
	if (aligned > writer.limit || bytes > writer.limit - aligned) {
		writer.valid = false;
		return NULL;
	}

	if (aligned > writer.offset) {
		memset(writer.base + writer.offset, 0, aligned - writer.offset);
	}
	offset = aligned;
	writer.offset = aligned + bytes;
	return reinterpret_cast<uint32*>(writer.base + aligned);
}


inline bool
AddRcsShaderWord(RcsShaderWriter& writer, uint32 value)
{
	uint32 offset;
	uint32* word = AllocateRcsShaderWords(writer, sizeof(uint32), 1, offset);
	if (word == NULL)
		return false;
	*word = value;
	return true;
}


inline bool
EmitRcsShaderPipelineFlush(RcsShaderWriter& commands)
{
	const uint32 values[] = {
		kGen7PipeControl,
		kRcsShaderPipelineFlushFlags,
		0,
		0
	};
	for (size_t index = 0; index < sizeof(values) / sizeof(values[0]); index++) {
		if (!AddRcsShaderWord(commands, values[index]))
			return false;
	}
	return true;
}


inline bool
EmitRcsShaderPipelineInvalidate(RcsShaderWriter& commands)
{
	const uint32 values[] = {
		kGen7PipeControl + 1,
		kRcsShaderPipelineStallFlags,
		0, 0, 0,
		kGen7PipeControl + 1,
		kPipeControlStateInvalidate,
		0, 0, 0
	};
	for (size_t index = 0; index < sizeof(values) / sizeof(values[0]); index++) {
		if (!AddRcsShaderWord(commands, values[index]))
			return false;
	}
	return true;
}


inline bool
EmitRcsShaderStateBaseAddress(RcsShaderWriter& commands, uint32 ggttBase,
	uint32 surfaceStateBase)
{
	const uint32 values[] = {
		kStateBaseAddress | 8,
		ggttBase | kBaseAddressModify,
		(ggttBase + surfaceStateBase) | kBaseAddressModify,
		ggttBase | kBaseAddressModify,
		ggttBase | kBaseAddressModify,
		ggttBase | kBaseAddressModify,
		0,
		kBaseAddressModify,
		0,
		kBaseAddressModify
	};
	for (size_t index = 0; index < sizeof(values) / sizeof(values[0]); index++) {
		if (!AddRcsShaderWord(commands, values[index]))
			return false;
	}
	return true;
}


inline bool
EmitRcsShaderVfeState(RcsShaderWriter& commands)
{
	uint32 offset;
	uint32* values = AllocateRcsShaderWords(commands, 32, 8, offset);
	if (values == NULL)
		return false;
	values[0] = kMediaVfeState | 6;
	values[1] = 0;
	values[2] = ((kRcsShaderMaxThreads - 1) << 16) | (1u << 8);
	values[3] = 0;
	values[4] = 0;
	values[5] = 0;
	values[6] = 0;
	values[7] = 0;
	return true;
}


inline bool
EmitRcsShaderInterfaceDescriptorLoad(RcsShaderWriter& commands,
	uint32 descriptorOffset)
{
	uint32 offset;
	uint32* values = AllocateRcsShaderWords(commands, 8, 4, offset);
	if (values == NULL)
		return false;
	values[0] = kMediaInterfaceDescriptorLoad | 2;
	values[1] = 0;
	values[2] = 8 * sizeof(uint32);
	values[3] = descriptorOffset;
	return true;
}


inline bool
EmitRcsShaderMediaObject(RcsShaderWriter& commands, uint32 index)
{
	uint32 offset;
	uint32* values = AllocateRcsShaderWords(commands, 8, 9, offset);
	if (values == NULL)
		return false;
	const uint32 x = (index % 16) * 64;
	const uint32 y = (index / 16) * 16;
	values[0] = kMediaObject | 7;
	values[1] = 0;
	values[2] = 0;
	values[3] = 0;
	values[4] = 0;
	values[5] = 0;
	values[6] = (y << 16) | x;
	values[7] = 0;
	values[8] = 0x1e00;
	return true;
}


inline bool
BuildRcsShaderBatch(void* memory, uint32 size, uint32 ggttBase,
	RcsShaderLayout& layout)
{
	memset(&layout, 0, sizeof(layout));
	if (memory == NULL || size < kRcsShaderTotalBytes
		|| (ggttBase & kPageMask) != 0
		|| ggttBase > UINT32_MAX - kRcsShaderObjectBytes) {
		return false;
	}

	memset(memory, 0, kRcsShaderSurfaceStart);
	RcsShaderWriter state;
	InitializeRcsShaderWriter(state, memory, kRcsShaderStateStart,
		kRcsShaderSurfaceStart);

	uint32 offset;
	uint32* kernel = AllocateRcsShaderWords(state, 64,
		sizeof(kIvbClearKernel) / sizeof(kIvbClearKernel[0]), offset);
	if (kernel == NULL)
		return false;
	layout.kernelOffset = offset;
	memcpy(kernel, kIvbClearKernel, sizeof(kIvbClearKernel));

	uint32* surface = AllocateRcsShaderWords(state, 32, 8, offset);
	if (surface == NULL)
		return false;
	layout.surfaceStateOffset = offset;
	surface[0] = (1u << 29) | (0x0c0u << 18) | (1u << 8);
	surface[1] = ggttBase + kRcsShaderSurfaceStart;
	surface[2] = ((kRcsShaderSurfaceHeight / 4 - 1) << 16)
		| (kRcsShaderSurfaceWidth / 4 - 1);
	surface[3] = kRcsShaderSurfaceWidth;
	surface[4] = 0;
	surface[5] = 0;
	surface[6] = 0;
	surface[7] = (4u << 25) | (5u << 22) | (6u << 19) | (7u << 16);

	uint32* binding = AllocateRcsShaderWords(state, 32, 8, offset);
	if (binding == NULL)
		return false;
	layout.bindingTableOffset = offset;
	memset(binding, 0, 8 * sizeof(uint32));
	binding[0] = layout.surfaceStateOffset - kRcsShaderStateStart;

	uint32* descriptor = AllocateRcsShaderWords(state, 32, 8, offset);
	if (descriptor == NULL)
		return false;
	layout.interfaceDescriptorOffset = offset;
	memset(descriptor, 0, 8 * sizeof(uint32));
	descriptor[0] = layout.kernelOffset;
	descriptor[1] = (1u << 7) | (1u << 13);
	descriptor[3]
		= (layout.bindingTableOffset - kRcsShaderStateStart) | 1u;

	RcsShaderWriter commands;
	InitializeRcsShaderWriter(commands, memory, 0,
		kRcsShaderCommandRegionBytes);
	if (!EmitRcsShaderPipelineFlush(commands)
		|| !EmitRcsShaderPipelineInvalidate(commands)
		|| !AddRcsShaderWord(commands, kMiLoadRegisterImm2)
		|| !AddRcsShaderWord(commands, kGen7CacheMode0)
		|| !AddRcsShaderWord(commands,
			0xffff0000u | kGen7HizRawStallDisable)
		|| !AddRcsShaderWord(commands, kGen7CacheMode1)
		|| !AddRcsShaderWord(commands,
			0xffff0000u | kGen7PixelSubspanCollectDisable)
		|| !EmitRcsShaderPipelineInvalidate(commands)
		|| !EmitRcsShaderPipelineFlush(commands)
		|| !EmitRcsShaderPipelineInvalidate(commands)
		|| !AddRcsShaderWord(commands, kPipelineSelectMedia)
		|| !AddRcsShaderWord(commands, kMiNoop)
		|| !EmitRcsShaderPipelineInvalidate(commands)
		|| !EmitRcsShaderPipelineFlush(commands)
		|| !EmitRcsShaderStateBaseAddress(commands, ggttBase,
			layout.interfaceDescriptorOffset)
		|| !EmitRcsShaderPipelineInvalidate(commands)
		|| !EmitRcsShaderVfeState(commands)
		|| !EmitRcsShaderInterfaceDescriptorLoad(commands,
			layout.interfaceDescriptorOffset)) {
		return false;
	}

	for (uint32 index = 0; index < kRcsShaderMaxThreads; index++) {
		if (!EmitRcsShaderMediaObject(commands, index))
			return false;
	}
	if (!AddRcsShaderWord(commands, kMiBatchBufferEnd))
		return false;

	layout.commandBytes = commands.offset;
	layout.surfaceOffset = kRcsShaderSurfaceStart;
	layout.surfaceBytes = kRcsShaderSurfaceBytes;
	layout.guardOffset = kRcsShaderObjectBytes;
	layout.guardBytes = kRcsShaderGuardBytes;
	return commands.valid && state.valid;
}


inline uint64
RcsShaderChecksum(const uint32* words, uint32 count)
{
	uint64 checksum = 1469598103934665603ull;
	for (uint32 index = 0; index < count; index++) {
		uint32 value = words[index];
		for (uint32 byte = 0; byte < sizeof(value); byte++) {
			checksum ^= (value >> (byte * 8)) & 0xff;
			checksum *= 1099511628211ull;
		}
	}
	return checksum;
}


inline bool
IsExpectedRcsShaderZeroDword(uint32 index)
{
	if (index >= kRcsShaderSurfaceBytes / sizeof(uint32))
		return false;

	const uint32 byteOffset = index * sizeof(uint32);
	const uint32 row = byteOffset / kRcsShaderSurfaceWidth;
	const uint32 x = byteOffset % kRcsShaderSurfaceWidth;
	return row < kRcsShaderExpectedOutputRows
		&& x % kRcsShaderOutputCellBytes < kRcsShaderOutputBytesPerCell;
}


inline bool
AnalyzeRcsShaderOutput(const uint32* surface, uint32 surfaceBytes,
	const uint32* guard, uint32 guardBytes, uint32 sentinel,
	RcsShaderAnalysis& analysis)
{
	memset(&analysis, 0, sizeof(analysis));
	analysis.firstChangedOffset = UINT32_MAX;
	analysis.lastChangedOffset = UINT32_MAX;
	analysis.firstUnexpectedOffset = UINT32_MAX;
	analysis.guardMismatchOffset = UINT32_MAX;
	if (surface == NULL || guard == NULL
		|| surfaceBytes != kRcsShaderSurfaceBytes
		|| guardBytes != kRcsShaderGuardBytes) {
		return false;
	}

	const uint32 surfaceWords = surfaceBytes / sizeof(uint32);
	for (uint32 index = 0; index < surfaceWords; index++) {
		const uint32 value = surface[index];
		const bool expectedZero = IsExpectedRcsShaderZeroDword(index);
		if (value != sentinel) {
			if (analysis.firstChangedOffset == UINT32_MAX)
				analysis.firstChangedOffset = index * sizeof(uint32);
			analysis.lastChangedOffset = index * sizeof(uint32);
		}
		if (value == 0)
			analysis.zeroDwords++;
		else if (value == sentinel)
			analysis.sentinelDwords++;

		if ((expectedZero && value != 0)
			|| (!expectedZero && value != sentinel)) {
			analysis.unexpectedDwords++;
			if (analysis.firstUnexpectedOffset == UINT32_MAX) {
				analysis.firstUnexpectedOffset = index * sizeof(uint32);
				analysis.firstUnexpectedValue = value;
			}
		}
	}
	analysis.checksum = RcsShaderChecksum(surface, surfaceWords);

	const uint32 guardWords = guardBytes / sizeof(uint32);
	for (uint32 index = 0; index < guardWords; index++) {
		if (guard[index] != sentinel) {
			analysis.guardMismatchOffset = index * sizeof(uint32);
			analysis.guardObserved = guard[index];
			break;
		}
	}

	return analysis.zeroDwords == kRcsShaderExpectedZeroDwords
		&& analysis.sentinelDwords == kRcsShaderExpectedSentinelDwords
		&& analysis.unexpectedDwords == 0
		&& analysis.guardMismatchOffset == UINT32_MAX;
}

} // namespace valleyview

#endif
