// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot (GPT-5.6 Sol)
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>


namespace jr::uvc {

constexpr size_t kMaxRawDescriptorBytes = 255;
constexpr size_t kMaxDescriptors = 128;
constexpr size_t kMaxFormats = 16;
constexpr size_t kMaxFramesPerFormat = 64;
constexpr size_t kMaxIntervalsPerFrame = 32;
constexpr size_t kMaxModes = 1024;
constexpr size_t kMaxFrameBytes = 64 * 1024 * 1024;
constexpr size_t kProbeLength = 26;

uint16_t ReadLE16(const uint8_t* bytes);
uint32_t ReadLE32(const uint8_t* bytes);
void WriteLE16(uint8_t* bytes, uint16_t value);
void WriteLE32(uint8_t* bytes, uint32_t value);
std::string Hex(const uint8_t* bytes, size_t length);
std::string JsonEscape(const std::string& value);

enum class FormatKind {
	kYuy2,
	kMjpeg,
	kUnsupported
};

const char* FormatKindName(FormatKind kind);

struct DescriptorRecord {
	uint8_t interfaceNumber = 0;
	uint8_t alternateValue = 0;
	uint8_t interfaceClass = 0;
	uint8_t interfaceSubclass = 0;
	std::vector<uint8_t> bytes;
};

struct InterfaceFacts {
	bool hasVideoControl = false;
	bool hasVideoStreaming = false;
	uint8_t videoControlInterface = 0;
	uint8_t videoStreamingInterface = 0;
};

struct VsInputHeader {
	bool valid = false;
	uint8_t formatCount = 0;
	uint16_t totalLength = 0;
	uint8_t endpointAddress = 0;
	uint8_t info = 0;
	uint8_t terminalLink = 0;
	uint8_t stillCaptureMethod = 0;
	uint8_t triggerSupport = 0;
	uint8_t triggerUsage = 0;
	uint8_t controlSize = 0;
	std::vector<uint8_t> controls;
};

struct ProcessingUnit {
	bool valid = false;
	uint8_t unitId = 0;
	uint8_t sourceId = 0;
	uint16_t maxMultiplier = 0;
	std::vector<uint8_t> controls;
	uint8_t processingIndex = 0;
	uint8_t videoStandards = 0;
};

struct ColorMatching {
	uint8_t interfaceNumber = 0;
	uint8_t colorPrimaries = 0;
	uint8_t transferCharacteristics = 0;
	uint8_t matrixCoefficients = 0;
};

struct FrameDescriptor {
	uint8_t index = 0;
	uint8_t capabilities = 0;
	uint16_t width = 0;
	uint16_t height = 0;
	uint32_t minBitRate = 0;
	uint32_t maxBitRate = 0;
	uint32_t maxFrameBufferSize = 0;
	uint32_t defaultInterval = 0;
	bool continuous = false;
	uint32_t minInterval = 0;
	uint32_t maxInterval = 0;
	uint32_t intervalStep = 0;
	std::vector<uint32_t> intervals;
};

struct FormatDescriptor {
	FormatKind kind = FormatKind::kUnsupported;
	uint8_t index = 0;
	uint8_t declaredFrameCount = 0;
	std::array<uint8_t, 16> guid{};
	uint8_t bitsPerPixel = 0;
	uint8_t defaultFrameIndex = 0;
	uint8_t aspectX = 0;
	uint8_t aspectY = 0;
	uint8_t interlaceFlags = 0;
	uint8_t copyProtect = 0;
	uint8_t descriptorSubtype = 0;
	std::vector<uint8_t> raw;
	std::vector<FrameDescriptor> frames;
};

struct UnknownDescriptor {
	uint8_t interfaceNumber = 0;
	uint8_t alternateValue = 0;
	uint8_t type = 0;
	uint8_t subtype = 0;
	std::vector<uint8_t> raw;
};

struct Alternate {
	uint8_t alternateIndex = 0;
	uint8_t alternateValue = 0;
	uint8_t endpointAddress = 0;
	uint16_t rawMaxPacket = 0;
	uint16_t basePacket = 0;
	uint8_t multiplier = 0;
	uint32_t effectiveBytesPerMicroframe = 0;
};

struct Mode {
	FormatKind kind = FormatKind::kUnsupported;
	uint8_t formatIndex = 0;
	uint8_t frameIndex = 0;
	uint16_t width = 0;
	uint16_t height = 0;
	uint8_t bitsPerPixel = 0;
	uint32_t interval = 0;
	uint32_t maxFrameBufferSize = 0;
	uint64_t pixelRate = 0;
	uint64_t estimatedBitsPerSecond = 0;
	std::string id;
};

struct DescriptorModel {
	InterfaceFacts interfaces;
	uint16_t uvcVersion = 0;
	uint16_t videoControlTotalLength = 0;
	uint32_t clockFrequency = 0;
	VsInputHeader streamingHeader;
	ProcessingUnit processingUnit;
	std::vector<ColorMatching> colors;
	std::vector<FormatDescriptor> formats;
	std::vector<UnknownDescriptor> unknown;
	std::vector<Mode> modes;
	std::vector<std::string> warnings;
	bool truncated = false;
};

bool ParseDescriptors(const std::vector<DescriptorRecord>& records,
	DescriptorModel& model, std::string& error);
void BuildModeCatalog(DescriptorModel& model);
Alternate DecodeAlternate(uint8_t alternateIndex, uint8_t alternateValue,
	uint8_t endpointAddress, uint16_t rawMaxPacket);

struct AlternateSelection {
	bool found = false;
	size_t vectorIndex = 0;
	bool highBandwidthAvoided = false;
};

AlternateSelection SelectAlternate(const std::vector<Alternate>& alternates,
	uint8_t endpointAddress, uint32_t requiredPayload,
	bool allowHighBandwidth);

struct ProbeControl {
	uint16_t hint = 0;
	uint8_t formatIndex = 0;
	uint8_t frameIndex = 0;
	uint32_t frameInterval = 0;
	uint16_t keyFrameRate = 0;
	uint16_t pFrameRate = 0;
	uint16_t compressionQuality = 0;
	uint16_t compressionWindowSize = 0;
	uint16_t delay = 0;
	uint32_t maxVideoFrameSize = 0;
	uint32_t maxPayloadTransferSize = 0;
};

struct ProbeFixups {
	bool payloadSignExtended = false;
	bool maxFrameFallback = false;
};

std::array<uint8_t, kProbeLength> EncodeProbe(const ProbeControl& probe);
bool DecodeProbe(const uint8_t* bytes, size_t length, ProbeControl& probe);
ProbeFixups ApplyProbeFixups(ProbeControl& probe, uint32_t fallbackFrameSize);
bool ProbeMatchesMode(const ProbeControl& probe, const Mode& mode);

enum class PacketResult {
	kAccepted,
	kFrameComplete,
	kInvalidHeader,
	kPayloadError,
	kOverflow
};

const char* PacketResultName(PacketResult result);

struct ReassemblyStats {
	uint64_t packets = 0;
	uint64_t payloadBytes = 0;
	uint64_t completedFrames = 0;
	uint64_t transportErrors = 0;
	uint64_t errorPackets = 0;
	uint64_t invalidHeaders = 0;
	uint64_t overflows = 0;
	uint64_t fidToggles = 0;
	uint64_t eofPackets = 0;
};

class PayloadReassembler {
public:
	explicit PayloadReassembler(size_t capacity);

	PacketResult AddPacket(const uint8_t* slot, size_t actualLength);
	void NoteTransportError();
	const std::vector<uint8_t>& CompletedFrame() const;
	const ReassemblyStats& Stats() const;
	void Reset();

private:
	void _Complete();

	size_t fCapacity;
	bool fFidKnown = false;
	bool fFid = false;
	bool fFrameErrored = false;
	std::vector<uint8_t> fCurrent;
	std::vector<uint8_t> fCompleted;
	ReassemblyStats fStats;
};

enum class ValidationStatus {
	kPass,
	kPassEoiMissing,
	kEmpty,
	kSizeMismatch,
	kTooLarge,
	kMissingSoi,
	kMissingSof
};

const char* ValidationStatusName(ValidationStatus status);

struct ValidationResult {
	ValidationStatus status = ValidationStatus::kEmpty;
	bool valid = false;
	bool warning = false;
	bool hasSoi = false;
	bool hasSof = false;
	bool hasEoi = false;
};

ValidationResult ValidateYuy2(const uint8_t* data, size_t length,
	uint16_t width, uint16_t height);
ValidationResult ValidateMjpeg(const uint8_t* data, size_t length,
	uint32_t maxFrameSize);

} // namespace jr::uvc
