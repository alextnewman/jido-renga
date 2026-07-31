// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot (GPT-5.6 Sol)

#include "UvcCore.h"

#include <algorithm>
#include <cstdio>
#include <limits>


namespace jr::uvc {

namespace {

constexpr uint8_t kClassVideo = 0x0e;
constexpr uint8_t kSubclassControl = 0x01;
constexpr uint8_t kSubclassStreaming = 0x02;
constexpr uint8_t kClassSpecificInterface = 0x24;
constexpr uint8_t kVcHeader = 0x01;
constexpr uint8_t kVcProcessingUnit = 0x05;
constexpr uint8_t kVsInputHeader = 0x01;
constexpr uint8_t kVsFormatUncompressed = 0x04;
constexpr uint8_t kVsFrameUncompressed = 0x05;
constexpr uint8_t kVsFormatMjpeg = 0x06;
constexpr uint8_t kVsFrameMjpeg = 0x07;
constexpr uint8_t kVsColorFormat = 0x0d;

template<typename T>
bool
AddBounded(std::vector<T>& values, T value, size_t maximum, bool& truncated)
{
	if (values.size() >= maximum) {
		truncated = true;
		return false;
	}
	values.push_back(std::move(value));
	return true;
}


bool
IsYuy2(const uint8_t* guid)
{
	static constexpr uint8_t kYuy2Guid[16] = {
		'Y', 'U', 'Y', '2', 0x00, 0x00, 0x10, 0x00,
		0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71
	};
	return std::equal(guid, guid + 16, kYuy2Guid);
}


void
SaveUnknown(const DescriptorRecord& record, DescriptorModel& model)
{
	UnknownDescriptor descriptor;
	descriptor.interfaceNumber = record.interfaceNumber;
	descriptor.alternateValue = record.alternateValue;
	descriptor.type = record.bytes.size() > 1 ? record.bytes[1] : 0;
	descriptor.subtype = record.bytes.size() > 2 ? record.bytes[2] : 0;
	descriptor.raw = record.bytes;
	AddBounded(model.unknown, std::move(descriptor), kMaxDescriptors,
		model.truncated);
}


bool
ParseFrame(const DescriptorRecord& record, FormatDescriptor& format,
	DescriptorModel& model)
{
	const std::vector<uint8_t>& bytes = record.bytes;
	if (bytes.size() < 26)
		return false;

	FrameDescriptor frame;
	frame.index = bytes[3];
	frame.capabilities = bytes[4];
	frame.width = ReadLE16(&bytes[5]);
	frame.height = ReadLE16(&bytes[7]);
	frame.minBitRate = ReadLE32(&bytes[9]);
	frame.maxBitRate = ReadLE32(&bytes[13]);
	frame.maxFrameBufferSize = ReadLE32(&bytes[17]);
	frame.defaultInterval = ReadLE32(&bytes[21]);
	const uint8_t intervalType = bytes[25];
	if (intervalType == 0) {
		if (bytes.size() < 38)
			return false;
		frame.continuous = true;
		frame.minInterval = ReadLE32(&bytes[26]);
		frame.maxInterval = ReadLE32(&bytes[30]);
		frame.intervalStep = ReadLE32(&bytes[34]);
	} else {
		if (intervalType > kMaxIntervalsPerFrame
			|| bytes.size() < 26 + static_cast<size_t>(intervalType) * 4) {
			return false;
		}
		for (uint8_t index = 0; index < intervalType; index++)
			frame.intervals.push_back(ReadLE32(&bytes[26 + index * 4]));
	}
	return AddBounded(format.frames, std::move(frame), kMaxFramesPerFormat,
		model.truncated);
}


std::string
ModeId(FormatKind kind, uint8_t formatIndex, uint8_t frameIndex,
	uint32_t interval)
{
	char id[80];
	std::snprintf(id, sizeof(id), "%s-fmt%02u-frame%02u-int%08u",
		FormatKindName(kind), formatIndex, frameIndex, interval);
	return id;
}


bool
IsSofMarker(uint8_t marker)
{
	return (marker >= 0xc0 && marker <= 0xc3)
		|| (marker >= 0xc5 && marker <= 0xc7)
		|| (marker >= 0xc9 && marker <= 0xcb)
		|| (marker >= 0xcd && marker <= 0xcf);
}

} // namespace


uint16_t
ReadLE16(const uint8_t* bytes)
{
	return static_cast<uint16_t>(bytes[0])
		| static_cast<uint16_t>(bytes[1]) << 8;
}


uint32_t
ReadLE32(const uint8_t* bytes)
{
	return static_cast<uint32_t>(bytes[0])
		| static_cast<uint32_t>(bytes[1]) << 8
		| static_cast<uint32_t>(bytes[2]) << 16
		| static_cast<uint32_t>(bytes[3]) << 24;
}


void
WriteLE16(uint8_t* bytes, uint16_t value)
{
	bytes[0] = static_cast<uint8_t>(value);
	bytes[1] = static_cast<uint8_t>(value >> 8);
}


void
WriteLE32(uint8_t* bytes, uint32_t value)
{
	bytes[0] = static_cast<uint8_t>(value);
	bytes[1] = static_cast<uint8_t>(value >> 8);
	bytes[2] = static_cast<uint8_t>(value >> 16);
	bytes[3] = static_cast<uint8_t>(value >> 24);
}


std::string
Hex(const uint8_t* bytes, size_t length)
{
	static constexpr char kDigits[] = "0123456789abcdef";
	std::string result;
	result.reserve(length * 2);
	for (size_t index = 0; index < length; index++) {
		result.push_back(kDigits[bytes[index] >> 4]);
		result.push_back(kDigits[bytes[index] & 0x0f]);
	}
	return result;
}


std::string
JsonEscape(const std::string& value)
{
	std::string result;
	for (unsigned char byte : value) {
		switch (byte) {
			case '"': result += "\\\""; break;
			case '\\': result += "\\\\"; break;
			case '\b': result += "\\b"; break;
			case '\f': result += "\\f"; break;
			case '\n': result += "\\n"; break;
			case '\r': result += "\\r"; break;
			case '\t': result += "\\t"; break;
			default:
				if (byte < 0x20) {
					char escaped[7];
					std::snprintf(escaped, sizeof(escaped), "\\u%04x", byte);
					result += escaped;
				} else {
					result.push_back(static_cast<char>(byte));
				}
		}
	}
	return result;
}


const char*
FormatKindName(FormatKind kind)
{
	switch (kind) {
		case FormatKind::kYuy2: return "yuy2";
		case FormatKind::kMjpeg: return "mjpeg";
		case FormatKind::kUnsupported: return "unsupported";
	}
	return "unsupported";
}


bool
ParseDescriptors(const std::vector<DescriptorRecord>& records,
	DescriptorModel& model, std::string& error)
{
	model = DescriptorModel();
	error.clear();
	FormatDescriptor* currentFormat = nullptr;

	for (const DescriptorRecord& record : records) {
		if (record.bytes.size() < 3
			|| record.bytes.size() > kMaxRawDescriptorBytes
			|| record.bytes[0] != record.bytes.size()) {
			error = "invalid descriptor length";
			return false;
		}
		if (record.interfaceClass == kClassVideo) {
			if (record.interfaceSubclass == kSubclassControl) {
				model.interfaces.hasVideoControl = true;
				model.interfaces.videoControlInterface = record.interfaceNumber;
			} else if (record.interfaceSubclass == kSubclassStreaming) {
				model.interfaces.hasVideoStreaming = true;
				model.interfaces.videoStreamingInterface = record.interfaceNumber;
			}
		}
		if (record.bytes[1] != kClassSpecificInterface) {
			SaveUnknown(record, model);
			continue;
		}

		const uint8_t subtype = record.bytes[2];
		if (record.interfaceSubclass == kSubclassControl && subtype == kVcHeader
			&& record.bytes.size() >= 12) {
			model.uvcVersion = ReadLE16(&record.bytes[3]);
			model.videoControlTotalLength = ReadLE16(&record.bytes[5]);
			model.clockFrequency = ReadLE32(&record.bytes[7]);
			continue;
		}
		if (record.interfaceSubclass == kSubclassControl
			&& subtype == kVcProcessingUnit && record.bytes.size() >= 8) {
			const uint8_t controlSize = record.bytes[7];
			const size_t requiredLength = static_cast<size_t>(
				(model.uvcVersion >= 0x0110 ? 10 : 9) + controlSize);
			if (record.bytes.size() < requiredLength) {
				model.warnings.push_back("short_processing_unit_descriptor");
				SaveUnknown(record, model);
				continue;
			}
			model.processingUnit.valid = true;
			model.processingUnit.unitId = record.bytes[3];
			model.processingUnit.sourceId = record.bytes[4];
			model.processingUnit.maxMultiplier = ReadLE16(&record.bytes[5]);
			model.processingUnit.controls.assign(record.bytes.begin() + 8,
				record.bytes.begin() + 8 + controlSize);
			model.processingUnit.processingIndex = record.bytes[8 + controlSize];
			if (model.uvcVersion >= 0x0110)
				model.processingUnit.videoStandards = record.bytes[9 + controlSize];
			continue;
		}
		if (record.interfaceSubclass != kSubclassStreaming) {
			SaveUnknown(record, model);
			continue;
		}

		if (subtype == kVsInputHeader && record.bytes.size() >= 13) {
			VsInputHeader& header = model.streamingHeader;
			header.valid = true;
			header.formatCount = record.bytes[3];
			header.totalLength = ReadLE16(&record.bytes[4]);
			header.endpointAddress = record.bytes[6];
			header.info = record.bytes[7];
			header.terminalLink = record.bytes[8];
			header.stillCaptureMethod = record.bytes[9];
			header.triggerSupport = record.bytes[10];
			header.triggerUsage = record.bytes[11];
			header.controlSize = record.bytes[12];
			const size_t controls = static_cast<size_t>(header.formatCount)
				* header.controlSize;
			if (record.bytes.size() < 13 + controls) {
				error = "short VS input header";
				return false;
			}
			header.controls.assign(record.bytes.begin() + 13,
				record.bytes.begin() + 13 + controls);
		} else if (subtype == kVsFormatUncompressed
			&& record.bytes.size() >= 27) {
			FormatDescriptor format;
			format.descriptorSubtype = subtype;
			format.index = record.bytes[3];
			format.declaredFrameCount = record.bytes[4];
			std::copy(record.bytes.begin() + 5, record.bytes.begin() + 21,
				format.guid.begin());
			format.kind = IsYuy2(format.guid.data())
				? FormatKind::kYuy2 : FormatKind::kUnsupported;
			format.bitsPerPixel = record.bytes[21];
			format.defaultFrameIndex = record.bytes[22];
			format.aspectX = record.bytes[23];
			format.aspectY = record.bytes[24];
			format.interlaceFlags = record.bytes[25];
			format.copyProtect = record.bytes[26];
			format.raw = record.bytes;
			if (AddBounded(model.formats, std::move(format), kMaxFormats,
					model.truncated)) {
				currentFormat = &model.formats.back();
			}
		} else if (subtype == kVsFormatMjpeg && record.bytes.size() >= 11) {
			FormatDescriptor format;
			format.kind = FormatKind::kMjpeg;
			format.descriptorSubtype = subtype;
			format.index = record.bytes[3];
			format.declaredFrameCount = record.bytes[4];
			format.defaultFrameIndex = record.bytes[6];
			format.aspectX = record.bytes[7];
			format.aspectY = record.bytes[8];
			format.interlaceFlags = record.bytes[9];
			format.copyProtect = record.bytes[10];
			format.raw = record.bytes;
			if (AddBounded(model.formats, std::move(format), kMaxFormats,
					model.truncated)) {
				currentFormat = &model.formats.back();
			}
		} else if ((subtype == kVsFrameUncompressed
				|| subtype == kVsFrameMjpeg) && currentFormat != nullptr) {
			if (!ParseFrame(record, *currentFormat, model)) {
				error = "invalid frame descriptor";
				return false;
			}
		} else if (subtype == kVsColorFormat && record.bytes.size() >= 6) {
			ColorMatching color;
			color.interfaceNumber = record.interfaceNumber;
			color.colorPrimaries = record.bytes[3];
			color.transferCharacteristics = record.bytes[4];
			color.matrixCoefficients = record.bytes[5];
			AddBounded(model.colors, std::move(color), kMaxFormats,
				model.truncated);
		} else {
			if (subtype == 0x0e && record.bytes.size() >= 5) {
				FormatDescriptor format;
				format.kind = FormatKind::kUnsupported;
				format.descriptorSubtype = subtype;
				format.index = record.bytes[3];
				format.declaredFrameCount = record.bytes[4];
				format.raw = record.bytes;
				if (AddBounded(model.formats, std::move(format), kMaxFormats,
						model.truncated)) {
					currentFormat = &model.formats.back();
				}
			} else if (subtype == 0x0f && currentFormat != nullptr
				&& currentFormat->kind == FormatKind::kUnsupported) {
				if (!ParseFrame(record, *currentFormat, model)) {
					error = "invalid vendor frame descriptor";
					return false;
				}
			}
			SaveUnknown(record, model);
		}
	}

	if (!model.interfaces.hasVideoControl
		|| !model.interfaces.hasVideoStreaming
		|| !model.streamingHeader.valid) {
		error = "missing UVC control or streaming descriptors";
		return false;
	}
	BuildModeCatalog(model);
	return true;
}


void
BuildModeCatalog(DescriptorModel& model)
{
	model.modes.clear();
	for (const FormatDescriptor& format : model.formats) {
		if (format.kind == FormatKind::kUnsupported)
			continue;
		for (const FrameDescriptor& frame : format.frames) {
			std::vector<uint32_t> intervals = frame.intervals;
			if (frame.continuous) {
				const uint32_t selected = frame.defaultInterval != 0
					? frame.defaultInterval : frame.minInterval;
				if (selected != 0)
					intervals.push_back(selected);
			}
			for (uint32_t interval : intervals) {
				if (interval == 0 || model.modes.size() >= kMaxModes) {
					model.truncated = true;
					continue;
				}
				Mode mode;
				mode.kind = format.kind;
				mode.formatIndex = format.index;
				mode.frameIndex = frame.index;
				mode.width = frame.width;
				mode.height = frame.height;
				mode.bitsPerPixel = format.kind == FormatKind::kYuy2
					? format.bitsPerPixel : 0;
				mode.interval = interval;
				mode.maxFrameBufferSize = frame.maxFrameBufferSize;
				mode.pixelRate = static_cast<uint64_t>(mode.width) * mode.height
					* 10000000ULL / interval;
				const uint32_t bits = mode.bitsPerPixel != 0
					? mode.bitsPerPixel : 8;
				mode.estimatedBitsPerSecond = mode.pixelRate * bits;
				mode.id = ModeId(mode.kind, mode.formatIndex, mode.frameIndex,
					mode.interval);
				model.modes.push_back(std::move(mode));
			}
		}
	}
	std::stable_sort(model.modes.begin(), model.modes.end(),
		[](const Mode& left, const Mode& right) {
			if (left.estimatedBitsPerSecond != right.estimatedBitsPerSecond)
				return left.estimatedBitsPerSecond
					< right.estimatedBitsPerSecond;
			if (left.pixelRate != right.pixelRate)
				return left.pixelRate < right.pixelRate;
			return left.id < right.id;
		});
}


Alternate
DecodeAlternate(uint8_t alternateIndex, uint8_t alternateValue,
	uint8_t endpointAddress, uint16_t rawMaxPacket)
{
	Alternate alternate;
	alternate.alternateIndex = alternateIndex;
	alternate.alternateValue = alternateValue;
	alternate.endpointAddress = endpointAddress;
	alternate.rawMaxPacket = rawMaxPacket;
	alternate.basePacket = rawMaxPacket & 0x07ff;
	alternate.multiplier = static_cast<uint8_t>(((rawMaxPacket >> 11) & 0x03)
		+ 1);
	alternate.effectiveBytesPerMicroframe = alternate.basePacket
		* alternate.multiplier;
	return alternate;
}


AlternateSelection
SelectAlternate(const std::vector<Alternate>& alternates,
	uint8_t endpointAddress, uint32_t requiredPayload,
	bool allowHighBandwidth)
{
	AlternateSelection selection;
	size_t largestSingle = 0;
	bool foundSingle = false;

	for (size_t index = 0; index < alternates.size(); index++) {
		const Alternate& alternate = alternates[index];
		if (alternate.endpointAddress != endpointAddress)
			continue;
		if (alternate.multiplier == 1
			&& (!foundSingle || alternate.effectiveBytesPerMicroframe
				> alternates[largestSingle].effectiveBytesPerMicroframe)) {
			largestSingle = index;
			foundSingle = true;
		}
		if (allowHighBandwidth && !selection.found
			&& alternate.effectiveBytesPerMicroframe >= requiredPayload) {
			selection.found = true;
			selection.vectorIndex = index;
		}
	}

	if (allowHighBandwidth)
		return selection;
	if (foundSingle) {
		selection.found = true;
		selection.vectorIndex = largestSingle;
		selection.highBandwidthAvoided
			= alternates[largestSingle].effectiveBytesPerMicroframe
				< requiredPayload;
	}
	return selection;
}


std::array<uint8_t, kProbeLength>
EncodeProbe(const ProbeControl& probe)
{
	std::array<uint8_t, kProbeLength> bytes{};
	WriteLE16(&bytes[0], probe.hint);
	bytes[2] = probe.formatIndex;
	bytes[3] = probe.frameIndex;
	WriteLE32(&bytes[4], probe.frameInterval);
	WriteLE16(&bytes[8], probe.keyFrameRate);
	WriteLE16(&bytes[10], probe.pFrameRate);
	WriteLE16(&bytes[12], probe.compressionQuality);
	WriteLE16(&bytes[14], probe.compressionWindowSize);
	WriteLE16(&bytes[16], probe.delay);
	WriteLE32(&bytes[18], probe.maxVideoFrameSize);
	WriteLE32(&bytes[22], probe.maxPayloadTransferSize);
	return bytes;
}


bool
DecodeProbe(const uint8_t* bytes, size_t length, ProbeControl& probe)
{
	if (bytes == nullptr || length < kProbeLength)
		return false;
	probe.hint = ReadLE16(&bytes[0]);
	probe.formatIndex = bytes[2];
	probe.frameIndex = bytes[3];
	probe.frameInterval = ReadLE32(&bytes[4]);
	probe.keyFrameRate = ReadLE16(&bytes[8]);
	probe.pFrameRate = ReadLE16(&bytes[10]);
	probe.compressionQuality = ReadLE16(&bytes[12]);
	probe.compressionWindowSize = ReadLE16(&bytes[14]);
	probe.delay = ReadLE16(&bytes[16]);
	probe.maxVideoFrameSize = ReadLE32(&bytes[18]);
	probe.maxPayloadTransferSize = ReadLE32(&bytes[22]);
	return true;
}


ProbeFixups
ApplyProbeFixups(ProbeControl& probe, uint32_t fallbackFrameSize)
{
	ProbeFixups fixups;
	if ((probe.maxPayloadTransferSize & 0xffff0000U) == 0xffff0000U) {
		probe.maxPayloadTransferSize &= 0xffffU;
		fixups.payloadSignExtended = true;
	}
	if (probe.maxVideoFrameSize == 0) {
		probe.maxVideoFrameSize = fallbackFrameSize;
		fixups.maxFrameFallback = true;
	}
	return fixups;
}


bool
ProbeMatchesMode(const ProbeControl& probe, const Mode& mode)
{
	return probe.formatIndex == mode.formatIndex
		&& probe.frameIndex == mode.frameIndex
		&& probe.frameInterval == mode.interval;
}


const char*
PacketResultName(PacketResult result)
{
	switch (result) {
		case PacketResult::kAccepted: return "accepted";
		case PacketResult::kFrameComplete: return "frame_complete";
		case PacketResult::kInvalidHeader: return "invalid_header";
		case PacketResult::kPayloadError: return "payload_error";
		case PacketResult::kOverflow: return "overflow";
	}
	return "invalid_header";
}


PayloadReassembler::PayloadReassembler(size_t capacity)
	:
	fCapacity(std::min(capacity, kMaxFrameBytes))
{
	fCurrent.reserve(fCapacity);
	fCompleted.reserve(fCapacity);
}


void
PayloadReassembler::_Complete()
{
	if (!fCurrent.empty() && !fFrameErrored) {
		fCompleted = fCurrent;
		fStats.completedFrames++;
	} else {
		fCompleted.clear();
	}
	fCurrent.clear();
	fFrameErrored = false;
}


PacketResult
PayloadReassembler::AddPacket(const uint8_t* slot, size_t actualLength)
{
	fStats.packets++;
	fCompleted.clear();
	if (slot == nullptr || actualLength < 2 || slot[0] < 2
		|| slot[0] > actualLength) {
		fStats.invalidHeaders++;
		fFrameErrored = true;
		return PacketResult::kInvalidHeader;
	}

	const size_t headerLength = slot[0];
	const uint8_t flags = slot[1];
	const bool fid = (flags & 0x01) != 0;
	const bool eof = (flags & 0x02) != 0;
	const bool error = (flags & 0x40) != 0;
	bool completed = false;

	if (!fFidKnown) {
		fFidKnown = true;
		fFid = fid;
	} else if (fid != fFid) {
		fStats.fidToggles++;
		if (!fCurrent.empty()) {
			_Complete();
			completed = !fCompleted.empty();
		}
		fFid = fid;
	}

	if (error) {
		fStats.errorPackets++;
		fFrameErrored = true;
		if (eof) {
			fStats.eofPackets++;
			if (completed) {
				fCurrent.clear();
				fFrameErrored = false;
			} else {
				_Complete();
			}
		}
		return completed ? PacketResult::kFrameComplete
			: PacketResult::kPayloadError;
	}

	const size_t payloadLength = actualLength - headerLength;
	if (payloadLength > fCapacity - std::min(fCapacity, fCurrent.size())) {
		fStats.overflows++;
		fCurrent.clear();
		fFrameErrored = true;
		return completed ? PacketResult::kFrameComplete
			: PacketResult::kOverflow;
	}
	fCurrent.insert(fCurrent.end(), slot + headerLength,
		slot + actualLength);
	fStats.payloadBytes += payloadLength;

	if (eof) {
		fStats.eofPackets++;
		_Complete();
		completed = !fCompleted.empty();
	}
	return completed ? PacketResult::kFrameComplete : PacketResult::kAccepted;
}


void
PayloadReassembler::NoteTransportError()
{
	fStats.transportErrors++;
	fFrameErrored = true;
}


const std::vector<uint8_t>&
PayloadReassembler::CompletedFrame() const
{
	return fCompleted;
}


const ReassemblyStats&
PayloadReassembler::Stats() const
{
	return fStats;
}


void
PayloadReassembler::Reset()
{
	fFidKnown = false;
	fFid = false;
	fFrameErrored = false;
	fCurrent.clear();
	fCompleted.clear();
	fStats = ReassemblyStats();
}


const char*
ValidationStatusName(ValidationStatus status)
{
	switch (status) {
		case ValidationStatus::kPass: return "PASS";
		case ValidationStatus::kPassEoiMissing: return "PASS_EOI_WARNING";
		case ValidationStatus::kEmpty: return "FAIL_EMPTY";
		case ValidationStatus::kSizeMismatch: return "FAIL_SIZE";
		case ValidationStatus::kTooLarge: return "FAIL_TOO_LARGE";
		case ValidationStatus::kMissingSoi: return "FAIL_MISSING_SOI";
		case ValidationStatus::kMissingSof: return "FAIL_MISSING_SOF";
	}
	return "FAIL_EMPTY";
}


ValidationResult
ValidateYuy2(const uint8_t*, size_t length, uint16_t width, uint16_t height)
{
	ValidationResult result;
	const uint64_t expected = static_cast<uint64_t>(width) * height * 2;
	if (length == expected) {
		result.status = ValidationStatus::kPass;
		result.valid = true;
	} else if (length == 0) {
		result.status = ValidationStatus::kEmpty;
	} else {
		result.status = ValidationStatus::kSizeMismatch;
	}
	return result;
}


ValidationResult
ValidateMjpeg(const uint8_t* data, size_t length, uint32_t maxFrameSize)
{
	ValidationResult result;
	if (data == nullptr || length == 0) {
		result.status = ValidationStatus::kEmpty;
		return result;
	}
	if (maxFrameSize != 0 && length > maxFrameSize) {
		result.status = ValidationStatus::kTooLarge;
		return result;
	}
	result.hasSoi = length >= 2 && data[0] == 0xff && data[1] == 0xd8;
	if (!result.hasSoi) {
		result.status = ValidationStatus::kMissingSoi;
		return result;
	}
	for (size_t index = 2; index + 1 < length; index++) {
		if (data[index] == 0xff && IsSofMarker(data[index + 1])) {
			result.hasSof = true;
			break;
		}
	}
	if (!result.hasSof) {
		result.status = ValidationStatus::kMissingSof;
		return result;
	}
	result.hasEoi = length >= 2 && data[length - 2] == 0xff
		&& data[length - 1] == 0xd9;
	result.valid = true;
	result.warning = !result.hasEoi;
	result.status = result.hasEoi ? ValidationStatus::kPass
		: ValidationStatus::kPassEoiMissing;
	return result;
}

} // namespace jr::uvc
