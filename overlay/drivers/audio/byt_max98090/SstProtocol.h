// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot
#pragma once

#include <stddef.h>
#include <stdint.h>


namespace jr::byt_audio {

constexpr int16_t kGainZeroDb = 0;

constexpr size_t kMrfldAllocationSize = 100;
constexpr uint32_t kMrfldTimestampBase = 0x800;
constexpr uint32_t kMrfldTimestampStride = 76;
constexpr uint32_t kPlaybackFrameQuantum = 48;
constexpr uint32_t kDefaultPeriodFrames = 960;
constexpr int32_t kDefaultPeriodCount = 4;
constexpr uint64_t kSstDmaAddressSpaceSize = uint64_t{1} << 32;


constexpr uint32_t
MrfldTimestampOffset(uint32_t streamId)
{
	return kMrfldTimestampBase + streamId * kMrfldTimestampStride;
}


constexpr uint32_t
MrfldTimestampAddress(uint32_t mailboxAddress, uint32_t streamId)
{
	return mailboxAddress + MrfldTimestampOffset(streamId);
}


constexpr uint32_t
NormalizePeriodFrames(uint32_t requested)
{
	return requested != 0 && requested <= 4096
			&& requested % kPlaybackFrameQuantum == 0
		? requested : kDefaultPeriodFrames;
}


constexpr int32_t
NormalizePeriodCount(int32_t requested)
{
	return requested >= 2 && requested <= 8 && requested % 2 == 0
		? requested : kDefaultPeriodCount;
}


constexpr bool
FitsSstDmaRange(uint64_t address, uint64_t size)
{
	return address < kSstDmaAddressSpaceSize
		&& size <= kSstDmaAddressSpaceSize - address;
}


#pragma pack(push, 1)
struct SstDspHeader {
	uint16_t	locationId;
	uint16_t	moduleId;
	uint16_t	commandId;
	uint16_t	length;
};

struct SstSspCommand {
	SstDspHeader	header;
	uint16_t	selection;
	uint16_t	switchState;
	uint16_t	slotConfiguration;
	uint16_t	activeTxSlotMap;
	uint16_t	activeRxSlotMap;
	uint16_t	frameSyncFrequency;
	uint16_t	polarity;
	uint16_t	frameSyncWidth;
	uint16_t	protocolAndStartDelay;
};

struct SstSspSlotMapCommand {
	SstDspHeader	header;
	uint16_t	parameterId;
	uint16_t	parameterLength;
	uint16_t	selection;
	uint8_t		receiveSlots[8];
	uint8_t		transmitSlots[8];
};

struct SstMrfldBufferAddress {
	uint32_t	address;
	uint32_t	size;
};

struct SstMrfldAllocation {
	uint16_t				codecType;
	uint8_t					operation;
	uint8_t					scatterGatherCount;
	SstMrfldBufferAddress	ringBuffers[8];
	uint32_t				fragmentSizeBytes;
	uint32_t				timestampAddress;
	uint8_t					codecParameters[24];
};
#pragma pack(pop)


static_assert(sizeof(SstDspHeader) == 8);
static_assert(sizeof(SstSspCommand) == 26);
static_assert(sizeof(SstSspSlotMapCommand) == 30);
static_assert(sizeof(SstMrfldBufferAddress) == 8);
static_assert(sizeof(SstMrfldAllocation) == kMrfldAllocationSize);
static_assert(offsetof(SstMrfldAllocation, ringBuffers) == 4);
static_assert(offsetof(SstMrfldAllocation, fragmentSizeBytes) == 68);
static_assert(offsetof(SstMrfldAllocation, timestampAddress) == 72);
static_assert(offsetof(SstMrfldAllocation, codecParameters) == 76);

} // namespace jr::byt_audio
