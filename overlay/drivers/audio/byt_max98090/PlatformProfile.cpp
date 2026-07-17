// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot

#include "PlatformProfile.h"

#include "Max98090Registers.h"

#include <string.h>


namespace jr::byt_audio {

namespace {

constexpr uint16_t kSbaVirtualBusStart = 85;
constexpr uint16_t kSbaConfigureSsp = 117;
constexpr uint16_t kSbaSetSspSlotMap = 130;
constexpr uint16_t kSspCodecSelection = 3;
constexpr uint16_t kSspSwitchOn = 3;
constexpr uint8_t kWinkySpeakerVolumeMaximum = 20;
constexpr uint8_t kWinkySpeakerVolumeDefault = 10;
constexpr uint8_t kWinkyHeadphoneVolumeMaximum = 19;
constexpr uint8_t kWinkyHeadphoneVolumeDefault = 19;
constexpr uint8_t kWinkySpeakerMixerVolume = 2;

constexpr uint8_t kWinkyEqualizerCoefficients[] = {
	0x0f, 0xb4, 0xdf, 0xe0, 0x96, 0x41, 0x0f, 0xb4, 0xdf, 0xe0, 0x97, 0xa2,
	0x0f, 0x6b, 0x1f,
	0x06, 0x60, 0x0d, 0xf3, 0x47, 0x82, 0x06, 0x58, 0x76, 0xe0, 0x0c, 0x0c,
	0x0f, 0xf3, 0xf9,
	0x10, 0x18, 0xa5, 0xe0, 0x5f, 0x60, 0x0f, 0x93, 0x18, 0xe0, 0x5f, 0x60,
	0x0f, 0xab, 0xbd,
	0x0f, 0xca, 0x56, 0xe1, 0x1f, 0x73, 0x0f, 0x47, 0x66, 0xe1, 0x1f, 0x73,
	0x0f, 0x11, 0xbc,
	0x10, 0x6d, 0x2d, 0xe4, 0xb2, 0xf8, 0x0c, 0x47, 0x86, 0xe4, 0xb2, 0xf8,
	0x0c, 0xb4, 0xb3,
	0x0d, 0x5b, 0x5c, 0xec, 0xf4, 0x22, 0x0b, 0x48, 0x46, 0xec, 0xf4, 0x22,
	0x08, 0xa3, 0xa2,
	0x15, 0x10, 0x2f, 0xf6, 0x94, 0x76, 0x03, 0x8d, 0x7a, 0xf6, 0x94, 0x76,
	0x08, 0x9d, 0xa9
};

static_assert(sizeof(kWinkyEqualizerCoefficients) == max98090::kEqualizerSize);

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
		kWinkySpeakerMixerVolume,
		{
			true,
			kWinkyEqualizerCoefficients,
			sizeof(kWinkyEqualizerCoefficients),
			max98090::kEqualizerPreattenuation4Db,
			static_cast<uint8_t>(max98090::kDrcRelease1Second
				| max98090::kDrcAttack1Millisecond),
			static_cast<uint8_t>(max98090::kDrcCompressionInfinity
				| max98090::kDrcCompressionThresholdMinus11Db),
			0,
			max98090::kDrcMakeup4Db
		}
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
