// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot

#include "framework/jr_test.h"

#include "Firmware.h"
#include "Ipc.h"
#include "Max98090Registers.h"
#include "ModuleNames.h"
#include "SstBoot.h"
#include "SstPlayback.h"
#include "SstProtocol.h"

#include <cstring>
#include <vector>


using namespace jr::byt_audio;

namespace {

void
Append32(std::vector<uint8_t>& bytes, uint32_t value)
{
	bytes.push_back(value);
	bytes.push_back(value >> 8);
	bytes.push_back(value >> 16);
	bytes.push_back(value >> 24);
}


void
Patch32(std::vector<uint8_t>& bytes, size_t offset, uint32_t value)
{
	for (size_t i = 0; i < 4; i++)
		bytes[offset + i] = static_cast<uint8_t>(value >> (i * 8));
}


std::vector<uint8_t>
MakeFirmware(uint32_t blockType = 1, uint32_t destination = 4,
	uint32_t blockSize = 4)
{
	std::vector<uint8_t> bytes = {'$', 'S', 'S', 'T'};
	Append32(bytes, 0);
	Append32(bytes, 1);
	Append32(bytes, 1);
	for (int i = 0; i < 4; i++)
		Append32(bytes, 0);

	bytes.insert(bytes.end(), {'M', 'O', 'D', '0'});
	Append32(bytes, 16 + blockSize);
	Append32(bytes, 1);
	Append32(bytes, 0);
	Append32(bytes, 0);

	Append32(bytes, blockType);
	Append32(bytes, blockSize);
	Append32(bytes, destination);
	Append32(bytes, 0);
	for (uint32_t i = 0; i < blockSize; i++)
		bytes.push_back(static_cast<uint8_t>(0xa0 + i));

	Patch32(bytes, 4, static_cast<uint32_t>(bytes.size() - 32));
	return bytes;
}


struct VisitState {
	int			count = 0;
	uint32_t	offset = 0;
	uint8_t		first = 0;
};


FirmwareStatus
Visit(const FirmwareBlock& block, void* context)
{
	VisitState* state = static_cast<VisitState*>(context);
	state->count++;
	state->offset = block.destinationOffset;
	state->first = block.data[0];
	return FirmwareStatus::kOk;
}

} // namespace


JR_TEST(byt_firmware, parses_bounded_block)
{
	std::vector<uint8_t> firmware = MakeFirmware();
	VisitState state;
	const FirmwareLimits limits = {0x14000, 0x28000, 0x100000};
	JR_CHECK_EQ(ParseFirmware(firmware.data(), firmware.size(), limits, Visit,
		&state), FirmwareStatus::kOk);
	JR_CHECK_EQ(state.count, 1);
	JR_CHECK_EQ(state.offset, (uint32_t)4);
	JR_CHECK_EQ(state.first, (uint8_t)0xa0);
}


JR_TEST(byt_firmware, rejects_file_and_module_size_mismatches)
{
	const FirmwareLimits limits = {0x14000, 0x28000, 0x100000};
	std::vector<uint8_t> firmware = MakeFirmware();
	firmware[4]++;
	JR_CHECK_EQ(ParseFirmware(firmware.data(), firmware.size(), limits, nullptr,
		nullptr), FirmwareStatus::kSizeMismatch);

	firmware = MakeFirmware();
	Patch32(firmware, 36, 0xffffffff);
	JR_CHECK_EQ(ParseFirmware(firmware.data(), firmware.size(), limits, nullptr,
		nullptr), FirmwareStatus::kTruncated);
}


JR_TEST(byt_firmware, rejects_block_overflow_and_unknown_type)
{
	const FirmwareLimits limits = {16, 16, 16};
	std::vector<uint8_t> firmware = MakeFirmware(1, 14, 4);
	JR_CHECK_EQ(ParseFirmware(firmware.data(), firmware.size(), limits, nullptr,
		nullptr), FirmwareStatus::kDestinationRange);

	firmware = MakeFirmware(9, 0, 4);
	JR_CHECK_EQ(ParseFirmware(firmware.data(), firmware.size(), limits, nullptr,
		nullptr), FirmwareStatus::kBadBlockType);
	firmware = MakeFirmware(1, 0, 0);
	JR_CHECK_EQ(ParseFirmware(firmware.data(), firmware.size(), limits, nullptr,
		nullptr), FirmwareStatus::kBadBlockSize);
	firmware = MakeFirmware(1, 2, 4);
	JR_CHECK_EQ(ParseFirmware(firmware.data(), firmware.size(), limits, nullptr,
		nullptr), FirmwareStatus::kUnalignedDestination);
	firmware = MakeFirmware(1, 0, 6);
	JR_CHECK_EQ(ParseFirmware(firmware.data(), firmware.size(), limits, nullptr,
		nullptr), FirmwareStatus::kOk);
	JR_CHECK(CheckedRange(4, 4, 8));
	JR_CHECK(!CheckedRange(SIZE_MAX, 1, SIZE_MAX));
	JR_CHECK(!CheckedRange(7, 2, 8));
}


JR_TEST(byt_ipc, packs_mrfld_64_bit_header_fields)
{
	const uint64_t header = PackMrfldIpcHeader(0x1234,
		kMrfldIpcCommand, 3, 7, true, true, false, true);
	JR_CHECK_EQ(IpcPayloadSize(header), (uint32_t)0x1234);
	JR_CHECK_EQ(IpcMessageId(header), kMrfldIpcCommand);
	JR_CHECK_EQ(IpcTaskId(header), (uint8_t)3);
	JR_CHECK_EQ(IpcDriverId(header), (uint8_t)7);
	JR_CHECK_EQ(IpcResult(header), (uint8_t)0);
	JR_CHECK(IpcBusy(header));
	JR_CHECK(IpcLarge(header));
	JR_CHECK(!IpcDone(header));
	JR_CHECK_EQ(kMrfldFirmwareInitCommand, (uint16_t)0x01);
	JR_CHECK_EQ(kMrfldFirmwareAsyncError, (uint16_t)0x11);
	JR_CHECK_EQ(kMrfldAllocateStream, (uint16_t)0x02);
	JR_CHECK_EQ(kMrfldFreeStream, (uint16_t)0x03);
	JR_CHECK_EQ(kMrfldPauseStream, (uint16_t)0x04);
	JR_CHECK_EQ(kMrfldResumeStream, (uint16_t)0x05);
	JR_CHECK_EQ(kMrfldStartStream, (uint16_t)0x06);
	JR_CHECK_EQ(kMrfldDropStream, (uint16_t)0x07);
	JR_CHECK_EQ(kMrfldDrainStream, (uint16_t)0x08);
	JR_CHECK_EQ(kMrfldPeriodElapsed, (uint16_t)0x0a);
	JR_CHECK_EQ(kMrfldSetStreamParameters, (uint16_t)0x12);
	JR_CHECK_EQ(kMrfldSetGain, (uint16_t)0x21);
	JR_CHECK(kMrfldAllocateStream != 0x20);

	const MrfldDspHeader allocation = MakeMrfldDspHeader(
		kMrfldAllocateStream, 0x90, 100);
	JR_CHECK_EQ(allocation.moduleAndPipe, (uint16_t)0x90ff);
	JR_CHECK_EQ(allocation.commandId, (uint16_t)0x02);
	JR_CHECK_EQ(allocation.length, (uint16_t)100);

	const uint64_t byteStream = PackMrfldByteStreamHeader(48,
		kMrfldIpcSetParameters, 1, 9, true);
	JR_CHECK_EQ(IpcPayloadSize(byteStream), (uint32_t)48);
	JR_CHECK_EQ(IpcMessageId(byteStream), kMrfldIpcSetParameters);
	JR_CHECK(IpcBusy(byteStream));
	JR_CHECK(!IpcDone(byteStream));
	JR_CHECK((byteStream & (uint64_t{1} << (29 + 32))) != 0);
	JR_CHECK((byteStream & (uint64_t{1} << (28 + 32))) != 0);
	JR_CHECK_EQ((byteStream >> (12 + 32)) & 0xf, (uint64_t)9);

	uint8_t initMessage[48] = {};
	initMessage[4] = kMrfldFirmwareInitCommand;
	initMessage[8] = 0x01;
	initMessage[9] = 0x00;
	initMessage[10] = 0x0c;
	initMessage[11] = 0x01;
	initMessage[44] = 0x34;
	initMessage[45] = 0x12;
	MrfldFirmwareInitInfo init = {};
	JR_CHECK(DecodeMrfldFirmwareInit(initMessage, sizeof(initMessage), init));
	JR_CHECK_EQ(init.version[3], (uint8_t)0x01);
	JR_CHECK_EQ(init.version[2], (uint8_t)0x0c);
	JR_CHECK_EQ(init.version[1], (uint8_t)0x00);
	JR_CHECK_EQ(init.version[0], (uint8_t)0x01);
	JR_CHECK_EQ(init.result, (uint16_t)0x1234);
	initMessage[44] = 0;
	initMessage[45] = 0;
	JR_CHECK(DecodeMrfldFirmwareInit(initMessage,
		kMrfldFirmwareInitShortMessageSize, init));
	JR_CHECK_EQ(init.result, (uint16_t)0);
	initMessage[4] = 0xff;
	JR_CHECK(!DecodeMrfldFirmwareInit(initMessage, sizeof(initMessage), init));
	initMessage[4] = kMrfldFirmwareInitCommand;
	JR_CHECK(!DecodeMrfldFirmwareInit(initMessage,
		kMrfldFirmwareInitShortMessageSize - 1, init));
	JR_CHECK(!DecodeMrfldFirmwareInit(initMessage,
		kMrfldFirmwareInitShortMessageSize + 1, init));

	const uint64_t notification = PackMrfldIpcHeader(
		kMrfldFirmwareInitShortMessageSize,
		kMrfldIpcCommand, 3, kMrfldAsyncDriverId, false, true, false, true);
	JR_CHECK(IpcIsAsyncMessage(notification));
	JR_CHECK(!IpcIsProcessReply(notification));
	JR_CHECK_EQ(IpcTaskId(notification), (uint8_t)3);
	JR_CHECK_EQ(IpcDriverId(notification), kMrfldAsyncDriverId);
	const uint64_t acknowledge = AcknowledgeMrfldIpc(notification);
	JR_CHECK_EQ(IpcPayloadSize(acknowledge), (uint32_t)0);
	JR_CHECK(IpcDone(acknowledge));
	JR_CHECK(!IpcBusy(acknowledge));

	const uint64_t reply = PackMrfldIpcHeader(0, kMrfldIpcCommand, 3, 7,
		false, false, false, true);
	JR_CHECK(IpcMatchesReply(reply, kMrfldIpcCommand, 7));
	JR_CHECK(!IpcMatchesReply(reply, kMrfldIpcSetParameters, 7));
	JR_CHECK(!IpcMatchesReply(reply, kMrfldIpcCommand, 6));
}


JR_TEST(byt_boot, preserves_the_mrfld_reset_start_sequence)
{
	const uint64_t initial = uint64_t{1} << 25;
	const uint64_t resetAsserted = MrfldAssertReset(initial);
	JR_CHECK_EQ(resetAsserted, initial | (uint64_t)0x7);

	const uint64_t resetReleased = MrfldReleaseLpeReset(resetAsserted);
	JR_CHECK((resetReleased & kMrfldCsrLpeReset) == 0);
	JR_CHECK((resetReleased & kMrfldCsrResetVector) != 0);
	JR_CHECK((resetReleased & kMrfldCsrRunstall) != 0);

	const uint64_t startAsserted = MrfldAssertReset(resetReleased);
	JR_CHECK_EQ(startAsserted, initial | (uint64_t)0x7);
	const uint64_t running = MrfldReleaseForExecution(startAsserted);
	JR_CHECK((running & kMrfldCsrLpeReset) == 0);
	JR_CHECK((running & kMrfldCsrResetVector) != 0);
	JR_CHECK((running & kMrfldCsrRunstall) == 0);
	JR_CHECK((running & kMrfldCsrXtSnoop) != 0);
	JR_CHECK((running & initial) != 0);
}


JR_TEST(byt_profile, winky_ssp_and_stream_contract_is_explicit)
{
	const PlatformProfile& profile = kWinkyProfile;
	JR_CHECK_EQ(PlatformProfileCount(), (size_t)1);
	JR_CHECK_EQ(FindPlatformProfile("winky"), &profile);
	JR_CHECK_EQ(MatchLpeProfile("80860F28"), &profile);
	JR_CHECK_EQ(MatchCodecProfile("193C9890", 0x10), &profile);
	JR_CHECK_EQ(MatchCodecProfile("193C9890", 0x11),
		(const PlatformProfile*)nullptr);
	JR_CHECK_EQ(std::strcmp(profile.friendlyName, "Bay Trail MAX98090"), 0);
	JR_CHECK_EQ(profile.codecI2cAddress, (uint16_t)0x10);
	JR_CHECK_EQ(std::strcmp(profile.firmwareSubpath,
		"/firmware/byt_max98090/fw_sst_0f28.bin"), 0);
	JR_CHECK_EQ(profile.clock.pciVendorId, (uint16_t)0x8086);
	JR_CHECK_EQ(profile.clock.pciDeviceId, (uint16_t)0x0f1c);
	JR_CHECK_EQ(profile.clock.barOffset, (uint16_t)0x44);
	JR_CHECK_EQ(profile.clock.barMask, (uint32_t)0xfffffe00);
	JR_CHECK_EQ(profile.clock.mapSize, (uint32_t)0x100);
	JR_CHECK_EQ(profile.clock.registerOffset, (uint32_t)0x60);
	JR_CHECK_EQ(profile.clock.clearMask, (uint32_t)0x7);
	JR_CHECK_EQ(profile.clock.setBits, (uint32_t)0x5);
	JR_CHECK_EQ(profile.resources.lpeMemoryIndex, (uint32_t)0);
	JR_CHECK_EQ(profile.resources.imrMemoryIndex, (uint32_t)2);
	JR_CHECK_EQ(profile.resources.ipcIrqIndex, (uint32_t)5);
	JR_CHECK_EQ(profile.resources.expectedIpcIrq, (uint32_t)29);
	JR_CHECK_EQ(profile.resources.expectedImrBase, (uint64_t)0x20000000);
	JR_CHECK_EQ(profile.resources.expectedImrSize, (uint64_t)0x100000);
	JR_CHECK_EQ(profile.jack.headphoneResourceIndex, (uint32_t)0);
	JR_CHECK_EQ(profile.jack.microphoneResourceIndex, (uint32_t)1);
	JR_CHECK(!profile.jack.headphoneActiveLow);
	JR_CHECK(profile.jack.microphoneActiveLow);
	JR_CHECK_EQ(profile.jack.debounce, (int64_t)200000);
	JR_CHECK_EQ(profile.output.speakerVolumeMaximum, (uint8_t)15);
	JR_CHECK_EQ(profile.output.speakerVolumeDefault, (uint8_t)10);
	JR_CHECK_EQ(profile.output.headphoneVolumeMaximum, (uint8_t)19);
	JR_CHECK_EQ(profile.output.headphoneVolumeDefault, (uint8_t)19);
	JR_CHECK_EQ(profile.output.speakerMixerVolume, (uint8_t)2);

	const SstDspHeader& start = profile.playback.virtualBusStart;
	JR_CHECK_EQ(start.commandId, (uint16_t)85);
	JR_CHECK_EQ(start.length, (uint16_t)0);

	const SstSspCommand& ssp = profile.playback.sspConfiguration;
	JR_CHECK_EQ(ssp.header.commandId, (uint16_t)117);
	JR_CHECK_EQ(ssp.header.length, (uint16_t)18);
	JR_CHECK_EQ(ssp.selection, (uint16_t)3);
	JR_CHECK_EQ(ssp.switchState, (uint16_t)3);
	JR_CHECK_EQ(ssp.slotConfiguration & 0x3f, (uint16_t)16);
	JR_CHECK_EQ((ssp.slotConfiguration >> 6) & 0x0f, (uint16_t)2);
	JR_CHECK_EQ((ssp.slotConfiguration >> 10) & 0x07, (uint16_t)0);
	JR_CHECK_EQ((ssp.slotConfiguration >> 13) & 0x07, (uint16_t)0);
	JR_CHECK_EQ(ssp.activeTxSlotMap, (uint16_t)0xff03);
	JR_CHECK_EQ(ssp.activeRxSlotMap, (uint16_t)0xff03);
	JR_CHECK_EQ(ssp.frameSyncFrequency, (uint16_t)3);
	JR_CHECK_EQ(ssp.polarity, (uint16_t)1);
	JR_CHECK_EQ(ssp.frameSyncWidth, (uint16_t)16);
	JR_CHECK_EQ(ssp.protocolAndStartDelay, (uint16_t)0x0101);
	const SstSspSlotMapCommand& slotMap = profile.playback.sspSlotMap;
	JR_CHECK_EQ(slotMap.header.commandId, (uint16_t)130);
	JR_CHECK_EQ(slotMap.header.length, (uint16_t)22);
	JR_CHECK_EQ(slotMap.parameterId, (uint16_t)130);
	JR_CHECK_EQ(slotMap.parameterLength, (uint16_t)18);
	JR_CHECK_EQ(slotMap.selection, (uint16_t)3);
	for (size_t i = 0; i < 8; i++) {
		JR_CHECK_EQ(slotMap.receiveSlots[i], (uint8_t)(1u << i));
		JR_CHECK_EQ(slotMap.transmitSlots[i], (uint8_t)(1u << i));
	}
	JR_CHECK_EQ(profile.playback.mmxTaskId, (uint8_t)3);
	JR_CHECK_EQ(profile.playback.sbaTaskId, (uint8_t)1);
	JR_CHECK_EQ(kGainZeroDb, (int16_t)0);
	JR_CHECK_EQ(profile.playback.streamId, (uint8_t)1);
	JR_CHECK_EQ(profile.playback.pipeId, (uint8_t)0x90);
	JR_CHECK_EQ(kMrfldAllocationSize, (size_t)100);
	JR_CHECK_EQ(kMrfldTimestampStride, (uint32_t)76);
	JR_CHECK_EQ(MrfldTimestampOffset(1), (uint32_t)(0x800 + 76));
	JR_CHECK_EQ(MrfldTimestampAddress(profile.playback.mailboxLpeAddress, 1),
		(uint32_t)0xff34484c);
	JR_CHECK_EQ(offsetof(SstMrfldAllocation, ringBuffers), (size_t)4);
	JR_CHECK_EQ(offsetof(SstMrfldAllocation, fragmentSizeBytes), (size_t)68);
	JR_CHECK_EQ(offsetof(SstMrfldAllocation, timestampAddress), (size_t)72);
	JR_CHECK_EQ(offsetof(SstMrfldAllocation, codecParameters), (size_t)76);
	JR_CHECK_EQ(NormalizePeriodFrames(480), (uint32_t)480);
	JR_CHECK_EQ(NormalizePeriodFrames(1024), (uint32_t)960);
	JR_CHECK_EQ(NormalizePeriodCount(6), (int32_t)6);
	JR_CHECK_EQ(NormalizePeriodCount(3), (int32_t)4);
	JR_CHECK(FitsSstDmaRange(0, 1));
	JR_CHECK(FitsSstDmaRange(0xfffff000, 0x1000));
	JR_CHECK(!FitsSstDmaRange(0xfffff000, 0x1001));
	JR_CHECK(!FitsSstDmaRange(0x100000000ULL, 1));
}


JR_TEST(byt_codec, full_register_playback_program_is_exact)
{
	using namespace max98090;
	JR_CHECK_EQ(kSoftwareReset, (uint8_t)0x00);
	JR_CHECK_EQ(kSystemClock, (uint8_t)0x1b);
	JR_CHECK_EQ(kClockMode, (uint8_t)0x1c);
	JR_CHECK_EQ(kClockRatioNiMsb, (uint8_t)0x1d);
	JR_CHECK_EQ(kClockRatioNiLsb, (uint8_t)0x1e);
	JR_CHECK_EQ(kMasterMode, (uint8_t)0x21);
	JR_CHECK_EQ(kInterfaceFormat, (uint8_t)0x22);
	JR_CHECK_EQ(kTdmControl, (uint8_t)0x23);
	JR_CHECK_EQ(kTdmFormat, (uint8_t)0x24);
	JR_CHECK_EQ(kIoConfiguration, (uint8_t)0x25);
	JR_CHECK_EQ(kFilterConfiguration, (uint8_t)0x26);
	JR_CHECK_EQ(kDaiPlaybackLevel, (uint8_t)0x27);
	JR_CHECK_EQ(kHeadphoneControl, (uint8_t)0x2b);
	JR_CHECK_EQ(kLeftHeadphoneVolume, (uint8_t)0x2c);
	JR_CHECK_EQ(kRightHeadphoneVolume, (uint8_t)0x2d);
	JR_CHECK_EQ(kLeftSpeakerMixer, (uint8_t)0x2e);
	JR_CHECK_EQ(kRightSpeakerMixer, (uint8_t)0x2f);
	JR_CHECK_EQ(kSpeakerControl, (uint8_t)0x30);
	JR_CHECK_EQ(kLeftSpeakerVolume, (uint8_t)0x31);
	JR_CHECK_EQ(kRightSpeakerVolume, (uint8_t)0x32);
	JR_CHECK_EQ(kOutputEnable, (uint8_t)0x3f);
	JR_CHECK_EQ(kDeviceShutdown, (uint8_t)0x45);
	JR_CHECK_EQ(kRevision, (uint8_t)0xff);
	JR_CHECK_EQ(kReset, (uint8_t)0x80);
	JR_CHECK_EQ(kSystemClock19M2, (uint8_t)0x10);
	JR_CHECK_EQ(kConsumerClockRatio, (uint8_t)0x00);
	JR_CHECK_EQ(kMasterModeConsumer, (uint8_t)0x00);
	JR_CHECK_EQ(kInterfaceI2sS16Normal, (uint8_t)0x04);
	JR_CHECK_EQ(kTdmDisabled, (uint8_t)0x00);
	JR_CHECK_EQ(kIoPlayback, (uint8_t)0x01);
	JR_CHECK_EQ(kFilterMusicPlaybackDcBlock, (uint8_t)0xa0);
	JR_CHECK_EQ(kDaiPlaybackUnmutedUnity, (uint8_t)0x00);
	JR_CHECK(kSystemClock != 0x04);
	JR_CHECK((kSystemClock19M2 & 0x40) == 0);
	JR_CHECK_EQ(kLeftDacToLeftSpeaker, (uint8_t)0x01);
	JR_CHECK_EQ(kRightDacToRightSpeaker, (uint8_t)0x02);
	JR_CHECK_EQ(kSpeakerVolumeHardwareMaximum, (uint8_t)39);
	JR_CHECK_EQ(kHeadphoneVolumeHardwareMaximum, (uint8_t)31);
	JR_CHECK_EQ(kSpeakerVolumeRawMinimum
		+ kWinkyProfile.output.speakerVolumeDefault, (uint8_t)0x22);
	JR_CHECK_EQ(SpeakerControlValue(
		kWinkyProfile.output.speakerMixerVolume), (uint8_t)0x05);
	JR_CHECK_EQ(kDacAndSpeakerEnable, (uint8_t)0x33);
	JR_CHECK_EQ(kDacAndHeadphoneEnable, (uint8_t)0xc3);
	JR_CHECK_EQ(kWinkyProfile.output.headphoneVolumeDefault, (uint8_t)0x13);
	JR_CHECK_EQ(kHeadphoneMute, (uint8_t)0x80);
	JR_CHECK_EQ(kShutdownRelease, (uint8_t)0x80);
}


// ---------------------------------------------------------------------------
// Playback command byte content and order tests
// ---------------------------------------------------------------------------

JR_TEST(byt_playback, exact_allocation_body_100_bytes)
{
	const auto alloc = BuildAllocation(kWinkyProfile.playback.pcm,
		0x10000000, 15360, 3840, 0xff34484c);
	JR_CHECK_EQ(sizeof(alloc), (size_t)100);
	JR_CHECK_EQ(alloc.codecType, (uint16_t)1);
	JR_CHECK_EQ(alloc.operation, (uint8_t)0);
	JR_CHECK_EQ(alloc.scatterGatherCount, (uint8_t)1);
	JR_CHECK_EQ(alloc.ringBuffers[0].address, (uint32_t)0x10000000);
	JR_CHECK_EQ(alloc.ringBuffers[0].size, (uint32_t)15360);
	for (int i = 1; i < 8; i++) {
		JR_CHECK_EQ(alloc.ringBuffers[i].address, (uint32_t)0);
		JR_CHECK_EQ(alloc.ringBuffers[i].size, (uint32_t)0);
	}
	JR_CHECK_EQ(alloc.fragmentSizeBytes, (uint32_t)3840);
	JR_CHECK_EQ(alloc.timestampAddress, (uint32_t)0xff34484c);
	// Codec params: 2 chan, 16 bit, 48000 Hz
	JR_CHECK_EQ(alloc.codecParameters[0], (uint8_t)2);
	JR_CHECK_EQ(alloc.codecParameters[1], (uint8_t)16);
	JR_CHECK_EQ(alloc.codecParameters[2], (uint8_t)0);
	JR_CHECK_EQ(alloc.codecParameters[3], (uint8_t)0);
	JR_CHECK_EQ(alloc.codecParameters[4], (uint8_t)0x80);
	JR_CHECK_EQ(alloc.codecParameters[5], (uint8_t)0xbb);
	JR_CHECK_EQ(alloc.codecParameters[6], (uint8_t)0x00);
	JR_CHECK_EQ(alloc.codecParameters[7], (uint8_t)0x00);
	JR_CHECK_EQ(alloc.codecParameters[8], (uint8_t)0);
	JR_CHECK_EQ(alloc.codecParameters[9], (uint8_t)1);
	for (int i = 10; i < 16; i++)
		JR_CHECK_EQ(alloc.codecParameters[i], (uint8_t)0xff);
}


JR_TEST(byt_playback, gain_command_bytes)
{
	const auto gain = MakeMedia1Gain0dB();
	JR_CHECK_EQ(sizeof(gain), GainCommandSize());
	JR_CHECK_EQ(gain.header.cellIndex, (uint8_t)0xff);
	JR_CHECK_EQ(gain.header.pathId, (uint8_t)0xff);
	JR_CHECK_EQ(gain.header.moduleId, (uint16_t)0xffff);
	JR_CHECK_EQ(gain.header.commandId, (uint16_t)33);
	JR_CHECK_EQ(gain.cellCount, (uint16_t)1);
	JR_CHECK_EQ(gain.cells[0].cellIndex, (uint8_t)0);
	JR_CHECK_EQ(gain.cells[0].pathId, (uint8_t)0x90);
	JR_CHECK_EQ(gain.cells[0].moduleId, (uint16_t)0x0067);
	JR_CHECK_EQ(gain.cells[0].leftGain, (int16_t)0);
	JR_CHECK_EQ(gain.cells[0].rightGain, (int16_t)0);
	JR_CHECK_EQ(gain.cells[0].timeConstant, (uint16_t)5);
	JR_CHECK_EQ(gain.header.length,
		(uint16_t)(sizeof(SstGainCommand) - sizeof(SstByteStreamDspHeader)));

	const auto codecGain = MakeCodecOut0Gain0dB();
	JR_CHECK_EQ(codecGain.cells[0].pathId, (uint8_t)0x02);
	JR_CHECK_EQ(codecGain.cells[0].moduleId, (uint16_t)0x0067);

	const auto pcm0Gain = MakePcm0InputGain0dB();
	JR_CHECK_EQ(pcm0Gain.cells[0].pathId, (uint8_t)0x8d);
}


JR_TEST(byt_playback, swm_command_bytes)
{
	const auto swm = MakeMedia1ToMedia0Swm();
	JR_CHECK_EQ(swm.header.commandId, (uint16_t)114);
	JR_CHECK_EQ(swm.outputPathId, (uint8_t)0x12);
	JR_CHECK_EQ(swm.outputModuleId, (uint16_t)0xffff);
	JR_CHECK_EQ(swm.switchState, (uint16_t)3);
	JR_CHECK_EQ(swm.inputCount, (uint16_t)1);
	JR_CHECK_EQ(swm.inputs[0].pathId, (uint8_t)0x90);
	JR_CHECK_EQ(swm.inputs[0].moduleId, (uint16_t)0xffff);
	const size_t expectedSize = SwmCommandSize(1);
	JR_CHECK_EQ(swm.header.length + sizeof(SstByteStreamDspHeader),
		(uint16_t)expectedSize);

	const auto sbaSwm = MakePcm0ToCodecOut0Swm();
	JR_CHECK_EQ(sbaSwm.outputPathId, (uint8_t)0x02);
	JR_CHECK_EQ(sbaSwm.inputs[0].pathId, (uint8_t)0x8d);
}


JR_TEST(byt_playback, media_path_command_bytes)
{
	const auto media0 = MakeMedia0PathEnable();
	JR_CHECK_EQ(media0.header.pathId, (uint8_t)0x12);
	JR_CHECK_EQ(media0.header.commandId, (uint16_t)119);
	JR_CHECK_EQ(media0.switchState, (uint16_t)1);
	JR_CHECK_EQ(media0.header.length,
		(uint16_t)(sizeof(SstMediaPathCommand) - sizeof(SstByteStreamDspHeader)));

	const auto pcm0In = MakePcm0InputEnable();
	JR_CHECK_EQ(pcm0In.header.pathId, (uint8_t)0x8d);
}


JR_TEST(byt_playback, counter_to_cycle_frame_mapping)
{
	// 960 frames * 4 bytes/frame = 3840 bytes/period, 4 periods
	const uint32_t periodBytes = 3840;
	const int32_t periodCount = 4;

	JR_CHECK_EQ(PlaybackBufferCycle(0, periodBytes, periodCount),
		(uint32_t)0);
	JR_CHECK_EQ(PlaybackBufferCycle(3839, periodBytes, periodCount),
		(uint32_t)0);
	JR_CHECK_EQ(PlaybackBufferCycle(3840, periodBytes, periodCount),
		(uint32_t)1);
	JR_CHECK_EQ(PlaybackBufferCycle(7680, periodBytes, periodCount),
		(uint32_t)2);
	JR_CHECK_EQ(PlaybackBufferCycle(11520, periodBytes, periodCount),
		(uint32_t)3);
	JR_CHECK_EQ(PlaybackBufferCycle(15360, periodBytes, periodCount),
		(uint32_t)0);
	JR_CHECK_EQ(PlaybackBufferCycle(19200, periodBytes, periodCount),
		(uint32_t)1);

	JR_CHECK_EQ(PlayedFrames(0, 4), (uint64_t)0);
	JR_CHECK_EQ(PlayedFrames(3840, 4), (uint64_t)960);
	JR_CHECK_EQ(PlayedFrames(15360, 4), (uint64_t)3840);

	// Edge cases
	JR_CHECK_EQ(PlaybackBufferCycle(100, 0, 4), (uint32_t)0);
	JR_CHECK_EQ(PlaybackBufferCycle(100, 10, 0), (uint32_t)0);
	JR_CHECK_EQ(PlayedFrames(100, 0), (uint64_t)0);
}


JR_TEST(byt_playback, allocation_response_parsing)
{
	uint16_t result = 0xffff;
	JR_CHECK(ParseAllocationResult(nullptr, 0, result));
	JR_CHECK_EQ(result, (uint16_t)0);

	// The response begins directly with the 8-byte snd_sst_str_type.
	uint8_t resp[8] = {};
	JR_CHECK(ParseAllocationResult(resp, sizeof(resp), result));
	JR_CHECK_EQ(result, (uint16_t)0);

	// Result = 15 (SST_ERR_STREAM_IN_USE)
	resp[6] = 15;
	JR_CHECK(ParseAllocationResult(resp, sizeof(resp), result));
	JR_CHECK_EQ(result, (uint16_t)15);

	// Too small
	JR_CHECK(!ParseAllocationResult(resp, sizeof(resp) - 1, result));
	JR_CHECK(!ParseAllocationResult(nullptr, sizeof(resp), result));

	JR_CHECK_EQ(kMrfldAsyncDriverId, (uint8_t)0);
	JR_CHECK_EQ(kMrfldSerializedDriverId, (uint8_t)1);
	JR_CHECK(ShouldRecoverStaleAllocation(15, 0));
	JR_CHECK(!ShouldRecoverStaleAllocation(15, 1));
	JR_CHECK(!ShouldRecoverStaleAllocation(1, 0));
	JR_CHECK(AcceptFixedPlaybackChannelMask(0x03));
	JR_CHECK(!AcceptFixedPlaybackChannelMask(0x00));
	JR_CHECK(!AcceptFixedPlaybackChannelMask(0x01));
	JR_CHECK(!AcceptFixedPlaybackChannelMask(0x07));
}


JR_TEST(byt_playback, command_order_and_ipc_categories)
{
	// Verify the Linux DAPM route commands:
	// VB start=85 (CMD/SBA), SSP=117 (CMD/SBA), slot map=130 (SET_PARAMS/SBA),
	// media1 gain (SET_PARAMS/MMX), MMX SWM (CMD/MMX), media0 path (CMD/MMX),
	// pcm0 input (CMD/SBA), pcm0 gain (SET_PARAMS/SBA), SBA SWM (CMD/SBA),
	// codec_out0 gain (SET_PARAMS/SBA). The codec AIF does not receive a
	// SET_MEDIA_PATH command.
	JR_CHECK_EQ(kPlaybackCommandCount, (size_t)10);
	JR_CHECK_EQ(kPlaybackHardwareCommandCount, (size_t)2);
	JR_CHECK_EQ(kPlaybackRouteCommandCount, (size_t)8);

	const auto& vbStart = kWinkyProfile.playback.virtualBusStart;
	JR_CHECK_EQ(vbStart.commandId, (uint16_t)85);

	const auto& ssp = kWinkyProfile.playback.sspConfiguration;
	JR_CHECK_EQ(ssp.header.commandId, (uint16_t)117);

	const auto& slotMap = kWinkyProfile.playback.sspSlotMap;
	JR_CHECK_EQ(slotMap.header.commandId, (uint16_t)130);

	const auto media1Gain = MakeMedia1Gain0dB();
	JR_CHECK_EQ(media1Gain.header.commandId, (uint16_t)33);

	const auto mmxSwm = MakeMedia1ToMedia0Swm();
	JR_CHECK_EQ(mmxSwm.header.commandId, (uint16_t)114);

	const auto media0Path = MakeMedia0PathEnable();
	JR_CHECK_EQ(media0Path.header.commandId, (uint16_t)119);

	const auto pcm0In = MakePcm0InputEnable();
	JR_CHECK_EQ(pcm0In.header.commandId, (uint16_t)119);

	const auto pcm0Gain = MakePcm0InputGain0dB();
	JR_CHECK_EQ(pcm0Gain.header.commandId, (uint16_t)33);

	const auto sbaSwm = MakePcm0ToCodecOut0Swm();
	JR_CHECK_EQ(sbaSwm.header.commandId, (uint16_t)114);

	const auto codecOut0Gain = MakeCodecOut0Gain0dB();
	JR_CHECK_EQ(codecOut0Gain.header.commandId, (uint16_t)33);

	// IPC message categories
	JR_CHECK_EQ(kIpcCmd, (uint8_t)1);
	JR_CHECK_EQ(kIpcSetParams, (uint8_t)2);
}


JR_TEST(byt_playback, byte_stream_header_constants)
{
	JR_CHECK_EQ(kDefaultCellIndex, (uint8_t)0xff);
	JR_CHECK_EQ(kGainCellIndex, (uint8_t)0);
	JR_CHECK_EQ(kDefaultModuleId, (uint16_t)0xffff);
	JR_CHECK_EQ(kGainCellModuleId, (uint16_t)0x0067);
	JR_CHECK_EQ(kGainTimeConstant, (uint16_t)5);
	JR_CHECK_EQ(kPathCodecOut0, (uint8_t)0x02);
	JR_CHECK_EQ(kPathPcm0Out, (uint8_t)0x0d);
	JR_CHECK_EQ(kPathMedia0Out, (uint8_t)0x12);
	JR_CHECK_EQ(kPathPcm0In, (uint8_t)0x8d);
	JR_CHECK_EQ(kPathMedia0In, (uint8_t)0x8f);
	JR_CHECK_EQ(kPathMedia1In, (uint8_t)0x90);
	JR_CHECK_EQ(kCmdSetGain, (uint16_t)33);
	JR_CHECK_EQ(kCmdSetSwm, (uint16_t)114);
	JR_CHECK_EQ(kCmdSetMediaPath, (uint16_t)119);
}


JR_TEST(byt_driver, module_names_follow_packaged_path)
{
	constexpr const char* kPackageModulePrefix
		= "drivers/audio/hmulti/byt_max98090/";
	const size_t prefixLength = std::strlen(kPackageModulePrefix);

	JR_CHECK_EQ(std::strncmp(kLpeDriverModule, kPackageModulePrefix, prefixLength),
		0);
	JR_CHECK_EQ(std::strncmp(kCodecDriverModule, kPackageModulePrefix,
		prefixLength), 0);
	JR_CHECK_EQ(std::strncmp(kDeviceModule, kPackageModulePrefix, prefixLength),
		0);
}
