// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot
#pragma once

#include <stdint.h>


namespace jr::byt_audio::max98090 {

constexpr uint8_t kSoftwareReset = 0x00;
constexpr uint8_t kSystemClock = 0x1b;
constexpr uint8_t kClockMode = 0x1c;
constexpr uint8_t kClockRatioNiMsb = 0x1d;
constexpr uint8_t kClockRatioNiLsb = 0x1e;
constexpr uint8_t kMasterMode = 0x21;
constexpr uint8_t kInterfaceFormat = 0x22;
constexpr uint8_t kTdmControl = 0x23;
constexpr uint8_t kTdmFormat = 0x24;
constexpr uint8_t kIoConfiguration = 0x25;
constexpr uint8_t kFilterConfiguration = 0x26;
constexpr uint8_t kDaiPlaybackLevel = 0x27;
constexpr uint8_t kHeadphoneControl = 0x2b;
constexpr uint8_t kLeftHeadphoneVolume = 0x2c;
constexpr uint8_t kRightHeadphoneVolume = 0x2d;
constexpr uint8_t kLeftSpeakerMixer = 0x2e;
constexpr uint8_t kRightSpeakerMixer = 0x2f;
constexpr uint8_t kSpeakerControl = 0x30;
constexpr uint8_t kLeftSpeakerVolume = 0x31;
constexpr uint8_t kRightSpeakerVolume = 0x32;
constexpr uint8_t kOutputEnable = 0x3f;
constexpr uint8_t kDeviceShutdown = 0x45;
constexpr uint8_t kRevision = 0xff;

constexpr uint8_t kReset = 1u << 7;
constexpr uint8_t kSystemClock19M2 = 1u << 4;
constexpr uint8_t kConsumerClockRatio = 0;
constexpr uint8_t kMasterModeConsumer = 0;
constexpr uint8_t kInterfaceI2sS16Normal = 1u << 2;
constexpr uint8_t kTdmDisabled = 0;
constexpr uint8_t kIoPlayback = 1u << 0;
constexpr uint8_t kFilterMusicPlaybackDcBlock = (1u << 7) | (1u << 5);
constexpr uint8_t kDaiPlaybackUnmutedUnity = 0;
constexpr uint8_t kLeftDacToLeftSpeaker = 1u << 0;
constexpr uint8_t kRightDacToRightSpeaker = 1u << 1;
constexpr uint8_t kLeftDacEnable = 1u << 0;
constexpr uint8_t kRightDacEnable = 1u << 1;
constexpr uint8_t kLeftHeadphoneEnable = 1u << 6;
constexpr uint8_t kRightHeadphoneEnable = 1u << 7;
constexpr uint8_t kLeftSpeakerEnable = 1u << 4;
constexpr uint8_t kRightSpeakerEnable = 1u << 5;
constexpr uint8_t kDacAndSpeakerEnable = kLeftDacEnable | kRightDacEnable
	| kLeftSpeakerEnable | kRightSpeakerEnable;
constexpr uint8_t kDacAndHeadphoneEnable = kLeftDacEnable | kRightDacEnable
	| kLeftHeadphoneEnable | kRightHeadphoneEnable;
constexpr uint8_t kShutdownRelease = 1u << 7;
constexpr uint8_t kHeadphoneMute = 1u << 7;
constexpr uint8_t kSpeakerMute = 1u << 7;

constexpr uint8_t kHeadphoneVolumeHardwareMaximum = 31;
constexpr uint8_t kSpeakerVolumeRawMinimum = 24;
constexpr uint8_t kSpeakerVolumeHardwareMaximum = 39;
constexpr uint8_t kMixerVolumeMaximum = 3;

constexpr uint8_t
SpeakerControlValue(uint8_t logicalVolume)
{
	const uint8_t rawValue = kMixerVolumeMaximum - logicalVolume;
	return (rawValue << 2) | rawValue;
}

} // namespace jr::byt_audio::max98090
