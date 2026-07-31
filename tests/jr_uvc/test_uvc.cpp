// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot (GPT-5.6 Sol)

#include "WinkyFixture.h"
#include "../framework/jr_test.h"

#include <algorithm>


using namespace jr::uvc;


JR_TEST(jr_uvc, parses_complete_winky_catalog)
{
	DescriptorModel model;
	std::string error;
	JR_CHECK(ParseDescriptors(jr::uvc::test::WinkyDescriptors(), model, error));
	JR_CHECK_EQ(model.uvcVersion, 0x0100);
	JR_CHECK_EQ(model.videoControlTotalLength, 78);
	JR_CHECK_EQ(model.streamingHeader.totalLength, 1198);
	JR_CHECK_EQ(model.streamingHeader.endpointAddress, 0x81);
	JR_CHECK(model.processingUnit.valid);
	JR_CHECK_EQ(model.processingUnit.controls.size(), 2);
	JR_CHECK_EQ(model.processingUnit.processingIndex, 0);
	JR_CHECK_EQ(model.processingUnit.videoStandards, 0);
	JR_CHECK(model.warnings.empty());
	JR_CHECK_EQ(model.formats.size(), 3);
	JR_CHECK_EQ(model.formats[0].kind, FormatKind::kYuy2);
	JR_CHECK_EQ(model.formats[1].kind, FormatKind::kMjpeg);
	JR_CHECK_EQ(model.formats[2].kind, FormatKind::kUnsupported);
	JR_CHECK_EQ(model.formats[0].frames.size(), 10);
	JR_CHECK_EQ(model.formats[1].frames.size(), 10);
	JR_CHECK_EQ(model.formats[2].frames.size(), 10);
	JR_CHECK_EQ(model.modes.size(), 41);
	JR_CHECK(std::all_of(model.modes.begin(), model.modes.end(),
		[](const Mode& mode) { return mode.kind != FormatKind::kUnsupported; }));
	for (size_t index = 1; index < model.modes.size(); index++) {
		JR_CHECK_NE(model.modes[index - 1].id, model.modes[index].id);
		JR_CHECK(model.modes[index - 1].estimatedBitsPerSecond
			<= model.modes[index].estimatedBitsPerSecond);
	}
	JR_CHECK(model.modes.front().estimatedBitsPerSecond
		<= model.modes.back().estimatedBitsPerSecond);
	JR_CHECK_EQ(model.unknown.size(), 11);
	JR_CHECK(Hex(model.unknown.front().raw.data(),
		model.unknown.front().raw.size()).find("4d343230") != std::string::npos);
}


JR_TEST(jr_uvc, preserves_short_optional_processing_metadata)
{
	std::vector<DescriptorRecord> records = jr::uvc::test::WinkyDescriptors();
	records[1].bytes.resize(10);
	records[1].bytes[0] = 10;

	DescriptorModel model;
	std::string error;
	JR_CHECK(ParseDescriptors(records, model, error));
	JR_CHECK(!model.processingUnit.valid);
	JR_CHECK_EQ(model.warnings.size(), 1);
	JR_CHECK_EQ(model.warnings[0], "short_processing_unit_descriptor");
	JR_CHECK_EQ(model.modes.size(), 41);
	JR_CHECK(!model.unknown.empty());
}


JR_TEST(jr_uvc, parses_continuous_frame_intervals)
{
	std::vector<DescriptorRecord> records;
	records.push_back(jr::uvc::test::Record(0, 1,
		{0x0d, 0x24, 0x01, 0x00, 0x01, 0x0d, 0x00, 0x80, 0x8d,
			0x5b, 0x00, 0x01, 0x01}));
	records.push_back(jr::uvc::test::Record(1, 2,
		{0x0e, 0x24, 0x01, 0x01, 0x4c, 0x00, 0x81, 0x00, 0x03,
			0x00, 0x00, 0x00, 0x01, 0x00}));
	records.push_back(jr::uvc::test::Record(1, 2,
		{0x1b, 0x24, 0x04, 0x01, 0x01,
			0x59, 0x55, 0x59, 0x32, 0x00, 0x00, 0x10, 0x00,
			0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71,
			0x10, 0x01, 0x00, 0x00, 0x00, 0x00}));
	std::vector<uint8_t> frame(38);
	frame[0] = 38;
	frame[1] = 0x24;
	frame[2] = 0x05;
	frame[3] = 1;
	WriteLE16(&frame[5], 320);
	WriteLE16(&frame[7], 240);
	WriteLE32(&frame[17], 153600);
	WriteLE32(&frame[21], 666666);
	frame[25] = 0;
	WriteLE32(&frame[26], 333333);
	WriteLE32(&frame[30], 1000000);
	WriteLE32(&frame[34], 333333);
	records.push_back(jr::uvc::test::Record(1, 2, std::move(frame)));

	DescriptorModel model;
	std::string error;
	JR_CHECK(ParseDescriptors(records, model, error));
	JR_CHECK(model.formats[0].frames[0].continuous);
	JR_CHECK_EQ(model.formats[0].frames[0].minInterval, 333333);
	JR_CHECK_EQ(model.formats[0].frames[0].maxInterval, 1000000);
	JR_CHECK_EQ(model.formats[0].frames[0].intervalStep, 333333);
	JR_CHECK_EQ(model.modes.size(), 1);
	JR_CHECK_EQ(model.modes[0].interval, 666666);
}


JR_TEST(jr_uvc, decodes_all_payload_alternates)
{
	const uint16_t raw[] = {0x0080, 0x0200, 0x0400, 0x0b00, 0x0c00, 0x1380,
		0x1400};
	const uint32_t effective[] = {128, 512, 1024, 1536, 2048, 2688, 3072};
	for (uint8_t index = 0; index < 7; index++) {
		const Alternate alternate = DecodeAlternate(index + 1, index + 1, 0x81,
			raw[index]);
		JR_CHECK_EQ(alternate.effectiveBytesPerMicroframe, effective[index]);
		JR_CHECK_EQ(alternate.multiplier,
			static_cast<uint8_t>(((raw[index] >> 11) & 3) + 1));
	}
}


JR_TEST(jr_uvc, selects_safe_single_transaction_fallback)
{
	std::vector<Alternate> alternates;
	const uint16_t raw[] = {0x0080, 0x0200, 0x0400, 0x0b00, 0x0c00, 0x1380,
		0x1400};
	for (uint8_t index = 0; index < 7; index++)
		alternates.push_back(DecodeAlternate(index + 1, index + 1, 0x81,
			raw[index]));

	AlternateSelection safe = SelectAlternate(alternates, 0x81, 3072, false);
	JR_CHECK(safe.found);
	JR_CHECK(safe.highBandwidthAvoided);
	JR_CHECK_EQ(alternates[safe.vectorIndex].alternateValue, 3);
	JR_CHECK_EQ(alternates[safe.vectorIndex].effectiveBytesPerMicroframe, 1024);

	AlternateSelection native = SelectAlternate(alternates, 0x81, 3072, true);
	JR_CHECK(native.found);
	JR_CHECK(!native.highBandwidthAvoided);
	JR_CHECK_EQ(alternates[native.vectorIndex].alternateValue, 7);
}


JR_TEST(jr_uvc, probe_round_trip_and_fixups)
{
	ProbeControl source;
	source.hint = 1;
	source.formatIndex = 2;
	source.frameIndex = 10;
	source.frameInterval = 333333;
	source.keyFrameRate = 3;
	source.pFrameRate = 4;
	source.compressionQuality = 5;
	source.compressionWindowSize = 6;
	source.delay = 7;
	source.maxVideoFrameSize = 0;
	source.maxPayloadTransferSize = 0xffff0c00;
	const auto bytes = EncodeProbe(source);
	ProbeControl decoded;
	JR_CHECK(DecodeProbe(bytes.data(), bytes.size(), decoded));
	JR_CHECK_EQ(decoded.frameInterval, 333333);
	JR_CHECK_EQ(decoded.formatIndex, 2);
	JR_CHECK_EQ(decoded.frameIndex, 10);
	const ProbeFixups fixups = ApplyProbeFixups(decoded, 1843200);
	JR_CHECK(fixups.payloadSignExtended);
	JR_CHECK(fixups.maxFrameFallback);
	JR_CHECK_EQ(decoded.maxPayloadTransferSize, 3072);
	JR_CHECK_EQ(decoded.maxVideoFrameSize, 1843200);
	Mode mode;
	mode.formatIndex = 2;
	mode.frameIndex = 10;
	mode.interval = 333333;
	JR_CHECK(ProbeMatchesMode(decoded, mode));
	decoded.frameIndex = 1;
	JR_CHECK(!ProbeMatchesMode(decoded, mode));
}


JR_TEST(jr_uvc, reassembles_fixed_request_slots)
{
	uint8_t transfer[16] = {
		2, 0, 'a', 'b', 'c', 0xee, 0xee, 0xee,
		2, 2, 'd', 'e', 0xee, 0xee, 0xee, 0xee
	};
	PayloadReassembler assembler(16);
	JR_CHECK_EQ(assembler.AddPacket(transfer + 0 * 8, 5),
		PacketResult::kAccepted);
	JR_CHECK_EQ(assembler.AddPacket(transfer + 1 * 8, 4),
		PacketResult::kFrameComplete);
	JR_CHECK_EQ(std::string(assembler.CompletedFrame().begin(),
		assembler.CompletedFrame().end()), "abcde");
	JR_CHECK_EQ(assembler.Stats().completedFrames, 1);
	JR_CHECK_EQ(assembler.Stats().payloadBytes, 5);
}


JR_TEST(jr_uvc, handles_fid_error_invalid_and_overflow)
{
	PayloadReassembler assembler(3);
	const uint8_t first[] = {2, 0, 'a', 'b'};
	const uint8_t toggle[] = {2, 1, 'c'};
	const uint8_t error[] = {2, 0x41, 'x'};
	const uint8_t invalid[] = {5, 0, 'x'};
	const uint8_t overflow[] = {2, 1, '1', '2', '3', '4'};
	JR_CHECK_EQ(assembler.AddPacket(first, sizeof(first)),
		PacketResult::kAccepted);
	JR_CHECK_EQ(assembler.AddPacket(toggle, sizeof(toggle)),
		PacketResult::kFrameComplete);
	JR_CHECK_EQ(std::string(assembler.CompletedFrame().begin(),
		assembler.CompletedFrame().end()), "ab");
	JR_CHECK_EQ(assembler.AddPacket(error, sizeof(error)),
		PacketResult::kPayloadError);
	JR_CHECK_EQ(assembler.AddPacket(invalid, sizeof(invalid)),
		PacketResult::kInvalidHeader);
	JR_CHECK_EQ(assembler.AddPacket(overflow, sizeof(overflow)),
		PacketResult::kOverflow);
	JR_CHECK_EQ(assembler.Stats().fidToggles, 1);
	JR_CHECK_EQ(assembler.Stats().errorPackets, 1);
	JR_CHECK_EQ(assembler.Stats().invalidHeaders, 1);
	JR_CHECK_EQ(assembler.Stats().overflows, 1);
}


JR_TEST(jr_uvc, transport_and_header_errors_poison_the_current_frame)
{
	PayloadReassembler assembler(16);
	const uint8_t first[] = {2, 0, 'a', 'b'};
	const uint8_t end[] = {2, 2, 'c', 'd'};
	JR_CHECK_EQ(assembler.AddPacket(first, sizeof(first)),
		PacketResult::kAccepted);
	assembler.NoteTransportError();
	JR_CHECK_EQ(assembler.AddPacket(end, sizeof(end)), PacketResult::kAccepted);
	JR_CHECK(assembler.CompletedFrame().empty());
	JR_CHECK_EQ(assembler.Stats().transportErrors, 1);

	assembler.Reset();
	JR_CHECK_EQ(assembler.AddPacket(first, sizeof(first)),
		PacketResult::kAccepted);
	const uint8_t malformed[] = {5, 0, 'x'};
	JR_CHECK_EQ(assembler.AddPacket(malformed, sizeof(malformed)),
		PacketResult::kInvalidHeader);
	JR_CHECK_EQ(assembler.AddPacket(end, sizeof(end)), PacketResult::kAccepted);
	JR_CHECK(assembler.CompletedFrame().empty());
}


JR_TEST(jr_uvc, preserves_completed_frame_when_next_fid_is_error)
{
	PayloadReassembler assembler(8);
	const uint8_t first[] = {2, 0, 'o', 'k'};
	const uint8_t errorToggle[] = {2, 0x43};
	JR_CHECK_EQ(assembler.AddPacket(first, sizeof(first)),
		PacketResult::kAccepted);
	JR_CHECK_EQ(assembler.AddPacket(errorToggle, sizeof(errorToggle)),
		PacketResult::kFrameComplete);
	JR_CHECK_EQ(std::string(assembler.CompletedFrame().begin(),
		assembler.CompletedFrame().end()), "ok");
	JR_CHECK_EQ(assembler.Stats().completedFrames, 1);
	JR_CHECK_EQ(assembler.Stats().errorPackets, 1);
	JR_CHECK_EQ(assembler.Stats().eofPackets, 1);
}


JR_TEST(jr_uvc, validates_yuy2_and_mjpeg)
{
	const uint8_t yuy2[8] = {};
	JR_CHECK(ValidateYuy2(yuy2, sizeof(yuy2), 2, 2).valid);
	JR_CHECK_EQ(ValidateYuy2(yuy2, 7, 2, 2).status,
		ValidationStatus::kSizeMismatch);
	const uint8_t jpeg[] = {
		0xff, 0xd8, 0xff, 0xc0, 0x00, 0x04, 0, 0, 0xff, 0xd9
	};
	const ValidationResult valid = ValidateMjpeg(jpeg, sizeof(jpeg), 20);
	JR_CHECK(valid.valid);
	JR_CHECK(valid.hasSoi);
	JR_CHECK(valid.hasSof);
	JR_CHECK(valid.hasEoi);
	JR_CHECK(!valid.warning);
	const ValidationResult warning = ValidateMjpeg(jpeg, sizeof(jpeg) - 2, 20);
	JR_CHECK(warning.valid);
	JR_CHECK(warning.warning);
	JR_CHECK_EQ(warning.status, ValidationStatus::kPassEoiMissing);
	JR_CHECK_EQ(ValidateMjpeg(jpeg + 1, sizeof(jpeg) - 1, 20).status,
		ValidationStatus::kMissingSoi);
}


JR_TEST(jr_uvc, escapes_json_control_characters)
{
	JR_CHECK_EQ(JsonEscape("\"\\\n\t\x01"), "\\\"\\\\\\n\\t\\u0001");
	JR_CHECK_EQ(JsonEscape("camera"), "camera");
}
