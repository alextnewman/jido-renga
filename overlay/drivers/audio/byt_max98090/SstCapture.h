// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot (GPT-5.6 Sol)
#pragma once

#include "SstPlayback.h"

#include <stdint.h>


namespace jr::byt_audio {

constexpr uint8_t kPathPcm1Out = 0x0e;
constexpr uint8_t kPathCodecIn0 = 0x82;
constexpr uint16_t kModuleDcr = 0x0094;
constexpr uint16_t kCmdSetIir = 129;

constexpr size_t kDcrParameterSize = 52;
constexpr size_t kCaptureRouteCommandCount = 6;


#pragma pack(push, 1)
struct SstDcrCommand {
	SstByteStreamDspHeader	header;
	uint8_t					parameters[kDcrParameterSize];
};
#pragma pack(pop)


static_assert(sizeof(SstDcrCommand)
	== sizeof(SstByteStreamDspHeader) + kDcrParameterSize);


inline SstDcrCommand
MakeCodecIn0DcrDefaults()
{
	SstDcrCommand command = {};
	command.header.cellIndex = kDefaultCellIndex;
	command.header.pathId = kPathCodecIn0;
	command.header.moduleId = kModuleDcr;
	command.header.commandId = kCmdSetIir;
	return command;
}


inline SstGainCommand
MakeCodecIn0Gain0dB()
{
	SstGainCommand command = {};
	command.header = MakeDefaultDspHeader(kCmdSetGain,
		sizeof(SstGainCommand) - sizeof(SstByteStreamDspHeader));
	command.cellCount = 1;
	command.cells[0].cellIndex = kGainCellIndex;
	command.cells[0].pathId = kPathCodecIn0;
	command.cells[0].moduleId = kGainCellModuleId;
	command.cells[0].leftGain = kGainZeroDb;
	command.cells[0].rightGain = kGainZeroDb;
	command.cells[0].timeConstant = kGainTimeConstant;
	return command;
}


inline SstSwmCommand
MakeCodecIn0ToPcm1Swm()
{
	SstSwmCommand command = {};
	command.header = MakeDefaultDspHeader(kCmdSetSwm, 0);
	command.outputCellIndex = kDefaultCellIndex;
	command.outputPathId = kPathPcm1Out;
	command.outputModuleId = kDefaultModuleId;
	command.switchState = kSwitchOn;
	command.inputCount = 1;
	command.inputs[0].cellIndex = kDefaultCellIndex;
	command.inputs[0].pathId = kPathCodecIn0;
	command.inputs[0].moduleId = kDefaultModuleId;
	command.header.length = static_cast<uint16_t>(
		offsetof(SstSwmCommand, inputs) + sizeof(SstSwmInputId)
		- sizeof(SstByteStreamDspHeader));
	return command;
}


inline SstMediaPathCommand
MakePcm1OutputEnable()
{
	SstMediaPathCommand command = {};
	command.header.cellIndex = kDefaultCellIndex;
	command.header.pathId = kPathPcm1Out;
	command.header.moduleId = kDefaultModuleId;
	command.header.commandId = kCmdSetMediaPath;
	command.header.length = sizeof(SstMediaPathCommand)
		- sizeof(SstByteStreamDspHeader);
	command.switchState = kPathOn;
	return command;
}


inline SstGainCommand
MakePcm1OutputGain0dB()
{
	SstGainCommand command = {};
	command.header = MakeDefaultDspHeader(kCmdSetGain,
		sizeof(SstGainCommand) - sizeof(SstByteStreamDspHeader));
	command.cellCount = 1;
	command.cells[0].cellIndex = kGainCellIndex;
	command.cells[0].pathId = kPathPcm1Out;
	command.cells[0].moduleId = kGainCellModuleId;
	command.cells[0].leftGain = kGainZeroDb;
	command.cells[0].rightGain = kGainZeroDb;
	command.cells[0].timeConstant = kGainTimeConstant;
	return command;
}


constexpr uint32_t
CaptureBufferCycle(uint64_t ringCounter, uint32_t periodBytes,
	int32_t periodCount)
{
	if (periodBytes == 0 || periodCount <= 0)
		return 0;
	return static_cast<uint32_t>(
		(ringCounter / periodBytes) % static_cast<uint32_t>(periodCount));
}


constexpr uint64_t
RecordedFrames(uint64_t ringCounter, uint32_t frameBytes)
{
	return frameBytes > 0 ? ringCounter / frameBytes : 0;
}


constexpr uint8_t kFixedDuplexChannelMask = 0x0f;


constexpr bool
AcceptFixedDuplexChannelMask(uint8_t enabledChannels)
{
	return enabledChannels == kFixedDuplexChannelMask;
}


} // namespace jr::byt_audio
