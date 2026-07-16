// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot
#pragma once

#include "SstProtocol.h"

#include <stddef.h>
#include <stdint.h>


namespace jr::byt_audio {

struct PlatformClockProfile {
	uint16_t	pciVendorId;
	uint16_t	pciDeviceId;
	uint16_t	barOffset;
	uint32_t	barMask;
	uint32_t	mapSize;
	uint32_t	registerOffset;
	uint32_t	clearMask;
	uint32_t	setBits;
	const char*	description;
};


struct LpeResourceProfile {
	uint32_t	lpeMemoryIndex;
	uint32_t	imrMemoryIndex;
	uint32_t	ipcIrqIndex;
	uint32_t	expectedIpcIrq;
	uint64_t	expectedImrBase;
	uint32_t	expectedImrSize;
};


struct PcmStreamProfile {
	uint16_t	codecType;
	uint8_t		operation;
	uint8_t		channels;
	uint8_t		sampleBits;
	uint32_t	sampleRate;
	uint8_t		channelMap[8];
};


struct SstPlaybackProfile {
	uint8_t					sbaTaskId;
	uint8_t					mmxTaskId;
	uint8_t					streamId;
	uint8_t					pipeId;
	uint32_t				mailboxLpeAddress;
	SstDspHeader			virtualBusStart;
	SstSspCommand			sspConfiguration;
	SstSspSlotMapCommand	sspSlotMap;
	PcmStreamProfile		pcm;
};


struct JackProfile {
	uint32_t	headphoneResourceIndex;
	bool		headphoneActiveLow;
	uint32_t	microphoneResourceIndex;
	bool		microphoneActiveLow;
	int64_t		debounce;
};


struct PlatformProfile {
	const char*				id;
	const char*				name;
	const char*				friendlyName;
	const char*				lpeAcpiId;
	const char*				codecAcpiId;
	uint16_t				codecI2cAddress;
	const char*				firmwareSubpath;
	const char*				requiredFirmwarePath;
	PlatformClockProfile	clock;
	LpeResourceProfile		resources;
	SstPlaybackProfile		playback;
	JackProfile				jack;
};


extern const PlatformProfile kWinkyProfile;

size_t PlatformProfileCount();
const PlatformProfile& PlatformProfileAt(size_t index);
const PlatformProfile* FindPlatformProfile(const char* id);
const PlatformProfile* MatchLpeProfile(const char* acpiId);
const PlatformProfile* MatchCodecProfile(const char* acpiId, uint16_t address);

} // namespace jr::byt_audio
