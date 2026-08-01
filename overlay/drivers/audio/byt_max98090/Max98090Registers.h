// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot
#pragma once

#include <stddef.h>
#include <stdint.h>


namespace jr::byt_audio::max98090 {

constexpr uint8_t kSoftwareReset = 0x00;
constexpr uint8_t kDeviceStatus = 0x01;
constexpr uint8_t kInputMode = 0x0f;
constexpr uint8_t kMic2InputLevel = 0x11;
constexpr uint8_t kDigitalMicEnable = 0x13;
constexpr uint8_t kDigitalMicConfig = 0x14;
constexpr uint8_t kLeftAdcMixer = 0x15;
constexpr uint8_t kRightAdcMixer = 0x16;
constexpr uint8_t kLeftAdcLevel = 0x17;
constexpr uint8_t kRightAdcLevel = 0x18;
constexpr uint8_t kAdcBiquadLevel = 0x19;
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
constexpr uint8_t kDaiPlaybackEqualizerLevel = 0x28;
constexpr uint8_t kHeadphoneControl = 0x2b;
constexpr uint8_t kLeftHeadphoneVolume = 0x2c;
constexpr uint8_t kRightHeadphoneVolume = 0x2d;
constexpr uint8_t kLeftSpeakerMixer = 0x2e;
constexpr uint8_t kRightSpeakerMixer = 0x2f;
constexpr uint8_t kSpeakerControl = 0x30;
constexpr uint8_t kLeftSpeakerVolume = 0x31;
constexpr uint8_t kRightSpeakerVolume = 0x32;
constexpr uint8_t kDrcTiming = 0x33;
constexpr uint8_t kDrcCompressor = 0x34;
constexpr uint8_t kDrcExpander = 0x35;
constexpr uint8_t kDrcGain = 0x36;
constexpr uint8_t kOutputEnable = 0x3f;
constexpr uint8_t kDspFilterEnable = 0x41;
constexpr uint8_t kBiasControl = 0x42;
constexpr uint8_t kAdcControl = 0x44;
constexpr uint8_t kInputEnable = 0x3e;
constexpr uint8_t kDeviceShutdown = 0x45;
constexpr uint8_t kEqualizerBase = 0x46;
constexpr uint8_t kRevision = 0xff;

constexpr uint8_t kReset = 1u << 7;
constexpr uint8_t kSystemClockLinuxWinky = 1u << 4;
constexpr uint8_t kConsumerClockRatio = 0;
constexpr uint8_t kMasterModeConsumer = 0;
constexpr uint8_t kInterfaceI2sS16Normal = 1u << 2;
constexpr uint8_t kTdmDisabled = 0;
constexpr uint8_t kIoPlayback = 1u << 0;
constexpr uint8_t kIoCapture = 1u << 1;
constexpr uint8_t kIoDuplex = kIoPlayback | kIoCapture;
constexpr uint8_t kFilterMusicPlaybackDcBlock = (1u << 7) | (1u << 5);
constexpr uint8_t kFilterRecordDcBlock = 1u << 6;
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
constexpr uint8_t kShutdownAssert = 0;
constexpr uint8_t kShutdownRelease = 1u << 7;
constexpr uint8_t kHeadphoneMute = 1u << 7;
constexpr uint8_t kSpeakerMute = 1u << 7;
constexpr uint8_t kEqualizer7BandEnable = 1u << 0;
constexpr uint8_t kDrcEnable = 1u << 7;
constexpr uint8_t kDrcRelease1Second = 3u << 4;
constexpr uint8_t kDrcAttack1Millisecond = 1u;
constexpr uint8_t kDrcCompressionInfinity = 4u << 5;
constexpr uint8_t kDrcCompressionThresholdMinus11Db = 11u;
constexpr uint8_t kDrcMakeup4Db = 4u;
constexpr uint8_t kEqualizerPreattenuation4Db = 4u;
constexpr uint8_t kDigitalMicClockDiv8 = 5u << 4;
constexpr uint8_t kDigitalMicChannels = (1u << 1) | (1u << 0);
constexpr uint8_t kDigitalMicCompensation48k = 6u << 4;
constexpr uint8_t kAdcBiquadAttenuation15Db = 15;
constexpr uint8_t kPllUnlocked = 1u << 5;
constexpr uint8_t kMic2In34 = 0;
constexpr uint8_t kMic2Gain10Db = 10;
constexpr uint8_t kMic2Pga0DbEnable = 1u << 5;
constexpr uint8_t kAdcMic2 = 1u << 6;
constexpr uint8_t kAdcBoost24DbVolumeMinus1Db = (4u << 4) | 4u;
constexpr uint8_t kMicBiasEnable = 1u << 4;
constexpr uint8_t kLeftAdcEnable = 1u << 0;
constexpr uint8_t kRightAdcEnable = 1u << 1;
constexpr uint8_t kMicBiasHighPerformance = 1u;
constexpr uint8_t kAdcDitherHighPerformance = (1u << 1) | (1u << 0);

struct CaptureImage {
	uint8_t digitalMicEnable;
	uint8_t digitalMicConfig;
	uint8_t mic2InputLevel;
	uint8_t inputEnable;
	uint8_t filterConfiguration;
	uint8_t adcBiquadLevel;
};


constexpr CaptureImage
CaptureImageFor(bool headsetMicrophone)
{
	return {
		static_cast<uint8_t>(kDigitalMicClockDiv8
			| (headsetMicrophone ? 0 : kDigitalMicChannels)),
		kDigitalMicCompensation48k,
		static_cast<uint8_t>(kMic2Gain10Db
			| (headsetMicrophone ? kMic2Pga0DbEnable : 0)),
		static_cast<uint8_t>(headsetMicrophone
			? kMicBiasEnable | kLeftAdcEnable | kRightAdcEnable : 0),
		static_cast<uint8_t>(kFilterMusicPlaybackDcBlock
			| kFilterRecordDcBlock),
		kAdcBiquadAttenuation15Db
	};
}

constexpr uint8_t kHeadphoneVolumeHardwareMaximum = 31;
constexpr uint8_t kSpeakerVolumeRawMinimum = 24;
constexpr uint8_t kSpeakerVolumeHardwareMaximum = 39;
constexpr uint8_t kMixerVolumeMaximum = 3;
constexpr size_t kEqualizerBandCount = 7;
constexpr size_t kEqualizerCoefficientCount = 5;
constexpr size_t kEqualizerCoefficientSize = 3;
constexpr size_t kEqualizerBandSize
	= kEqualizerCoefficientCount * kEqualizerCoefficientSize;
constexpr size_t kEqualizerSize = kEqualizerBandCount * kEqualizerBandSize;

constexpr uint8_t
SpeakerControlValue(uint8_t logicalVolume)
{
	const uint8_t rawValue = kMixerVolumeMaximum - logicalVolume;
	return (rawValue << 2) | rawValue;
}

} // namespace jr::byt_audio::max98090
