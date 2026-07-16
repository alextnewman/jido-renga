// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot
#pragma once

#include <stddef.h>
#include <stdint.h>


namespace jr::byt_audio {

enum class FirmwareStatus {
	kOk,
	kNullData,
	kTooSmall,
	kBadSignature,
	kSizeMismatch,
	kOverflow,
	kTruncated,
	kBadBlockType,
	kBadBlockSize,
	kUnalignedDestination,
	kDestinationRange
};

enum class FirmwareMemory : uint32_t {
	kIram = 1,
	kDram = 2,
	kDdr = 5,
	kCustom = 7
};

struct FirmwareLimits {
	size_t iramSize;
	size_t dramSize;
	size_t ddrSize;
};

struct FirmwareBlock {
	FirmwareMemory	memory;
	uint32_t		destinationOffset;
	const uint8_t*	data;
	uint32_t		size;
};

using FirmwareBlockVisitor = FirmwareStatus (*)(const FirmwareBlock& block,
	void* context);

bool CheckedRange(size_t offset, size_t length, size_t total);

FirmwareStatus ParseFirmware(const uint8_t* data, size_t size,
	const FirmwareLimits& limits, FirmwareBlockVisitor visitor, void* context);

const char* FirmwareStatusName(FirmwareStatus status);

} // namespace jr::byt_audio
