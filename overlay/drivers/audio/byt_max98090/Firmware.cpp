// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot

#include "Firmware.h"

#include <string.h>


namespace jr::byt_audio {

namespace {

constexpr size_t kFileHeaderSize = 32;
constexpr size_t kModuleHeaderSize = 20;
constexpr size_t kBlockHeaderSize = 16;


uint32_t
Read32(const uint8_t* data)
{
	return static_cast<uint32_t>(data[0])
		| static_cast<uint32_t>(data[1]) << 8
		| static_cast<uint32_t>(data[2]) << 16
		| static_cast<uint32_t>(data[3]) << 24;
}


size_t
MemorySize(FirmwareMemory memory, const FirmwareLimits& limits)
{
	switch (memory) {
		case FirmwareMemory::kIram:
			return limits.iramSize;
		case FirmwareMemory::kDram:
			return limits.dramSize;
		case FirmwareMemory::kDdr:
			return limits.ddrSize;
		case FirmwareMemory::kCustom:
			return SIZE_MAX;
	}
	return 0;
}

} // namespace


bool
CheckedRange(size_t offset, size_t length, size_t total)
{
	return offset <= total && length <= total - offset;
}


FirmwareStatus
ParseFirmware(const uint8_t* data, size_t size, const FirmwareLimits& limits,
	FirmwareBlockVisitor visitor, void* context)
{
	if (data == nullptr)
		return FirmwareStatus::kNullData;
	if (size < kFileHeaderSize)
		return FirmwareStatus::kTooSmall;
	if (memcmp(data, "$SST", 4) != 0)
		return FirmwareStatus::kBadSignature;

	const uint32_t fileSize = Read32(data + 4);
	const uint32_t moduleCount = Read32(data + 8);
	if (fileSize != size - kFileHeaderSize)
		return FirmwareStatus::kSizeMismatch;

	size_t moduleOffset = kFileHeaderSize;
	for (uint32_t moduleIndex = 0; moduleIndex < moduleCount; moduleIndex++) {
		if (!CheckedRange(moduleOffset, kModuleHeaderSize, size))
			return FirmwareStatus::kTruncated;

		const uint8_t* module = data + moduleOffset;
		const uint32_t moduleSize = Read32(module + 4);
		const uint32_t blockCount = Read32(module + 8);
		const size_t payloadOffset = moduleOffset + kModuleHeaderSize;
		if (!CheckedRange(payloadOffset, moduleSize, size))
			return FirmwareStatus::kTruncated;
		const size_t moduleEnd = payloadOffset + moduleSize;

		size_t blockOffset = payloadOffset;
		for (uint32_t blockIndex = 0; blockIndex < blockCount; blockIndex++) {
			if (!CheckedRange(blockOffset, kBlockHeaderSize, moduleEnd))
				return FirmwareStatus::kTruncated;

			const uint8_t* block = data + blockOffset;
			const uint32_t rawType = Read32(block);
			const uint32_t blockSize = Read32(block + 4);
			const uint32_t destinationOffset = Read32(block + 8);
			const FirmwareMemory memory = static_cast<FirmwareMemory>(rawType);
			if (blockSize == 0)
				return FirmwareStatus::kBadBlockSize;
			if (memory != FirmwareMemory::kIram
				&& memory != FirmwareMemory::kDram
				&& memory != FirmwareMemory::kDdr
				&& memory != FirmwareMemory::kCustom) {
				return FirmwareStatus::kBadBlockType;
			}

			const size_t dataOffset = blockOffset + kBlockHeaderSize;
			if (!CheckedRange(dataOffset, blockSize, moduleEnd))
				return FirmwareStatus::kTruncated;
			if (memory != FirmwareMemory::kCustom
				&& !CheckedRange(destinationOffset, blockSize,
					MemorySize(memory, limits))) {
				return FirmwareStatus::kDestinationRange;
			}
			if (memory != FirmwareMemory::kCustom
				&& (destinationOffset & 3) != 0) {
				return FirmwareStatus::kUnalignedDestination;
			}

			if (memory != FirmwareMemory::kCustom && visitor != nullptr) {
				FirmwareBlock view = {
					memory, destinationOffset, data + dataOffset, blockSize
				};
				const FirmwareStatus status = visitor(view, context);
				if (status != FirmwareStatus::kOk)
					return status;
			}
			blockOffset = dataOffset + blockSize;
		}

		if (blockOffset != moduleEnd)
			return FirmwareStatus::kSizeMismatch;
		moduleOffset = moduleEnd;
	}

	return moduleOffset == size
		? FirmwareStatus::kOk : FirmwareStatus::kSizeMismatch;
}


const char*
FirmwareStatusName(FirmwareStatus status)
{
	switch (status) {
		case FirmwareStatus::kOk:
			return "ok";
		case FirmwareStatus::kNullData:
			return "null data";
		case FirmwareStatus::kTooSmall:
			return "file header truncated";
		case FirmwareStatus::kBadSignature:
			return "bad $SST signature";
		case FirmwareStatus::kSizeMismatch:
			return "declared size mismatch";
		case FirmwareStatus::kOverflow:
			return "integer overflow";
		case FirmwareStatus::kTruncated:
			return "module or block truncated";
		case FirmwareStatus::kBadBlockType:
			return "unsupported block type";
		case FirmwareStatus::kBadBlockSize:
			return "zero-length block";
		case FirmwareStatus::kUnalignedDestination:
			return "block destination is not 32-bit aligned";
		case FirmwareStatus::kDestinationRange:
			return "block destination out of range";
	}
	return "unknown parser error";
}

} // namespace jr::byt_audio
