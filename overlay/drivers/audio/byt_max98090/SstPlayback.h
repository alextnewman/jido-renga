// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot
#pragma once

// Host-testable serialization, parsing, and counter math for the SST/MRFLD
// playback path. No Haiku kernel dependencies; all functions are pure.

#include "Ipc.h"
#include "PlatformProfile.h"
#include "SstProtocol.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>


namespace jr::byt_audio {

// ---------------------------------------------------------------------------
// DSP byte-stream command header (sst_dsp_header in Linux)
// Encodes location_id (cell_nbr_idx | path_id<<8), module_id, command_id,
// length. Used for SBA/MMX commands sent via sst_send_byte_stream_mrfld.
// ---------------------------------------------------------------------------
#pragma pack(push, 1)
struct SstByteStreamDspHeader {
	uint8_t		cellIndex;
	uint8_t		pathId;
	uint16_t	moduleId;
	uint16_t	commandId;
	uint16_t	length;
};

// Gain cell (gain_cell in Linux). 10 bytes.
struct SstGainCell {
	uint8_t		cellIndex;
	uint8_t		pathId;
	uint16_t	moduleId;
	int16_t		leftGain;
	int16_t		rightGain;
	uint16_t	timeConstant;
};

// Gain command: header + gain_cell_num + cells.
struct SstGainCommand {
	SstByteStreamDspHeader	header;
	uint16_t				cellCount;
	SstGainCell				cells[1];
};

// Switch-wire-matrix (SWM) input ID.
struct SstSwmInputId {
	uint8_t		cellIndex;
	uint8_t		pathId;
	uint16_t	moduleId;
};

// SWM command: header + output_id + switch_state + nb_inputs + inputs[].
struct SstSwmCommand {
	SstByteStreamDspHeader	header;
	uint8_t					outputCellIndex;
	uint8_t					outputPathId;
	uint16_t				outputModuleId;
	uint16_t				switchState;
	uint16_t				inputCount;
	SstSwmInputId			inputs[6];
};

// Media path enable/disable command.
struct SstMediaPathCommand {
	SstByteStreamDspHeader	header;
	uint16_t				switchState;
};
#pragma pack(pop)


static_assert(sizeof(SstByteStreamDspHeader) == 8);
static_assert(sizeof(SstGainCell) == 10);
static_assert(sizeof(SstSwmInputId) == 4);


// ---------------------------------------------------------------------------
// Path / location IDs from Linux sst-atom-controls.h
// ---------------------------------------------------------------------------
constexpr uint8_t kDefaultCellIndex = 0xff;
constexpr uint8_t kGainCellIndex = 0;
constexpr uint16_t kDefaultModuleId = 0xffff;
constexpr uint16_t kGainCellModuleId = 0x0067;
constexpr uint16_t kGainTimeConstant = 5;

// Output path IDs (high byte of location_id)
constexpr uint8_t kPathCodecOut0 = 0x02;
constexpr uint8_t kPathPcm0Out = 0x0d;
constexpr uint8_t kPathMedia0Out = 0x12;

// Input path IDs
constexpr uint8_t kPathPcm0In = 0x8d;
constexpr uint8_t kPathMedia0In = 0x8f;
constexpr uint8_t kPathMedia1In = 0x90;

// Command IDs
constexpr uint16_t kCmdSetGain = 33;
constexpr uint16_t kCmdSetSwm = 114;
constexpr uint16_t kCmdSetMediaPath = 119;
constexpr uint16_t kSstErrorStreamInUse = 15;

// Switch states
constexpr uint16_t kSwitchOn = 3;
constexpr uint16_t kPathOn = 1;


// ---------------------------------------------------------------------------
// Playback command sequence builder
//
// The BYT/MAX98090 playback path requires these commands in order:
//   0. SBA virtual-bus start (cmd 85) — already in SstProtocol.h
//   1. SSP configure (cmd 117) — already in SstProtocol.h
//   2. SSP slot map (cmd 130) — already in SstProtocol.h
//   3. MMX media1 gain 0 dB
//   4. MMX SWM: media1_in → media0_out
//   5. MMX enable media0 path
//   6. SBA enable pcm0 input
//   7. SBA gain pcm0 input 0 dB
//   8. SBA SWM: pcm0_in → codec_out0
//   9. SBA gain codec_out0 0 dB
//
// Commands 0–2 use the SstProtocol.h types directly. Commands 3–9 are
// serialized here for sending via sst_send_byte_stream_mrfld style IPC.
// ---------------------------------------------------------------------------

constexpr size_t kPlaybackCommandCount = 10;
constexpr size_t kPlaybackHardwareCommandCount = 2;
constexpr size_t kPlaybackRouteCommandCount
	= kPlaybackCommandCount - kPlaybackHardwareCommandCount;


// Helper: fill a default DSP header (SST_FILL_DEFAULT_DESTINATION).
inline SstByteStreamDspHeader
MakeDefaultDspHeader(uint16_t commandId, uint16_t payloadLength)
{
	return {kDefaultCellIndex, kDefaultCellIndex, kDefaultModuleId,
		commandId, payloadLength};
}


// 3. MMX media1 gain 0 dB
inline SstGainCommand
MakeMedia1Gain0dB()
{
	SstGainCommand cmd = {};
	cmd.header = MakeDefaultDspHeader(kCmdSetGain,
		sizeof(SstGainCommand) - sizeof(SstByteStreamDspHeader));
	cmd.cellCount = 1;
	cmd.cells[0].cellIndex = kGainCellIndex;
	cmd.cells[0].pathId = kPathMedia1In;
	cmd.cells[0].moduleId = kGainCellModuleId;
	cmd.cells[0].leftGain = kGainZeroDb;
	cmd.cells[0].rightGain = kGainZeroDb;
	cmd.cells[0].timeConstant = kGainTimeConstant;
	return cmd;
}


// 4. MMX SWM: media1_in → media0_out
inline SstSwmCommand
MakeMedia1ToMedia0Swm()
{
	SstSwmCommand cmd = {};
	cmd.header = MakeDefaultDspHeader(kCmdSetSwm, 0);
	cmd.outputCellIndex = kDefaultCellIndex;
	cmd.outputPathId = kPathMedia0Out;
	cmd.outputModuleId = kDefaultModuleId;
	cmd.switchState = kSwitchOn;
	cmd.inputCount = 1;
	cmd.inputs[0].cellIndex = kDefaultCellIndex;
	cmd.inputs[0].pathId = kPathMedia1In;
	cmd.inputs[0].moduleId = kDefaultModuleId;
	// Length = everything after the header up to and including inputs[0]
	cmd.header.length = static_cast<uint16_t>(
		offsetof(SstSwmCommand, inputs) + sizeof(SstSwmInputId)
		- sizeof(SstByteStreamDspHeader));
	return cmd;
}


// 5. MMX enable media0 path
inline SstMediaPathCommand
MakeMedia0PathEnable()
{
	SstMediaPathCommand cmd = {};
	cmd.header.cellIndex = kDefaultCellIndex;
	cmd.header.pathId = kPathMedia0Out;
	cmd.header.moduleId = kDefaultModuleId;
	cmd.header.commandId = kCmdSetMediaPath;
	cmd.header.length = sizeof(SstMediaPathCommand)
		- sizeof(SstByteStreamDspHeader);
	cmd.switchState = kPathOn;
	return cmd;
}


// 6. SBA enable pcm0 input
inline SstMediaPathCommand
MakePcm0InputEnable()
{
	SstMediaPathCommand cmd = {};
	cmd.header.cellIndex = kDefaultCellIndex;
	cmd.header.pathId = kPathPcm0In;
	cmd.header.moduleId = kDefaultModuleId;
	cmd.header.commandId = kCmdSetMediaPath;
	cmd.header.length = sizeof(SstMediaPathCommand)
		- sizeof(SstByteStreamDspHeader);
	cmd.switchState = kPathOn;
	return cmd;
}


// 7. SBA gain pcm0 input 0 dB
inline SstGainCommand
MakePcm0InputGain0dB()
{
	SstGainCommand cmd = {};
	cmd.header = MakeDefaultDspHeader(kCmdSetGain,
		sizeof(SstGainCommand) - sizeof(SstByteStreamDspHeader));
	cmd.cellCount = 1;
	cmd.cells[0].cellIndex = kGainCellIndex;
	cmd.cells[0].pathId = kPathPcm0In;
	cmd.cells[0].moduleId = kGainCellModuleId;
	cmd.cells[0].leftGain = kGainZeroDb;
	cmd.cells[0].rightGain = kGainZeroDb;
	cmd.cells[0].timeConstant = kGainTimeConstant;
	return cmd;
}


// 8. SBA SWM: pcm0_in → codec_out0
inline SstSwmCommand
MakePcm0ToCodecOut0Swm()
{
	SstSwmCommand cmd = {};
	cmd.header = MakeDefaultDspHeader(kCmdSetSwm, 0);
	cmd.outputCellIndex = kDefaultCellIndex;
	cmd.outputPathId = kPathCodecOut0;
	cmd.outputModuleId = kDefaultModuleId;
	cmd.switchState = kSwitchOn;
	cmd.inputCount = 1;
	cmd.inputs[0].cellIndex = kDefaultCellIndex;
	cmd.inputs[0].pathId = kPathPcm0In;
	cmd.inputs[0].moduleId = kDefaultModuleId;
	cmd.header.length = static_cast<uint16_t>(
		offsetof(SstSwmCommand, inputs) + sizeof(SstSwmInputId)
		- sizeof(SstByteStreamDspHeader));
	return cmd;
}


// 9. SBA gain codec_out0 0 dB
inline SstGainCommand
MakeCodecOut0Gain0dB()
{
	SstGainCommand cmd = {};
	cmd.header = MakeDefaultDspHeader(kCmdSetGain,
		sizeof(SstGainCommand) - sizeof(SstByteStreamDspHeader));
	cmd.cellCount = 1;
	cmd.cells[0].cellIndex = kGainCellIndex;
	cmd.cells[0].pathId = kPathCodecOut0;
	cmd.cells[0].moduleId = kGainCellModuleId;
	cmd.cells[0].leftGain = kGainZeroDb;
	cmd.cells[0].rightGain = kGainZeroDb;
	cmd.cells[0].timeConstant = kGainTimeConstant;
	return cmd;
}


// ---------------------------------------------------------------------------
// Allocation body builder
// ---------------------------------------------------------------------------

// Build the exact 100-byte SstMrfldAllocation for a configured profile.
inline SstMrfldAllocation
BuildAllocation(const PcmStreamProfile& pcm, uint32_t ringPhysical,
	uint32_t ringBytes, uint32_t periodBytes, uint32_t timestampAddress)
{
	SstMrfldAllocation alloc = {};
	alloc.codecType = pcm.codecType;
	alloc.operation = pcm.operation;
	alloc.scatterGatherCount = 1;
	alloc.ringBuffers[0].address = ringPhysical;
	alloc.ringBuffers[0].size = ringBytes;
	alloc.fragmentSizeBytes = periodBytes;
	alloc.timestampAddress = timestampAddress;

	// Codec parameters: snd_pcm_params (16 bytes within 24-byte field)
	// Layout: num_chan(u8), pcm_wd_sz(u8), use_offload_path(u8), rsvd(u8),
	//         sfreq(u32), channel_map[8]
	alloc.codecParameters[0] = pcm.channels;
	alloc.codecParameters[1] = pcm.sampleBits;
	alloc.codecParameters[2] = 0;	// use_offload_path = 0
	alloc.codecParameters[3] = 0;	// reserved
	alloc.codecParameters[4] = static_cast<uint8_t>(pcm.sampleRate);
	alloc.codecParameters[5] = static_cast<uint8_t>(pcm.sampleRate >> 8);
	alloc.codecParameters[6] = static_cast<uint8_t>(pcm.sampleRate >> 16);
	alloc.codecParameters[7] = static_cast<uint8_t>(pcm.sampleRate >> 24);
	memcpy(alloc.codecParameters + 8, pcm.channelMap, sizeof(pcm.channelMap));

	return alloc;
}


// ---------------------------------------------------------------------------
// Allocation response parsing
//
// The DSP responds with struct snd_sst_alloc_response, beginning directly
// with snd_sst_str_type (codec_type, str_type, operation, protected_str,
// time_slots, reserved, result(u16)). Result is at offset 6.
// ---------------------------------------------------------------------------

inline bool
ParseAllocationResult(const uint8_t* data, uint32_t size, uint16_t& result)
{
	// MRFLD firmware commonly acknowledges a successful allocation with a
	// short, zero-result IPC and no mailbox body. Linux accepts that as success.
	if (size == 0) {
		result = 0;
		return true;
	}
	if (data == nullptr || size < 8)
		return false;
	result = data[6] | static_cast<uint16_t>(data[7]) << 8;
	return true;
}


constexpr bool
ShouldRecoverStaleAllocation(uint16_t result, uint32_t attempt)
{
	return result == kSstErrorStreamInUse && attempt == 0;
}


constexpr uint8_t kFixedPlaybackChannelMask = 0x03;


constexpr bool
AcceptFixedPlaybackChannelMask(uint8_t enabledChannels)
{
	return enabledChannels == kFixedPlaybackChannelMask;
}


// ---------------------------------------------------------------------------
// Timestamp reading and counter-to-cycle/frame math
//
// The firmware writes a packed 76-byte snd_sst_tstamp per stream at
// mailbox + 0x800 + streamId * 76.
// Layout: ring_buffer_counter(u64), hardware_counter(u64), ...
//
// For Haiku multi_audio playback_buffer_cycle semantics: the current buffer
// index is (hardware_counter / periodBytes) % periodCount, matching how HDA
// drivers derive the buffer index from the DMA position.
// ---------------------------------------------------------------------------

inline uint64_t
ReadTimestampU64(const volatile uint8_t* base, size_t offset)
{
	// Read two 32-bit halves to avoid tearing on 32-bit platforms.
	// Read low first, then high, then re-read low. If low decreased
	// (rollover), re-read high.
	const volatile uint32_t* lo = reinterpret_cast<const volatile uint32_t*>(
		base + offset);
	const volatile uint32_t* hi = reinterpret_cast<const volatile uint32_t*>(
		base + offset + 4);
	uint32_t lowA = *lo;
	uint32_t high = *hi;
	uint32_t lowB = *lo;
	if (lowB < lowA)
		high = *hi;
	return static_cast<uint64_t>(high) << 32 | lowB;
}


constexpr uint32_t
PlaybackBufferCycle(uint64_t hwCounter, uint32_t periodBytes,
	int32_t periodCount)
{
	if (periodBytes == 0 || periodCount <= 0)
		return 0;
	return static_cast<uint32_t>(
		(hwCounter / periodBytes) % static_cast<uint32_t>(periodCount));
}


constexpr uint64_t
PlayedFrames(uint64_t hwCounter, uint32_t frameBytes)
{
	return frameBytes > 0 ? hwCounter / frameBytes : 0;
}


// ---------------------------------------------------------------------------
// Byte stream IPC header packing for non-allocation commands.
//
// sst_send_byte_stream_mrfld builds a large IPC with:
//   - header_high: msg_id = ipc_msg, task_id, drv_id = pvt_id, large=1,
//     busy=1, res_rqd = block
//   - header_low = payload length (the byte stream data)
//   - mailbox = the byte stream data (DSP header + cmd body)
//
// For the setup commands, ipc_msg varies:
//   - VB start, SSP, path enables, SWM: IPC_CMD (1), blocked
//   - Slot map, gains: IPC_IA_SET_PARAMS (2), blocked
// ---------------------------------------------------------------------------

constexpr uint8_t kIpcCmd = 1;
constexpr uint8_t kIpcSetParams = 2;


// Return the total serialized size of a gain command.
constexpr size_t
GainCommandSize()
{
	return sizeof(SstGainCommand);
}

// Return the total serialized size of a SWM command with N inputs.
constexpr size_t
SwmCommandSize(uint16_t inputCount)
{
	return offsetof(SstSwmCommand, inputs)
		+ inputCount * sizeof(SstSwmInputId);
}

// Return the total serialized size of a media path command.
constexpr size_t
MediaPathCommandSize()
{
	return sizeof(SstMediaPathCommand);
}


// ---------------------------------------------------------------------------
// Playback command descriptor for iteration
// ---------------------------------------------------------------------------

struct PlaybackCommand {
	const void*	data;
	uint16_t	size;
	uint8_t		ipcMsg;
	uint8_t		taskId;
	bool		blocked;
};

} // namespace jr::byt_audio
