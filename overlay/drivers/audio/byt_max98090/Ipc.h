// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot
#pragma once

#include <stddef.h>
#include <stdint.h>


namespace jr::byt_audio {

constexpr uint8_t kMrfldIpcCommand = 0x01;
constexpr uint8_t kMrfldIpcSetParameters = 0x02;
constexpr uint8_t kMrfldProcessReply = 0x80;
constexpr uint8_t kMrfldAsyncDriverId = 0;
constexpr uint8_t kMrfldSerializedDriverId = 1;

constexpr uint16_t kMrfldFirmwareInitCommand = 0x01;
constexpr uint16_t kMrfldFirmwareAsyncError = 0x11;
constexpr uint16_t kMrfldAllocateStream = 0x02;
constexpr uint16_t kMrfldFreeStream = 0x03;
constexpr uint16_t kMrfldPauseStream = 0x04;
constexpr uint16_t kMrfldResumeStream = 0x05;
constexpr uint16_t kMrfldStartStream = 0x06;
constexpr uint16_t kMrfldDropStream = 0x07;
constexpr uint16_t kMrfldDrainStream = 0x08;
constexpr uint16_t kMrfldPeriodElapsed = 0x0a;
constexpr uint16_t kMrfldBufferUnderrun = 0x0b;
constexpr uint16_t kMrfldSetStreamParameters = 0x12;
constexpr uint16_t kMrfldSetGain = 0x21;

#pragma pack(push, 1)
struct MrfldDspHeader {
	uint16_t	moduleAndPipe;
	uint16_t	moduleId;
	uint16_t	commandId;
	uint16_t	length;
};

struct MrfldFirmwareInitPayload {
	uint8_t		version[4];
	uint8_t		buildDate[16];
	uint8_t		buildTime[16];
	uint16_t	result;
	uint8_t		moduleId;
	uint8_t		debugInfo;
};
#pragma pack(pop)

struct MrfldFirmwareInitInfo {
	uint8_t		version[4];
	uint16_t	result;
};

constexpr size_t kMrfldFirmwareInitShortMessageSize = 38;
constexpr size_t kMrfldFirmwareInitFullMessageSize
	= sizeof(MrfldDspHeader) + sizeof(MrfldFirmwareInitPayload);


constexpr MrfldDspHeader
MakeMrfldDspHeader(uint16_t commandId, uint8_t pipeId, uint16_t payloadLength)
{
	return {static_cast<uint16_t>(0xff
			| static_cast<uint16_t>(pipeId) << 8),
		0, commandId, payloadLength};
}


constexpr uint64_t
PackMrfldIpcHeader(uint32_t payloadSize, uint8_t ipcMessage, uint8_t taskId,
	uint8_t driverId, bool responseRequired, bool large, bool done, bool busy)
{
	uint32_t high = ipcMessage | (static_cast<uint32_t>(taskId & 0x0f) << 8)
		| (static_cast<uint32_t>(driverId & 0x0f) << 12);
	if (responseRequired)
		high |= 1u << 28;
	if (large)
		high |= 1u << 29;
	if (done)
		high |= 1u << 30;
	if (busy)
		high |= 1u << 31;
	return payloadSize | (static_cast<uint64_t>(high) << 32);
}


constexpr uint64_t
PackMrfldByteStreamHeader(uint32_t payloadSize, uint8_t ipcMessage,
	uint8_t taskId, uint8_t driverId, bool responseRequired)
{
	return PackMrfldIpcHeader(payloadSize, ipcMessage, taskId, driverId,
		responseRequired, true, false, true);
}


constexpr uint32_t
IpcPayloadSize(uint64_t header)
{
	return static_cast<uint32_t>(header);
}


constexpr uint8_t
IpcMessageId(uint64_t header)
{
	return static_cast<uint8_t>(header >> 32);
}


constexpr uint8_t
IpcTaskId(uint64_t header)
{
	return static_cast<uint8_t>((header >> (8 + 32)) & 0x0f);
}


constexpr uint8_t
IpcDriverId(uint64_t header)
{
	return static_cast<uint8_t>((header >> (12 + 32)) & 0x0f);
}


constexpr uint8_t
IpcResult(uint64_t header)
{
	return static_cast<uint8_t>((header >> (24 + 32)) & 0x0f);
}


constexpr bool
IpcLarge(uint64_t header)
{
	return (header & (uint64_t{1} << 61)) != 0;
}


constexpr bool
IpcDone(uint64_t header)
{
	return (header & (uint64_t{1} << 62)) != 0;
}


constexpr bool
IpcBusy(uint64_t header)
{
	return (header & (uint64_t{1} << 63)) != 0;
}


constexpr bool
IpcIsProcessReply(uint64_t header)
{
	return (IpcMessageId(header) & kMrfldProcessReply) != 0;
}


constexpr bool
IpcIsAsyncMessage(uint64_t header)
{
	return IpcDriverId(header) == kMrfldAsyncDriverId;
}


constexpr bool
IpcMatchesReply(uint64_t header, uint8_t messageId, uint8_t driverId)
{
	return !IpcIsAsyncMessage(header) && !IpcIsProcessReply(header)
		&& IpcMessageId(header) == messageId
		&& IpcDriverId(header) == driverId;
}


constexpr uint64_t
AcknowledgeMrfldIpc(uint64_t header)
{
	uint32_t high = static_cast<uint32_t>(header >> 32);
	high &= ~(uint32_t{1} << 31);
	high |= uint32_t{1} << 30;
	return static_cast<uint64_t>(high) << 32;
}


inline bool
DecodeMrfldFirmwareInit(const uint8_t* message, size_t size,
	MrfldFirmwareInitInfo& info)
{
	if (message == nullptr
		|| (size != kMrfldFirmwareInitShortMessageSize
			&& size < kMrfldFirmwareInitFullMessageSize)) {
		return false;
	}
	const uint16_t command = message[4] | static_cast<uint16_t>(message[5]) << 8;
	if (command != kMrfldFirmwareInitCommand)
		return false;

	for (size_t i = 0; i < sizeof(info.version); i++)
		info.version[i] = message[sizeof(MrfldDspHeader) + i];
	info.result = 0;

	if (size >= kMrfldFirmwareInitFullMessageSize) {
		const size_t resultOffset = sizeof(MrfldDspHeader)
			+ offsetof(MrfldFirmwareInitPayload, result);
		const uint16_t bodyResult = message[resultOffset]
			| static_cast<uint16_t>(message[resultOffset + 1]) << 8;
		if (bodyResult != 0)
			info.result = bodyResult;
	}
	return true;
}


static_assert(sizeof(MrfldDspHeader) == 8);
static_assert(sizeof(MrfldFirmwareInitPayload) == 40);
static_assert(kMrfldFirmwareInitFullMessageSize == 48);

} // namespace jr::byt_audio
