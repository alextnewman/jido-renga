// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot (GPT-5.6 Sol)

#include "core/UvcCore.h"

#include <USBKit.h>
#include <USB3.h>
#include <usb/USB_video.h>

#include <OS.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>


namespace jr::uvc {

namespace {

constexpr uint16_t kWinkyVendor = 0x2232;
constexpr uint16_t kWinkyProduct = 0x1068;
constexpr uint32_t kPacketsPerTransfer = 16;
constexpr uint32_t kMaximumPacketDiagnostics = 65536;
constexpr bigtime_t kDiscoveryTimeout = 5000000;
constexpr const char* kToolVersion = "1.0.2";
constexpr const char* kSchemaVersion = "jr_uvc/1";

struct Options {
	std::string output;
	std::string devicePath;
	uint32_t frames = 3;
	bool enumerateOnly = false;
	bool help = false;
};

struct RawEvidence {
	std::string deviceHex;
	std::string configurationHex;
	std::vector<DescriptorRecord> descriptors;
};

struct ControlTransferResult {
	const char* request = "";
	ssize_t status = B_ERROR;
	std::vector<uint8_t> data;
};

struct ProcessingControlResult {
	uint8_t bitIndex = 0;
	uint8_t selector = 0;
	uint8_t length = 0;
	const char* name = "";
	std::vector<ControlTransferResult> transfers;
};


struct ProcessingControlCensus {
	std::vector<ProcessingControlResult> controls;
	bool complete = true;
	std::string abortControl;
	std::string abortRequest;
};

struct PacketDiagnostic {
	int32_t status = B_ERROR;
	int16_t requestLength = 0;
	int16_t actualLength = 0;
};

struct TransferDiagnostic {
	ssize_t result = B_ERROR;
	bigtime_t elapsedUs = 0;
	bool timedOut = false;
	std::vector<PacketDiagnostic> packets;
};

struct FrameDiagnostic {
	size_t size = 0;
	bigtime_t completedUs = 0;
	ValidationResult validation;
};

struct ModeResult {
	std::string id;
	std::string status = "NOT_RUN";
	std::string failureStage;
	int32_t failureStatus = B_OK;
	Mode requested;
	ProbeControl returnedProbe;
	ProbeFixups fixups;
	ssize_t setProbeStatus = B_ERROR;
	ssize_t getProbeStatus = B_ERROR;
	ssize_t commitStatus = B_ERROR;
	std::string probeRequestHex;
	std::string probeResponseHex;
	bool bandwidthClamped = false;
	bool highBandwidthAvoided = false;
	bool negotiationAdjusted = false;
	bool cleanupOk = true;
	bool transferWorkerAbandoned = false;
	uint8_t selectedAlternateIndex = 0;
	uint8_t selectedAlternateValue = 0;
	uint32_t requestLength = 0;
	uint32_t retries = 0;
	uint32_t validFrames = 0;
	uint32_t invalidFrames = 0;
	bool packetDiagnosticsTruncated = false;
	std::string samplePath;
	std::vector<TransferDiagnostic> transfers;
	std::vector<FrameDiagnostic> frames;
	ReassemblyStats reassembly;
};

struct Summary {
	uint32_t passed = 0;
	uint32_t failed = 0;
	uint32_t skipped = 0;
};

struct DeviceIdentity {
	uint16_t vendor = 0;
	uint16_t product = 0;
	uint16_t usbVersion = 0;
	uint16_t deviceVersion = 0;
	std::string manufacturer;
	std::string productName;
	std::string serial;
	std::string location;
	std::string path;
};


std::string
Quote(const std::string& value)
{
	return "\"" + JsonEscape(value) + "\"";
}


std::string
StatusName(ssize_t status)
{
	if (status >= 0)
		return "B_OK";
	const char* text = std::strerror(static_cast<int>(status));
	return text != nullptr ? text : "B_ERROR";
}


std::string
Timestamp()
{
	std::time_t now = std::time(nullptr);
	std::tm local;
	localtime_r(&now, &local);
	char buffer[32];
	std::strftime(buffer, sizeof(buffer), "%Y%m%d-%H%M%S", &local);
	return buffer;
}


bool
CreateDirectories(const std::string& path)
{
	if (path.empty())
		return false;
	for (size_t index = 1; index <= path.size(); index++) {
		if (index != path.size() && path[index] != '/')
			continue;
		std::string current = path.substr(0, index);
		while (current.size() > 1 && current.back() == '/')
			current.pop_back();
		if (current.empty() || current == "/")
			continue;
		if (mkdir(current.c_str(), 0755) != 0 && errno != EEXIST)
			return false;
	}
	return true;
}


void
PrintUsage()
{
	std::printf(
		"jr_uvc: Jidō Renga UVC probe\n"
		"usage: jr_uvc_probe [--output DIR] [--frames N] [--enumerate-only]\n"
		"                    [--device /dev/bus/usb/X/Y] [--help]\n");
}


bool
ParseOptions(int argc, char** argv, Options& options)
{
	for (int index = 1; index < argc; index++) {
		const std::string argument = argv[index];
		if (argument == "--help") {
			options.help = true;
		} else if (argument == "--enumerate-only") {
			options.enumerateOnly = true;
		} else if ((argument == "--output" || argument == "--device"
				|| argument == "--frames") && index + 1 < argc) {
			const char* value = argv[++index];
			if (argument == "--output")
				options.output = value;
			else if (argument == "--device")
				options.devicePath = value;
			else {
				char* end = nullptr;
				const unsigned long frames = std::strtoul(value, &end, 10);
				if (end == value || *end != '\0' || frames == 0
					|| frames > 1000) {
					return false;
				}
				options.frames = static_cast<uint32_t>(frames);
			}
		} else {
			return false;
		}
	}
	return true;
}


class WinkyRoster : public BUSBRoster {
public:
	status_t DeviceAdded(BUSBDevice* device) override
	{
		if (device->VendorID() != kWinkyVendor
			|| device->ProductID() != kWinkyProduct) {
			return B_ERROR;
		}
		const char* location = device->Location();
		if (location == nullptr)
			return B_ERROR;
		std::lock_guard<std::mutex> guard(fLock);
		fPath = std::string("/dev/bus/usb") + location;
		return B_ERROR;
	}

	void DeviceRemoved(BUSBDevice*) override
	{
	}

	std::string Path()
	{
		std::lock_guard<std::mutex> guard(fLock);
		return fPath;
	}

private:
	std::mutex fLock;
	std::string fPath;
};


std::string
DiscoverWinky()
{
	WinkyRoster roster;
	roster.Start();
	const bigtime_t deadline = system_time() + kDiscoveryTimeout;
	std::string path;
	do {
		path = roster.Path();
		if (!path.empty())
			break;
		snooze(50000);
	} while (system_time() < deadline);
	roster.Stop();
	return path;
}


std::vector<uint8_t>
GetRawDescriptor(const BUSBDevice& device, uint8_t type, uint8_t index,
	size_t requested)
{
	std::vector<uint8_t> bytes(requested);
	const size_t actual = device.GetDescriptor(type, index, 0, bytes.data(),
		bytes.size());
	if (actual == 0 || actual > bytes.size())
		return {};
	bytes.resize(actual);
	return bytes;
}


bool
CollectEvidence(BUSBDevice& device, RawEvidence& evidence,
	DescriptorModel& model, BUSBInterface*& streamingInterface,
	std::vector<Alternate>& alternates, uint8_t& idleIndex, std::string& error)
{
	const std::vector<uint8_t> deviceBytes = GetRawDescriptor(device,
		USB_DESCRIPTOR_DEVICE, 0, sizeof(usb_device_descriptor));
	evidence.deviceHex = Hex(deviceBytes.data(), deviceBytes.size());
	const BUSBConfiguration* configuration = device.ActiveConfiguration();
	if (configuration == nullptr) {
		error = "no active USB configuration";
		return false;
	}
	const size_t totalLength = configuration->Descriptor()->total_length;
	const std::vector<uint8_t> configurationBytes = GetRawDescriptor(device,
		USB_DESCRIPTOR_CONFIGURATION, configuration->Index(), totalLength);
	evidence.configurationHex = Hex(configurationBytes.data(),
		configurationBytes.size());

	for (uint32_t interfaceIndex = 0;
			interfaceIndex < configuration->CountInterfaces(); interfaceIndex++) {
		const BUSBInterface* active = configuration->InterfaceAt(interfaceIndex);
		if (active == nullptr)
			continue;
		const uint32_t alternateCount = active->CountAlternates();
		for (uint32_t alternateIndex = 0; alternateIndex < alternateCount;
				alternateIndex++) {
			const BUSBInterface* alternate = active->AlternateAt(alternateIndex);
			if (alternate == nullptr || alternate->Descriptor() == nullptr)
				continue;
			const usb_interface_descriptor* interfaceDescriptor
				= alternate->Descriptor();
			for (uint32_t descriptorIndex = 0;
					descriptorIndex < kMaxDescriptors; descriptorIndex++) {
				std::array<uint8_t, kMaxRawDescriptorBytes> bytes{};
				const status_t status = alternate->OtherDescriptorAt(
					descriptorIndex,
					reinterpret_cast<usb_descriptor*>(bytes.data()),
					bytes.size());
				if (status != B_OK)
					break;
				if (bytes[0] < 2 || bytes[0] > bytes.size()) {
					error = "invalid USB Kit generic descriptor";
					return false;
				}
				DescriptorRecord record;
				record.interfaceNumber = interfaceDescriptor->interface_number;
				record.alternateValue = interfaceDescriptor->alternate_setting;
				record.interfaceClass = interfaceDescriptor->interface_class;
				record.interfaceSubclass = interfaceDescriptor->interface_subclass;
				record.bytes.assign(bytes.begin(), bytes.begin() + bytes[0]);
				evidence.descriptors.push_back(std::move(record));
			}

			if (interfaceDescriptor->interface_class != USB_VIDEO_DEVICE_CLASS
				|| interfaceDescriptor->interface_subclass
					!= USB_VIDEO_INTERFACE_VIDEOSTREAMING_SUBCLASS) {
				continue;
			}
			if (interfaceDescriptor->alternate_setting == 0) {
				idleIndex = static_cast<uint8_t>(alternateIndex);
				streamingInterface = const_cast<BUSBInterface*>(active);
			}
			for (uint32_t endpointIndex = 0;
					endpointIndex < alternate->CountEndpoints(); endpointIndex++) {
				const BUSBEndpoint* endpoint = alternate->EndpointAt(endpointIndex);
				if (endpoint == nullptr || !endpoint->IsIsochronous()
					|| !endpoint->IsInput() || endpoint->Descriptor() == nullptr) {
					continue;
				}
				const usb_endpoint_descriptor* descriptor = endpoint->Descriptor();
				alternates.push_back(DecodeAlternate(
					static_cast<uint8_t>(alternateIndex),
					interfaceDescriptor->alternate_setting,
					descriptor->endpoint_address, descriptor->max_packet_size));
			}
		}
	}
	if (!ParseDescriptors(evidence.descriptors, model, error))
		return false;
	if (streamingInterface == nullptr || alternates.empty()) {
		error = "missing VideoStreaming alternates";
		return false;
	}
	std::stable_sort(alternates.begin(), alternates.end(),
		[](const Alternate& left, const Alternate& right) {
			return left.effectiveBytesPerMicroframe
				< right.effectiveBytesPerMicroframe;
		});
	return true;
}


ProcessingControlCensus
ReadProcessingControls(const BUSBDevice& device, const DescriptorModel& model)
{
	struct ControlDefinition {
		uint8_t bitIndex;
		uint8_t selector;
		uint8_t length;
		const char* name;
		bool hasRange;
		bool hasDefault;
	};
	static constexpr ControlDefinition kControls[] = {
		{0, USB_VIDEO_PU_BRIGHTNESS_CONTROL, 2, "brightness", true, true},
		{1, USB_VIDEO_PU_CONTRAST_CONTROL, 2, "contrast", true, true},
		{2, USB_VIDEO_PU_HUE_CONTROL, 2, "hue", true, true},
		{3, USB_VIDEO_PU_SATURATION_CONTROL, 2, "saturation", true, true},
		{4, USB_VIDEO_PU_SHARPNESS_CONTROL, 2, "sharpness", true, true},
		{5, USB_VIDEO_PU_GAMMA_CONTROL, 2, "gamma", true, true},
		{6, USB_VIDEO_PU_WHITE_BALANCE_TEMPERATURE_CONTROL, 2,
			"white_balance_temperature", true, true},
		{7, USB_VIDEO_PU_WHITE_BALANCE_COMPONENT_CONTROL, 4,
			"white_balance_component", true, true},
		{8, USB_VIDEO_PU_BACKLIGHT_COMPENSATION_CONTROL, 2,
			"backlight_compensation", true, true},
		{9, USB_VIDEO_PU_GAIN_CONTROL, 2, "gain", true, true},
		{10, USB_VIDEO_PU_POWER_LINE_FREQUENCY_CONTROL, 1,
			"power_line_frequency", false, true},
		{11, USB_VIDEO_PU_HUE_AUTO_CONTROL, 1, "hue_auto", false, true},
		{12, USB_VIDEO_PU_WHITE_BALANCE_TEMPERATURE_AUTO_CONTROL, 1,
			"white_balance_temperature_auto", false, true},
		{13, USB_VIDEO_PU_WHITE_BALANCE_COMPONENT_AUTO_CONTROL, 1,
			"white_balance_component_auto", false, true},
		{14, USB_VIDEO_PU_DIGITAL_MULTIPLEXER_CONTROL, 2,
			"digital_multiplier", true, true},
		{15, USB_VIDEO_PU_DIGITAL_MULTIPLEXER_LIMIT_CONTROL, 2,
			"digital_multiplier_limit", true, true},
		{16, USB_VIDEO_PU_ANALOG_VIDEO_STANDARD_CONTROL, 1,
			"analog_video_standard", false, false},
		{17, USB_VIDEO_PU_ANALOG_LOCK_STATUS_CONTROL, 1,
			"analog_lock_status", false, false}
	};

	ProcessingControlCensus census;
	if (!model.processingUnit.valid)
		return census;
	for (const ControlDefinition& definition : kControls) {
		const size_t byteIndex = definition.bitIndex / 8;
		const uint8_t bit = definition.bitIndex % 8;
		if (byteIndex >= model.processingUnit.controls.size()
			|| (model.processingUnit.controls[byteIndex] & (1U << bit)) == 0) {
			continue;
		}
		ProcessingControlResult control;
		control.bitIndex = definition.bitIndex;
		control.selector = definition.selector;
		control.length = definition.length;
		control.name = definition.name;
		const uint16_t value = static_cast<uint16_t>(control.selector) << 8;
		const uint16_t index = static_cast<uint16_t>(
			model.processingUnit.unitId) << 8
			| model.interfaces.videoControlInterface;

		const uint16_t length = definition.length;
		const struct {
			const char* name;
			uint8_t request;
			bool enabled;
		} requests[] = {
			{"GET_CUR", USB_VIDEO_RC_GET_CUR, true},
			{"GET_MIN", USB_VIDEO_RC_GET_MIN, definition.hasRange},
			{"GET_MAX", USB_VIDEO_RC_GET_MAX, definition.hasRange},
			{"GET_RES", USB_VIDEO_RC_GET_RES, definition.hasRange},
			{"GET_DEF", USB_VIDEO_RC_GET_DEF, definition.hasDefault}
		};
		for (const auto& request : requests) {
			if (!request.enabled)
				continue;
			std::printf("jr_uvc: control %s %s\n", definition.name,
				request.name);
			std::fflush(stdout);
			std::vector<uint8_t> data(length);
			const ssize_t status = device.ControlTransfer(
				USB_REQTYPE_INTERFACE_IN | USB_REQTYPE_CLASS,
				request.request, value, index, length, data.data());
			if (status < 0)
				data.clear();
			else
				data.resize(static_cast<size_t>(status));
			control.transfers.push_back({request.name, status,
				std::move(data)});
			if (status < 0) {
				census.complete = false;
				census.abortControl = definition.name;
				census.abortRequest = request.name;
				census.controls.push_back(std::move(control));
				return census;
			}
		}
		census.controls.push_back(std::move(control));
	}
	return census;
}


struct IsochronousTask {
	const BUSBEndpoint* endpoint = nullptr;
	std::vector<uint8_t> data;
	std::vector<usb_iso_packet_descriptor> packets;
	ssize_t result = B_ERROR;
};


struct IsochronousOutcome {
	ssize_t result = B_ERROR;
	bool timedOut = false;
	bool workerAbandoned = false;
	std::vector<uint8_t> data;
	std::vector<usb_iso_packet_descriptor> packets;
};


int32
IsochronousThread(void* cookie)
{
	IsochronousTask* task = static_cast<IsochronousTask*>(cookie);
	task->result = task->endpoint->IsochronousTransfer(task->data.data(),
		task->data.size(), task->packets.data(), task->packets.size());
	return B_OK;
}


IsochronousOutcome
IsochronousTransferWithTimeout(const BUSBEndpoint* endpoint,
	uint32_t requestLength, uint32_t packetCount, bigtime_t timeout)
{
	IsochronousOutcome outcome;
	std::unique_ptr<IsochronousTask> task(new(std::nothrow) IsochronousTask);
	if (!task) {
		outcome.result = B_NO_MEMORY;
		return outcome;
	}
	task->endpoint = endpoint;
	task->data.resize(static_cast<size_t>(requestLength) * packetCount);
	task->packets.resize(packetCount);
	for (usb_iso_packet_descriptor& packet : task->packets)
		packet.request_length = static_cast<int16_t>(requestLength);

	thread_id thread = spawn_thread(IsochronousThread,
		"jr_uvc isochronous transfer", B_NORMAL_PRIORITY, task.get());
	if (thread < B_OK) {
		outcome.result = thread;
		return outcome;
	}
	status_t status = resume_thread(thread);
	if (status != B_OK) {
		kill_thread(thread);
		status_t ignored;
		wait_for_thread(thread, &ignored);
		outcome.result = status;
		return outcome;
	}

	status_t threadResult;
	status = wait_for_thread_etc(thread, B_RELATIVE_TIMEOUT, timeout,
		&threadResult);
	if (status == B_TIMED_OUT) {
		outcome.timedOut = true;
		kill_thread(thread);
		status = wait_for_thread_etc(thread, B_RELATIVE_TIMEOUT, 1000000,
			&threadResult);
		if (status != B_OK) {
			outcome.workerAbandoned = true;
			task.release();
			outcome.result = B_TIMED_OUT;
			return outcome;
		}
		outcome.result = B_TIMED_OUT;
		outcome.data = std::move(task->data);
		outcome.packets = std::move(task->packets);
		return outcome;
	}
	if (status != B_OK) {
		kill_thread(thread);
		status_t ignored;
		const status_t joinStatus = wait_for_thread_etc(thread,
			B_RELATIVE_TIMEOUT, 1000000, &ignored);
		if (joinStatus != B_OK) {
			outcome.workerAbandoned = true;
			task.release();
		}
		outcome.result = status;
		return outcome;
	}
	outcome.result = task->result;
	outcome.data = std::move(task->data);
	outcome.packets = std::move(task->packets);
	return outcome;
}


const BUSBEndpoint*
FindStreamingEndpoint(BUSBInterface* interface, uint8_t endpointAddress)
{
	for (uint32_t index = 0; index < interface->CountEndpoints(); index++) {
		const BUSBEndpoint* endpoint = interface->EndpointAt(index);
		if (endpoint != nullptr && endpoint->Descriptor() != nullptr
			&& endpoint->Descriptor()->endpoint_address == endpointAddress
			&& endpoint->IsIsochronous() && endpoint->IsInput()) {
			return endpoint;
		}
	}
	return nullptr;
}


ValidationResult
Validate(const Mode& mode, const std::vector<uint8_t>& frame,
	uint32_t maxFrameSize)
{
	if (mode.kind == FormatKind::kYuy2) {
		return ValidateYuy2(frame.data(), frame.size(), mode.width, mode.height);
	}
	return ValidateMjpeg(frame.data(), frame.size(), maxFrameSize);
}


bool
SaveSample(const std::string& output, const Mode& mode,
	const std::vector<uint8_t>& frame, std::string& path)
{
	path = output + "/jr_uvc_" + mode.id
		+ (mode.kind == FormatKind::kYuy2 ? ".yuy2" : ".jpg");
	std::ofstream stream(path, std::ios::binary);
	if (!stream)
		return false;
	stream.write(reinterpret_cast<const char*>(frame.data()), frame.size());
	return stream.good();
}


bool
CaptureFrames(const Options& options, const std::string& output,
	const BUSBEndpoint* endpoint, const Mode& mode, ProbeControl& negotiated,
	uint32_t requestLength,
	ModeResult& result, bool& workerAbandoned)
{
	workerAbandoned = false;
	const uint32_t interval = negotiated.frameInterval != 0
		? negotiated.frameInterval : mode.interval;
	const uint64_t frameUs = (static_cast<uint64_t>(interval) + 9) / 10;
	const uint64_t requestedUs = frameUs
		* (static_cast<uint64_t>(options.frames) + 5) + 1000000;
	const bigtime_t timeout = static_cast<bigtime_t>(std::min<uint64_t>(
		std::max<uint64_t>(2000000, requestedUs), 10ULL * 60 * 1000000));
	const bigtime_t started = system_time();
	const bigtime_t deadline = started + timeout;
	const size_t capacity = std::max<uint32_t>(negotiated.maxVideoFrameSize,
		mode.maxFrameBufferSize);
	if (capacity == 0 || capacity > kMaxFrameBytes)
		return false;
	PayloadReassembler assembler(std::max<size_t>(capacity, 1));
	uint32_t packetDiagnosticCount = 0;

	while (system_time() < deadline && result.validFrames < options.frames) {
		const bigtime_t transferStarted = system_time();
		const bigtime_t remaining = std::max<bigtime_t>(1,
			deadline - transferStarted);
		IsochronousOutcome outcome = IsochronousTransferWithTimeout(endpoint,
			requestLength, kPacketsPerTransfer, remaining);

		TransferDiagnostic transfer;
		transfer.result = outcome.result;
		transfer.elapsedUs = system_time() - transferStarted;
		transfer.timedOut = outcome.timedOut;
		for (uint32_t index = 0; index < outcome.packets.size(); index++) {
			const usb_iso_packet_descriptor& packet = outcome.packets[index];
			if (packetDiagnosticCount < kMaximumPacketDiagnostics) {
				transfer.packets.push_back({packet.status, packet.request_length,
					packet.actual_length});
				packetDiagnosticCount++;
			} else {
				result.packetDiagnosticsTruncated = true;
			}
			if (packet.status != B_OK || packet.actual_length < 0
				|| packet.actual_length > packet.request_length) {
				assembler.NoteTransportError();
				continue;
			}
			if (packet.actual_length == 0) {
				continue;
			}
			const uint8_t* slot = outcome.data.data() + index * requestLength;
			const PacketResult packetResult = assembler.AddPacket(slot,
				static_cast<size_t>(packet.actual_length));
			if (packetResult != PacketResult::kFrameComplete)
				continue;
			const std::vector<uint8_t>& frame = assembler.CompletedFrame();
			FrameDiagnostic diagnostic;
			diagnostic.size = frame.size();
			diagnostic.completedUs = system_time() - started;
			diagnostic.validation = Validate(mode, frame,
				negotiated.maxVideoFrameSize);
			result.frames.push_back(diagnostic);
			if (diagnostic.validation.valid) {
				result.validFrames++;
				if (result.samplePath.empty()
					&& !SaveSample(output, mode, frame, result.samplePath)) {
					result.samplePath.clear();
				}
			} else {
				result.invalidFrames++;
			}
		}
		result.transfers.push_back(std::move(transfer));
		if (outcome.workerAbandoned) {
			workerAbandoned = true;
			break;
		}
		if (outcome.result < 0 || outcome.timedOut)
			break;
	}
	result.reassembly = assembler.Stats();
	return result.validFrames >= options.frames;
}


Mode
NegotiatedMode(const DescriptorModel& model, const Mode& requested,
	const ProbeControl& negotiated)
{
	Mode mode = requested;
	for (const FormatDescriptor& format : model.formats) {
		if (format.index != negotiated.formatIndex
			|| format.kind == FormatKind::kUnsupported) {
			continue;
		}
		for (const FrameDescriptor& frame : format.frames) {
			if (frame.index != negotiated.frameIndex)
				continue;
			mode.kind = format.kind;
			mode.formatIndex = format.index;
			mode.frameIndex = frame.index;
			mode.width = frame.width;
			mode.height = frame.height;
			mode.bitsPerPixel = format.bitsPerPixel;
			mode.maxFrameBufferSize = frame.maxFrameBufferSize;
			mode.interval = negotiated.frameInterval != 0
				? negotiated.frameInterval : requested.interval;
			return mode;
		}
	}
	return mode;
}


bool
SetIdle(BUSBInterface* interface, uint8_t idleIndex)
{
	return interface->SetAlternate(idleIndex) == B_OK;
}


bool
DeviceResponsive(const BUSBDevice& device)
{
	std::array<uint8_t, sizeof(usb_device_descriptor)> descriptor{};
	return device.GetDescriptor(USB_DESCRIPTOR_DEVICE, 0, 0,
		descriptor.data(), descriptor.size()) == descriptor.size();
}


bool
RunMode(const Options& options, const std::string& output, BUSBDevice& device,
	BUSBInterface* streamingInterface, uint8_t idleIndex,
	const DescriptorModel& model, const std::vector<Alternate>& alternates,
	const Mode& mode,
	ModeResult& result, bool& fatalCleanup)
{
	result.id = mode.id;
	result.requested = mode;
	const uint16_t interfaceIndex
		= streamingInterface->Descriptor()->interface_number;
	bool passed = false;

	for (uint32_t captureTry = 0; captureTry < 2 && !passed; captureTry++) {
		if (captureTry != 0)
			result.retries++;
		if (!SetIdle(streamingInterface, idleIndex)) {
			result.status = "FAIL_CLEANUP";
			result.failureStage = "set_idle_before_probe";
			result.failureStatus = B_ERROR;
			result.cleanupOk = false;
			fatalCleanup = true;
			return false;
		}

		ProbeControl request;
		request.hint = 1;
		request.formatIndex = mode.formatIndex;
		request.frameIndex = mode.frameIndex;
		request.frameInterval = mode.interval;
		auto requestBytes = EncodeProbe(request);
		result.probeRequestHex = Hex(requestBytes.data(), requestBytes.size());
		result.setProbeStatus = device.ControlTransfer(
			USB_REQTYPE_INTERFACE_OUT | USB_REQTYPE_CLASS,
			USB_VIDEO_RC_SET_CUR,
			USB_VIDEO_VS_PROBE_CONTROL << 8, interfaceIndex,
			requestBytes.size(), requestBytes.data());
		if (result.setProbeStatus != static_cast<ssize_t>(requestBytes.size())) {
			result.status = "FAIL_PROBE_SET";
			result.failureStage = "set_cur_probe";
			result.failureStatus = static_cast<int32_t>(result.setProbeStatus);
			break;
		}

		std::array<uint8_t, kProbeLength> responseBytes{};
		result.getProbeStatus = device.ControlTransfer(
			USB_REQTYPE_INTERFACE_IN | USB_REQTYPE_CLASS,
			USB_VIDEO_RC_GET_CUR,
			USB_VIDEO_VS_PROBE_CONTROL << 8, interfaceIndex,
			responseBytes.size(), responseBytes.data());
		if (result.getProbeStatus != static_cast<ssize_t>(responseBytes.size())
			|| !DecodeProbe(responseBytes.data(), responseBytes.size(),
				result.returnedProbe)) {
			result.status = "FAIL_PROBE_GET";
			result.failureStage = "get_cur_probe";
			result.failureStatus = static_cast<int32_t>(result.getProbeStatus);
			break;
		}
		result.probeResponseHex = Hex(responseBytes.data(), responseBytes.size());
		result.fixups = ApplyProbeFixups(result.returnedProbe,
			mode.maxFrameBufferSize != 0 ? mode.maxFrameBufferSize
				: static_cast<uint32_t>(mode.width) * mode.height * 2);
		if (!ProbeMatchesMode(result.returnedProbe, mode)) {
			result.negotiationAdjusted = true;
			result.status = "FAIL_NEGOTIATED_MISMATCH";
			result.failureStage = "verify_negotiated_mode";
			break;
		}

		const AlternateSelection selection = SelectAlternate(alternates,
			model.streamingHeader.endpointAddress,
			result.returnedProbe.maxPayloadTransferSize, false);
		if (!selection.found) {
			result.status = "FAIL_BANDWIDTH";
			result.failureStage = "select_alternate";
			break;
		}
		const Alternate* selected = &alternates[selection.vectorIndex];
		result.highBandwidthAvoided = selection.highBandwidthAvoided;
		result.selectedAlternateIndex = selected->alternateIndex;
		result.selectedAlternateValue = selected->alternateValue;
		result.requestLength = selected->effectiveBytesPerMicroframe;
		if (result.highBandwidthAvoided) {
			std::printf("jr_uvc: compatibility alt %u: %u of negotiated %u "
				"bytes/microframe\n", result.selectedAlternateValue,
				result.requestLength,
				result.returnedProbe.maxPayloadTransferSize);
			std::fflush(stdout);
		}

		auto commitBytes = EncodeProbe(result.returnedProbe);
		result.commitStatus = device.ControlTransfer(
			USB_REQTYPE_INTERFACE_OUT | USB_REQTYPE_CLASS,
			USB_VIDEO_RC_SET_CUR,
			USB_VIDEO_VS_COMMIT_CONTROL << 8, interfaceIndex,
			commitBytes.size(), commitBytes.data());
		if (result.commitStatus != static_cast<ssize_t>(commitBytes.size())) {
			result.status = "FAIL_COMMIT";
			result.failureStage = "set_cur_commit";
			result.failureStatus = static_cast<int32_t>(result.commitStatus);
			break;
		}
		const status_t alternateStatus
			= streamingInterface->SetAlternate(selected->alternateIndex);
		if (alternateStatus != B_OK) {
			result.status = "FAIL_SET_ALTERNATE";
			result.failureStage = "set_streaming_alternate";
			result.failureStatus = alternateStatus;
			break;
		}
		const BUSBEndpoint* endpoint = FindStreamingEndpoint(streamingInterface,
			selected->endpointAddress);
		if (endpoint == nullptr) {
			result.status = "FAIL_ENDPOINT";
			result.failureStage = "refetch_endpoint";
			break;
		}
		const Mode captureMode = NegotiatedMode(model, mode,
			result.returnedProbe);
		bool workerAbandoned = false;
		passed = CaptureFrames(options, output, endpoint, captureMode,
			result.returnedProbe,
			selected->effectiveBytesPerMicroframe, result, workerAbandoned);
		if (workerAbandoned) {
			result.transferWorkerAbandoned = true;
			result.cleanupOk = false;
			result.status = "FAIL_TRANSFER_WORKER_STUCK";
			result.failureStage = "capture_timeout_cancel";
			fatalCleanup = true;
			return false;
		}
		if (passed) {
			result.status = "PASS";
		} else {
			result.status = result.transfers.empty()
				? "FAIL_TRANSFER" : "FAIL_TIMEOUT_OR_VALIDATION";
			result.failureStage = "capture";
			if (captureTry == 0) {
				if (!SetIdle(streamingInterface, idleIndex)) {
					result.cleanupOk = false;
					fatalCleanup = true;
					return false;
				}
				snooze(100000);
			}
		}
	}

	if (!SetIdle(streamingInterface, idleIndex)) {
		result.cleanupOk = false;
		result.status = "FAIL_CLEANUP";
		result.failureStage = "set_idle_on_exit";
		fatalCleanup = true;
		return false;
	}
	snooze(100000);
	return passed;
}


void
WriteProbe(std::ostream& stream, const ProbeControl& probe)
{
	stream << "{\"hint\":" << probe.hint
		<< ",\"format_index\":" << static_cast<unsigned>(probe.formatIndex)
		<< ",\"frame_index\":" << static_cast<unsigned>(probe.frameIndex)
		<< ",\"frame_interval_100ns\":" << probe.frameInterval
		<< ",\"max_video_frame_size\":" << probe.maxVideoFrameSize
		<< ",\"max_payload_transfer_size\":"
		<< probe.maxPayloadTransferSize << "}";
}


void
WriteModel(std::ostream& stream, const DescriptorModel& model)
{
	stream << "{\"uvc_version\":" << model.uvcVersion
		<< ",\"vc_total_length\":" << model.videoControlTotalLength
		<< ",\"clock_frequency_hz\":" << model.clockFrequency
		<< ",\"truncated\":" << (model.truncated ? "true" : "false")
		<< ",\"interfaces\":{\"video_control\":"
		<< static_cast<unsigned>(model.interfaces.videoControlInterface)
		<< ",\"video_streaming\":"
		<< static_cast<unsigned>(model.interfaces.videoStreamingInterface)
		<< "},\"vs_input_header\":{\"format_count\":"
		<< static_cast<unsigned>(model.streamingHeader.formatCount)
		<< ",\"total_length\":" << model.streamingHeader.totalLength
		<< ",\"endpoint\":" << static_cast<unsigned>(
			model.streamingHeader.endpointAddress)
		<< ",\"info\":" << static_cast<unsigned>(model.streamingHeader.info)
		<< ",\"terminal_link\":" << static_cast<unsigned>(
			model.streamingHeader.terminalLink)
		<< ",\"still_capture_method\":" << static_cast<unsigned>(
			model.streamingHeader.stillCaptureMethod)
		<< ",\"trigger_support\":" << static_cast<unsigned>(
			model.streamingHeader.triggerSupport)
		<< ",\"trigger_usage\":" << static_cast<unsigned>(
			model.streamingHeader.triggerUsage)
		<< ",\"controls_hex\":"
		<< Quote(Hex(model.streamingHeader.controls.data(),
			model.streamingHeader.controls.size())) << "}";
	stream << ",\"processing_unit\":{\"valid\":"
		<< (model.processingUnit.valid ? "true" : "false")
		<< ",\"unit_id\":" << static_cast<unsigned>(
			model.processingUnit.unitId)
		<< ",\"source_id\":" << static_cast<unsigned>(
			model.processingUnit.sourceId)
		<< ",\"max_multiplier\":" << model.processingUnit.maxMultiplier
		<< ",\"controls_hex\":"
		<< Quote(Hex(model.processingUnit.controls.data(),
			model.processingUnit.controls.size()))
		<< ",\"processing_index\":" << static_cast<unsigned>(
			model.processingUnit.processingIndex)
		<< ",\"video_standards\":" << static_cast<unsigned>(
			model.processingUnit.videoStandards) << "}";
	stream << ",\"formats\":[";
	for (size_t formatIndex = 0; formatIndex < model.formats.size();
			formatIndex++) {
		const FormatDescriptor& format = model.formats[formatIndex];
		if (formatIndex != 0)
			stream << ',';
		stream << "{\"kind\":" << Quote(FormatKindName(format.kind))
			<< ",\"index\":" << static_cast<unsigned>(format.index)
			<< ",\"subtype\":" << static_cast<unsigned>(
				format.descriptorSubtype)
			<< ",\"declared_frames\":" << static_cast<unsigned>(
				format.declaredFrameCount)
			<< ",\"guid_hex\":" << Quote(Hex(format.guid.data(),
				format.guid.size()))
			<< ",\"bits_per_pixel\":" << static_cast<unsigned>(
				format.bitsPerPixel)
			<< ",\"default_frame\":" << static_cast<unsigned>(
				format.defaultFrameIndex)
			<< ",\"raw_hex\":" << Quote(Hex(format.raw.data(),
				format.raw.size()))
			<< ",\"frames\":[";
		for (size_t frameIndex = 0; frameIndex < format.frames.size();
				frameIndex++) {
			const FrameDescriptor& frame = format.frames[frameIndex];
			if (frameIndex != 0)
				stream << ',';
			stream << "{\"index\":" << static_cast<unsigned>(frame.index)
				<< ",\"width\":" << frame.width
				<< ",\"height\":" << frame.height
				<< ",\"capabilities\":" << static_cast<unsigned>(
					frame.capabilities)
				<< ",\"min_bit_rate\":" << frame.minBitRate
				<< ",\"max_bit_rate\":" << frame.maxBitRate
				<< ",\"max_frame_buffer_size\":"
				<< frame.maxFrameBufferSize
				<< ",\"default_interval_100ns\":"
				<< frame.defaultInterval
				<< ",\"continuous\":" << (frame.continuous ? "true" : "false")
				<< ",\"min_interval_100ns\":" << frame.minInterval
				<< ",\"max_interval_100ns\":" << frame.maxInterval
				<< ",\"interval_step_100ns\":" << frame.intervalStep
				<< ",\"intervals_100ns\":[";
			for (size_t intervalIndex = 0;
					intervalIndex < frame.intervals.size(); intervalIndex++) {
				if (intervalIndex != 0)
					stream << ',';
				stream << frame.intervals[intervalIndex];
			}
			stream << "]}";
		}
		stream << "]}";
	}
	stream << "],\"modes\":[";
	for (size_t index = 0; index < model.modes.size(); index++) {
		const Mode& mode = model.modes[index];
		if (index != 0)
			stream << ',';
		stream << "{\"id\":" << Quote(mode.id)
			<< ",\"kind\":" << Quote(FormatKindName(mode.kind))
			<< ",\"format_index\":" << static_cast<unsigned>(mode.formatIndex)
			<< ",\"frame_index\":" << static_cast<unsigned>(mode.frameIndex)
			<< ",\"width\":" << mode.width << ",\"height\":" << mode.height
			<< ",\"interval_100ns\":" << mode.interval
			<< ",\"pixel_rate\":" << mode.pixelRate
			<< ",\"estimated_bits_per_second\":"
			<< mode.estimatedBitsPerSecond << "}";
	}
	stream << "],\"warnings\":[";
	for (size_t index = 0; index < model.warnings.size(); index++) {
		if (index != 0)
			stream << ',';
		stream << Quote(model.warnings[index]);
	}
	stream << "],\"color_matching\":[";
	for (size_t index = 0; index < model.colors.size(); index++) {
		const ColorMatching& color = model.colors[index];
		if (index != 0)
			stream << ',';
		stream << "{\"interface\":" << static_cast<unsigned>(
			color.interfaceNumber)
			<< ",\"primaries\":" << static_cast<unsigned>(
				color.colorPrimaries)
			<< ",\"transfer\":" << static_cast<unsigned>(
				color.transferCharacteristics)
			<< ",\"matrix\":" << static_cast<unsigned>(
				color.matrixCoefficients) << "}";
	}
	stream << "],\"unknown_descriptors\":[";
	for (size_t index = 0; index < model.unknown.size(); index++) {
		const UnknownDescriptor& unknown = model.unknown[index];
		if (index != 0)
			stream << ',';
		stream << "{\"interface\":" << static_cast<unsigned>(
			unknown.interfaceNumber)
			<< ",\"alternate\":" << static_cast<unsigned>(
				unknown.alternateValue)
			<< ",\"type\":" << static_cast<unsigned>(unknown.type)
			<< ",\"subtype\":" << static_cast<unsigned>(unknown.subtype)
			<< ",\"raw_hex\":" << Quote(Hex(unknown.raw.data(),
				unknown.raw.size())) << "}";
	}
	stream << "]}";
}


bool
WriteReport(const std::string& path, const DeviceIdentity& device,
	const RawEvidence& evidence, const DescriptorModel& model,
	const std::vector<Alternate>& alternates,
	const ProcessingControlCensus& controlCensus,
	const std::vector<ModeResult>& modes, const Summary& summary,
	bigtime_t elapsed, const std::string& abortReason)
{
	std::ofstream stream(path);
	if (!stream)
		return false;
	stream << "{\"schema_version\":" << Quote(kSchemaVersion)
		<< ",\"tool\":{\"name\":\"jr_uvc_probe\",\"brand\":\"Jido Renga\","
		"\"version\":" << Quote(kToolVersion) << "}"
		<< ",\"device\":{\"vendor_id\":" << device.vendor
		<< ",\"product_id\":" << device.product
		<< ",\"usb_version\":" << device.usbVersion
		<< ",\"device_version\":" << device.deviceVersion
		<< ",\"manufacturer\":" << Quote(device.manufacturer)
		<< ",\"product\":" << Quote(device.productName)
		<< ",\"serial\":" << Quote(device.serial)
		<< ",\"location\":" << Quote(device.location)
		<< ",\"path\":" << Quote(device.path) << "}";
	stream << ",\"parsed_descriptors\":";
	WriteModel(stream, model);
	stream << ",\"raw_evidence\":{\"device_descriptor_hex\":"
		<< Quote(evidence.deviceHex)
		<< ",\"configuration_descriptor_hex\":"
		<< Quote(evidence.configurationHex)
		<< ",\"class_specific\":[";
	for (size_t index = 0; index < evidence.descriptors.size(); index++) {
		const DescriptorRecord& descriptor = evidence.descriptors[index];
		if (index != 0)
			stream << ',';
		stream << "{\"interface\":" << static_cast<unsigned>(
			descriptor.interfaceNumber)
			<< ",\"alternate\":" << static_cast<unsigned>(
				descriptor.alternateValue)
			<< ",\"interface_class\":" << static_cast<unsigned>(
				descriptor.interfaceClass)
			<< ",\"interface_subclass\":" << static_cast<unsigned>(
				descriptor.interfaceSubclass)
			<< ",\"raw_hex\":" << Quote(Hex(descriptor.bytes.data(),
				descriptor.bytes.size())) << "}";
	}
	stream << "]}";
	stream << ",\"processing_control_census\":{\"complete\":"
		<< (controlCensus.complete ? "true" : "false")
		<< ",\"abort_control\":" << Quote(controlCensus.abortControl)
		<< ",\"abort_request\":" << Quote(controlCensus.abortRequest)
		<< ",\"controls\":[";
	const std::vector<ProcessingControlResult>& controls
		= controlCensus.controls;
	for (size_t index = 0; index < controls.size(); index++) {
		if (index != 0)
			stream << ',';
		const ProcessingControlResult& control = controls[index];
		stream << "{\"bit_index\":" << static_cast<unsigned>(control.bitIndex)
			<< ",\"name\":" << Quote(control.name)
			<< ",\"selector\":" << static_cast<unsigned>(control.selector)
			<< ",\"length\":" << static_cast<unsigned>(control.length)
			<< ",\"requests\":[";
		for (size_t requestIndex = 0;
				requestIndex < control.transfers.size(); requestIndex++) {
			if (requestIndex != 0)
				stream << ',';
			const ControlTransferResult& transfer
				= control.transfers[requestIndex];
			stream << "{\"request\":" << Quote(transfer.request)
				<< ",\"status\":" << transfer.status
				<< ",\"status_name\":" << Quote(StatusName(transfer.status))
				<< ",\"data_hex\":" << Quote(Hex(transfer.data.data(),
					transfer.data.size())) << "}";
		}
		stream << "]}";
	}
	stream << "]},\"alternates\":[";
	for (size_t index = 0; index < alternates.size(); index++) {
		if (index != 0)
			stream << ',';
		const Alternate& alternate = alternates[index];
		stream << "{\"alternate_index\":" << static_cast<unsigned>(
			alternate.alternateIndex)
			<< ",\"alternate_value\":" << static_cast<unsigned>(
				alternate.alternateValue)
			<< ",\"endpoint\":" << static_cast<unsigned>(
				alternate.endpointAddress)
			<< ",\"raw_max_packet\":" << alternate.rawMaxPacket
			<< ",\"base_packet\":" << alternate.basePacket
			<< ",\"multiplier\":" << static_cast<unsigned>(
				alternate.multiplier)
			<< ",\"effective_bytes_per_microframe\":"
			<< alternate.effectiveBytesPerMicroframe << "}";
	}
	stream << "],\"mode_results\":[";
	for (size_t index = 0; index < modes.size(); index++) {
		if (index != 0)
			stream << ',';
		const ModeResult& mode = modes[index];
		stream << "{\"id\":" << Quote(mode.id)
			<< ",\"status\":" << Quote(mode.status)
			<< ",\"failure_stage\":" << Quote(mode.failureStage)
			<< ",\"failure_status\":" << mode.failureStatus
			<< ",\"requested\":{\"kind\":"
			<< Quote(FormatKindName(mode.requested.kind))
			<< ",\"format_index\":" << static_cast<unsigned>(
				mode.requested.formatIndex)
			<< ",\"frame_index\":" << static_cast<unsigned>(
				mode.requested.frameIndex)
			<< ",\"width\":" << mode.requested.width
			<< ",\"height\":" << mode.requested.height
			<< ",\"interval_100ns\":" << mode.requested.interval << "}"
			<< ",\"probe_request_hex\":" << Quote(mode.probeRequestHex)
			<< ",\"set_probe_status\":" << mode.setProbeStatus
			<< ",\"get_probe_status\":" << mode.getProbeStatus
			<< ",\"probe_response_hex\":" << Quote(mode.probeResponseHex)
			<< ",\"returned_probe\":";
		WriteProbe(stream, mode.returnedProbe);
		stream << ",\"fixups\":{\"payload_sign_extended\":"
			<< (mode.fixups.payloadSignExtended ? "true" : "false")
			<< ",\"zero_max_frame_fallback\":"
			<< (mode.fixups.maxFrameFallback ? "true" : "false") << "}"
			<< ",\"commit_status\":" << mode.commitStatus
			<< ",\"bandwidth_clamped\":"
			<< (mode.bandwidthClamped ? "true" : "false")
			<< ",\"high_bandwidth_avoided\":"
			<< (mode.highBandwidthAvoided ? "true" : "false")
			<< ",\"negotiation_adjusted\":"
			<< (mode.negotiationAdjusted ? "true" : "false")
			<< ",\"selected_alternate_index\":"
			<< static_cast<unsigned>(mode.selectedAlternateIndex)
			<< ",\"selected_alternate_value\":"
			<< static_cast<unsigned>(mode.selectedAlternateValue)
			<< ",\"request_length\":" << mode.requestLength
			<< ",\"retries\":" << mode.retries
			<< ",\"valid_frames\":" << mode.validFrames
			<< ",\"invalid_frames\":" << mode.invalidFrames
			<< ",\"cleanup_ok\":" << (mode.cleanupOk ? "true" : "false")
			<< ",\"transfer_worker_abandoned\":"
			<< (mode.transferWorkerAbandoned ? "true" : "false")
			<< ",\"packet_diagnostics_truncated\":"
			<< (mode.packetDiagnosticsTruncated ? "true" : "false")
			<< ",\"sample_path\":" << Quote(mode.samplePath)
			<< ",\"reassembly\":{\"packets\":" << mode.reassembly.packets
			<< ",\"payload_bytes\":" << mode.reassembly.payloadBytes
			<< ",\"completed_frames\":" << mode.reassembly.completedFrames
			<< ",\"transport_errors\":" << mode.reassembly.transportErrors
			<< ",\"error_packets\":" << mode.reassembly.errorPackets
			<< ",\"invalid_headers\":" << mode.reassembly.invalidHeaders
			<< ",\"overflows\":" << mode.reassembly.overflows
			<< ",\"fid_toggles\":" << mode.reassembly.fidToggles
			<< ",\"eof_packets\":" << mode.reassembly.eofPackets << "}"
			<< ",\"frames\":[";
		for (size_t frameIndex = 0; frameIndex < mode.frames.size();
				frameIndex++) {
			if (frameIndex != 0)
				stream << ',';
			const FrameDiagnostic& frame = mode.frames[frameIndex];
			stream << "{\"size\":" << frame.size
				<< ",\"completed_us\":" << frame.completedUs
				<< ",\"status\":" << Quote(ValidationStatusName(
					frame.validation.status))
				<< ",\"valid\":"
				<< (frame.validation.valid ? "true" : "false")
				<< ",\"warning\":"
				<< (frame.validation.warning ? "true" : "false")
				<< ",\"soi\":" << (frame.validation.hasSoi ? "true" : "false")
				<< ",\"sof\":" << (frame.validation.hasSof ? "true" : "false")
				<< ",\"eoi\":" << (frame.validation.hasEoi ? "true" : "false")
				<< "}";
		}
		stream << "],\"transfers\":[";
		for (size_t transferIndex = 0;
				transferIndex < mode.transfers.size(); transferIndex++) {
			if (transferIndex != 0)
				stream << ',';
			const TransferDiagnostic& transfer = mode.transfers[transferIndex];
			stream << "{\"result\":" << transfer.result
				<< ",\"elapsed_us\":" << transfer.elapsedUs
				<< ",\"timed_out\":"
				<< (transfer.timedOut ? "true" : "false")
				<< ",\"packets\":[";
			for (size_t packetIndex = 0;
					packetIndex < transfer.packets.size(); packetIndex++) {
				if (packetIndex != 0)
					stream << ',';
				const PacketDiagnostic& packet = transfer.packets[packetIndex];
				stream << "{\"status\":" << packet.status
					<< ",\"request_length\":" << packet.requestLength
					<< ",\"actual_length\":" << packet.actualLength << "}";
			}
			stream << "]}";
		}
		stream << "]}";
	}
	stream << "],\"summary\":{\"passed\":" << summary.passed
		<< ",\"failed\":" << summary.failed
		<< ",\"skipped\":" << summary.skipped << "}"
		<< ",\"elapsed_us\":" << elapsed
		<< ",\"abort_reason\":" << Quote(abortReason) << "}\n";
	return stream.good();
}


} // namespace

} // namespace jr::uvc


int
main(int argc, char** argv)
{
	using namespace jr::uvc;
	Options options;
	if (!ParseOptions(argc, argv, options)) {
		PrintUsage();
		return 1;
	}
	if (options.help) {
		PrintUsage();
		return 0;
	}
	if (options.output.empty())
		options.output = "/boot/home/jr_uvc-" + Timestamp();
	if (!CreateDirectories(options.output)) {
		std::fprintf(stderr, "jr_uvc: cannot create output directory %s\n",
			options.output.c_str());
		return 1;
	}

	const bigtime_t started = system_time();
	std::printf("jr_uvc: version %s\n", kToolVersion);
	std::fflush(stdout);
	std::string path = options.devicePath.empty()
		? DiscoverWinky() : options.devicePath;
	if (path.empty()) {
		std::fprintf(stderr,
			"jr_uvc: Winky camera 2232:1068 not found within 5 seconds\n");
		return 2;
	}
	BUSBDevice device(path.c_str());
	if (device.InitCheck() != B_OK || device.VendorID() != kWinkyVendor
		|| device.ProductID() != kWinkyProduct) {
		std::fprintf(stderr, "jr_uvc: %s is not the Winky camera 2232:1068\n",
			path.c_str());
		return 2;
	}
	std::printf("jr_uvc: found Winky camera at %s\n", path.c_str());

	DeviceIdentity identity;
	identity.vendor = device.VendorID();
	identity.product = device.ProductID();
	identity.usbVersion = device.USBVersion();
	identity.deviceVersion = device.Version();
	identity.manufacturer = device.ManufacturerString();
	identity.productName = device.ProductString();
	identity.serial = device.SerialNumberString();
	identity.location = device.Location() != nullptr ? device.Location() : "";
	identity.path = path;

	RawEvidence evidence;
	DescriptorModel model;
	BUSBInterface* streamingInterface = nullptr;
	std::vector<Alternate> alternates;
	uint8_t idleIndex = 0;
	std::string error;
	if (!CollectEvidence(device, evidence, model, streamingInterface,
			alternates, idleIndex, error)) {
		std::fprintf(stderr, "jr_uvc: enumeration failed: %s\n", error.c_str());
		const std::string reportPath = options.output + "/jr_uvc_report.json";
		const ProcessingControlCensus noControls;
		const std::vector<ModeResult> noModes;
		const Summary emptySummary;
		if (WriteReport(reportPath, identity, evidence, model, alternates,
				noControls, noModes, emptySummary, system_time() - started,
				"enumeration_failed:" + error)) {
			std::fprintf(stderr, "jr_uvc: partial report %s\n",
				reportPath.c_str());
		}
		return 1;
	}
	std::printf("jr_uvc: enumerated %zu standard mode tuples and %zu payload "
		"alternates\n", model.modes.size(), alternates.size());
	std::fflush(stdout);

	std::vector<ModeResult> results;
	Summary summary;
	for (const FormatDescriptor& format : model.formats) {
		if (format.kind != FormatKind::kUnsupported)
			continue;
		ModeResult skipped;
		skipped.id = "unsupported-fmt"
			+ std::to_string(static_cast<unsigned>(format.index));
		skipped.status = "SKIP_UNSUPPORTED";
		skipped.requested.kind = FormatKind::kUnsupported;
		skipped.requested.formatIndex = format.index;
		results.push_back(std::move(skipped));
		summary.skipped++;
	}

	std::string abortReason;
	if (options.enumerateOnly) {
		for (const Mode& mode : model.modes) {
			ModeResult enumerated;
			enumerated.id = mode.id;
			enumerated.requested = mode;
			enumerated.status = "SKIP_ENUMERATE_ONLY";
			results.push_back(std::move(enumerated));
			summary.skipped++;
		}
	} else {
		std::printf("jr_uvc: phase mode_sweep begin modes=%zu\n",
			model.modes.size());
		std::fflush(stdout);
		for (size_t index = 0; index < model.modes.size(); index++) {
			const Mode& mode = model.modes[index];
			std::printf("jr_uvc: [%zu/%zu] %s %ux%u %.2f fps\n",
				index + 1, model.modes.size(), FormatKindName(mode.kind),
				mode.width, mode.height, 10000000.0 / mode.interval);
			ModeResult result;
			bool fatalCleanup = false;
			const bool passed = RunMode(options, options.output, device,
				streamingInterface, idleIndex, model, alternates, mode, result,
				fatalCleanup);
			if (passed)
				summary.passed++;
			else
				summary.failed++;
			std::printf("jr_uvc: %s -> %s (%u valid frame%s)\n",
				mode.id.c_str(), result.status.c_str(), result.validFrames,
				result.validFrames == 1 ? "" : "s");
			std::fflush(stdout);
			results.push_back(std::move(result));
			if (fatalCleanup) {
				abortReason = result.transferWorkerAbandoned
					? "transfer_worker_stuck"
					: (DeviceResponsive(device) ? "device_wedged" : "device_lost");
				break;
			}
		}
		std::printf("jr_uvc: phase mode_sweep end pass=%u fail=%u\n",
			summary.passed, summary.failed);
		std::fflush(stdout);
	}

	ProcessingControlCensus controls;
	if (!options.enumerateOnly && abortReason.empty()) {
		std::printf("jr_uvc: phase control_census begin\n");
		std::fflush(stdout);
		controls = ReadProcessingControls(device, model);
		std::printf("jr_uvc: phase control_census end complete=%s\n",
			controls.complete ? "yes" : "no");
		std::fflush(stdout);
	}

	const std::string reportPath = options.output + "/jr_uvc_report.json";
	if (!WriteReport(reportPath, identity, evidence, model, alternates, controls,
			results, summary, system_time() - started, abortReason)) {
		std::fprintf(stderr, "jr_uvc: cannot write report %s\n",
			reportPath.c_str());
		return 1;
	}
	std::printf("jr_uvc: report %s\n", reportPath.c_str());
	std::printf("jr_uvc: PASS=%u FAIL=%u SKIP=%u\n", summary.passed,
		summary.failed, summary.skipped);
	if (abortReason == "transfer_worker_stuck") {
		std::fflush(stdout);
		std::fflush(stderr);
		_exit(1);
	}
	return summary.passed > 0 && abortReason.empty() ? 0 : 1;
}
