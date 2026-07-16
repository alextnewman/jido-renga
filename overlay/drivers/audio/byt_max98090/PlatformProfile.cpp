// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot

#include "PlatformProfile.h"

#include <string.h>


namespace jr::byt_audio {

namespace {

constexpr uint16_t kSbaVirtualBusStart = 85;
constexpr uint16_t kSbaConfigureSsp = 117;
constexpr uint16_t kSbaSetSspSlotMap = 130;
constexpr uint16_t kSspCodecSelection = 3;
constexpr uint16_t kSspSwitchOn = 3;
constexpr uint8_t kWinkySpeakerVolumeMaximum = 15;
constexpr uint8_t kWinkySpeakerVolumeDefault = 10;
constexpr uint8_t kWinkyHeadphoneVolumeMaximum = 19;
constexpr uint8_t kWinkyHeadphoneVolumeDefault = 19;
constexpr uint8_t kWinkySpeakerMixerVolume = 2;

const PlatformProfile* const kProfiles[] = {
	&kWinkyProfile
};

} // namespace


const PlatformProfile kWinkyProfile = {
	"winky",
	"Samsung Chromebook 2 XE500C12 (Winky)",
	"Bay Trail MAX98090",
	"80860F28",
	"193C9890",
	0x10,
	"/firmware/byt_max98090/fw_sst_0f28.bin",
	"/boot/system/non-packaged/data/firmware/byt_max98090/fw_sst_0f28.bin",
	{
		0x8086,
		0x0f1c,
		0x44,
		0xfffffe00,
		0x100,
		0x60,
		0x7,
		0x5,
		"PLT_CLK_0 at 19.2 MHz"
	},
	{
		0,
		2,
		5,
		29,
		0x20000000,
		0x100000
	},
	{
		1,
		3,
		1,
		0x90,
		0xff344000,
		{0xffff, 0xffff, kSbaVirtualBusStart, 0},
		{
			{0xffff, 0xffff, kSbaConfigureSsp,
				sizeof(SstSspCommand) - sizeof(SstDspHeader)},
			kSspCodecSelection,
			kSspSwitchOn,
			static_cast<uint16_t>(16 | (2 << 6)),
			0xff03,
			0xff03,
			3,
			1,
			16,
			0x0101
		},
		{
			{0xffff, 0xffff, kSbaSetSspSlotMap,
				sizeof(SstSspSlotMapCommand) - sizeof(SstDspHeader)},
			kSbaSetSspSlotMap,
			18,
			kSspCodecSelection,
			{0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80},
			{0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80}
		},
		{
			1,
			0,
			2,
			16,
			48000,
			{0, 1, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff}
		}
	},
	{
		0,
		false,
		1,
		true,
		200000
	},
	{
		kWinkySpeakerVolumeMaximum,
		kWinkySpeakerVolumeDefault,
		kWinkyHeadphoneVolumeMaximum,
		kWinkyHeadphoneVolumeDefault,
		kWinkySpeakerMixerVolume
	}
};


size_t
PlatformProfileCount()
{
	return sizeof(kProfiles) / sizeof(kProfiles[0]);
}


const PlatformProfile&
PlatformProfileAt(size_t index)
{
	return *kProfiles[index];
}


const PlatformProfile*
FindPlatformProfile(const char* id)
{
	if (id == nullptr)
		return nullptr;
	for (const PlatformProfile* profile : kProfiles) {
		if (strcmp(profile->id, id) == 0)
			return profile;
	}
	return nullptr;
}


const PlatformProfile*
MatchLpeProfile(const char* acpiId)
{
	if (acpiId == nullptr)
		return nullptr;
	for (const PlatformProfile* profile : kProfiles) {
		if (strcmp(profile->lpeAcpiId, acpiId) == 0)
			return profile;
	}
	return nullptr;
}


const PlatformProfile*
MatchCodecProfile(const char* acpiId, uint16_t address)
{
	if (acpiId == nullptr)
		return nullptr;
	for (const PlatformProfile* profile : kProfiles) {
		if (strcmp(profile->codecAcpiId, acpiId) == 0
			&& profile->codecI2cAddress == address) {
			return profile;
		}
	}
	return nullptr;
}

} // namespace jr::byt_audio
