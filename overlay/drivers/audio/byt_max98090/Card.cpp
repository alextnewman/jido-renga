// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot

#include "Card.h"

#include "Debug.h"
#include "Firmware.h"
#include "Ipc.h"
#include "Max98090Registers.h"
#include "SstBoot.h"
#include "SstPlayback.h"
#include "SstProtocol.h"

#include <ACPI.h>
#include <FindDirectory.h>
#include <algorithm>
#include <fcntl.h>
#include <new>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>
#include <vm/vm.h>

#include "acpi.h"


extern device_manager_info* gDeviceManager;
extern pci_module_info* gPci;
extern gpio::module_info* gGpio;


namespace jr::byt_audio {

namespace {

constexpr size_t kIramOffset = 0x0c0000;
constexpr size_t kIramSize = 0x14000;
constexpr size_t kDramOffset = 0x100000;
constexpr size_t kDramSize = 0x28000;
constexpr size_t kDramFirmwareDdrBase = 0;
constexpr size_t kDramFirmwareFeatures = 4;
constexpr uint32 kDramFirmwareBssReset = 1;
constexpr size_t kShimOffset = 0x140000;
constexpr size_t kShimSize = 0x100;
constexpr size_t kMailboxOffset = 0x144000;
constexpr size_t kMailboxSize = 0x1000;
constexpr size_t kRequiredLpeSize = kMailboxOffset + kMailboxSize;
constexpr size_t kMailboxImrBase = 0;
constexpr size_t kMailboxImrSize = 4;
constexpr size_t kMailboxReceiveOffset = 0x400;
constexpr size_t kMailboxChannelSize = 0x400;
constexpr uint32 kIpcIrqIndex = 5;
constexpr uint32 kWinkyIpcIrq = 29;
constexpr phys_addr_t kWinkyImrBase = 0x20000000;
constexpr size_t kWinkyImrSize = 0x100000;

constexpr size_t kShimCsr = 0x00;
constexpr size_t kShimIsrx = 0x18;
constexpr size_t kShimImrx = 0x28;
constexpr size_t kShimIpcx = 0x38;
constexpr size_t kShimIpcd = 0x40;
constexpr uint64 kIpcDoneInterrupt = uint64{1} << 0;
constexpr uint64 kIpcBusyInterrupt = uint64{1} << 1;
constexpr uint64 kPollingInterruptMask = 0xffff003b;
constexpr bigtime_t kFirmwareInitTimeout = 5000000;
constexpr bigtime_t kIpcTimeout = 1000000;
constexpr uint32 kIpcPollInterval = 200;
constexpr bigtime_t kExchangeTimeout = 500000;
constexpr bigtime_t kExchangeFailureBackoff = 100000;

constexpr uint16 kIntelVendor = 0x8086;
constexpr uint16 kBayTrailPmc = 0x0f1c;
constexpr uint16 kPmcBarOffset = 0x44;
constexpr uint32 kPmcBarMask = 0xfffffe00;
constexpr size_t kPmcMapSize = 0x100;
constexpr size_t kPlatformClock0 = 0x60;
constexpr uint32 kPlatformClockControlMask = 0x3;
constexpr uint32 kPlatformClockFrequencyMask = 0x4;
constexpr uint32 kPlatformClockForceOn = 0x1;
constexpr uint32 kPlatformClock19M2 = 0x4;

constexpr const char* kFirmwareSubpath
	= "/firmware/byt_max98090/fw_sst_0f28.bin";
constexpr const char* kRequiredFirmwarePath
	= "/boot/system/non-packaged/data/firmware/byt_max98090/fw_sst_0f28.bin";

constexpr int32 kMixGroupId = 1000;
constexpr int32 kMixVolumeId = 1001;
constexpr int32 kMixMuteId = 1002;
constexpr int32 kMixHeadphoneGroupId = 1010;
constexpr int32 kMixHeadphoneVolumeId = 1011;
constexpr int32 kMixHeadphoneMuteId = 1012;
constexpr int32 kMixRouteId = 1020;
constexpr int32 kMixRouteSpeakerId = 1021;
constexpr int32 kMixRouteHeadphoneId = 1022;
constexpr uint32 kMixRouteSpeaker = 0;
constexpr uint32 kMixRouteHeadphone = 1;
constexpr bigtime_t kJackDebounce = 200000;


void
TraceAllocationBody(const SstMrfldAllocation& allocation)
{
	const uint8* bytes = reinterpret_cast<const uint8*>(&allocation);
	for (size_t offset = 0; offset < sizeof(allocation); offset += 16) {
		char line[16 * 3 + 1];
		size_t position = 0;
		const size_t count = std::min(sizeof(allocation) - offset, size_t{16});
		for (size_t i = 0; i < count; i++) {
			const int written = snprintf(line + position,
				sizeof(line) - position, "%02x%s", bytes[offset + i],
				i + 1 == count ? "" : " ");
			if (written < 0)
				return;
			position += static_cast<size_t>(written);
		}
		TRACE("allocation[%02" B_PRIuSIZE "]: %s\n", offset, line);
	}
}


struct AcpiResources {
	phys_addr_t	lpeBase = 0;
	size_t		lpeSize = 0;
	phys_addr_t	ddrBase = 0;
	size_t		ddrSize = 0;
	uint32		irq = 0;
	uint32		memoryIndex = 0;
	uint32		irqIndex = 0;
};


void
RecordMemory(AcpiResources* resources, phys_addr_t base, size_t size)
{
	if (resources->memoryIndex == 0) {
		resources->lpeBase = base;
		resources->lpeSize = size;
	} else if (resources->memoryIndex == 2) {
		resources->ddrBase = base;
		resources->ddrSize = size;
	}
	resources->memoryIndex++;
}


acpi_status
ReadResources(ACPI_RESOURCE* resource, void* context)
{
	AcpiResources* resources = static_cast<AcpiResources*>(context);
	switch (resource->Type) {
		case ACPI_RESOURCE_TYPE_FIXED_MEMORY32:
			RecordMemory(resources, resource->Data.FixedMemory32.Address,
				resource->Data.FixedMemory32.AddressLength);
			break;
		case ACPI_RESOURCE_TYPE_ADDRESS32:
			if (resource->Data.Address32.ResourceType == ACPI_MEMORY_RANGE) {
				RecordMemory(resources, resource->Data.Address32.Address.Minimum,
					resource->Data.Address32.Address.AddressLength);
			}
			break;
		case ACPI_RESOURCE_TYPE_ADDRESS64:
			if (resource->Data.Address64.ResourceType == ACPI_MEMORY_RANGE) {
				RecordMemory(resources, resource->Data.Address64.Address.Minimum,
					resource->Data.Address64.Address.AddressLength);
			}
			break;
		case ACPI_RESOURCE_TYPE_EXTENDED_ADDRESS64:
			if (resource->Data.ExtAddress64.ResourceType == ACPI_MEMORY_RANGE) {
				RecordMemory(resources, resource->Data.ExtAddress64.Address.Minimum,
					resource->Data.ExtAddress64.Address.AddressLength);
			}
			break;
		case ACPI_RESOURCE_TYPE_IRQ:
			for (uint8 i = 0; i < resource->Data.Irq.InterruptCount; i++) {
				if (resources->irqIndex++ == kIpcIrqIndex)
					resources->irq = resource->Data.Irq.Interrupts[i];
			}
			break;
		case ACPI_RESOURCE_TYPE_EXTENDED_IRQ:
			for (uint8 i = 0; i < resource->Data.ExtendedIrq.InterruptCount; i++) {
				if (resources->irqIndex++ == kIpcIrqIndex)
					resources->irq = resource->Data.ExtendedIrq.Interrupts[i];
			}
			break;
		default:
			break;
	}
	return B_OK;
}


uint64
Read64(volatile uint8* base, size_t offset)
{
	return *reinterpret_cast<volatile uint64*>(base + offset);
}


void
Write64(volatile uint8* base, size_t offset, uint64 value)
{
	*reinterpret_cast<volatile uint64*>(base + offset) = value;
}


struct FirmwareCopyContext {
	volatile uint8* iram;
	volatile uint8* dram;
	volatile uint8* ddr;
};


FirmwareStatus
CopyFirmwareBlock(const FirmwareBlock& block, void* context)
{
	FirmwareCopyContext* copy = static_cast<FirmwareCopyContext*>(context);
	volatile uint8* destination = nullptr;
	switch (block.memory) {
		case FirmwareMemory::kIram:
			destination = copy->iram;
			break;
		case FirmwareMemory::kDram:
			destination = copy->dram;
			break;
		case FirmwareMemory::kDdr:
			destination = copy->ddr;
			break;
		case FirmwareMemory::kCustom:
			return FirmwareStatus::kOk;
	}
	if (destination == nullptr)
		return FirmwareStatus::kDestinationRange;
	const char* memoryName = block.memory == FirmwareMemory::kIram ? "IRAM"
		: block.memory == FirmwareMemory::kDram ? "DRAM" : "IMR";
	TRACE("copying SST firmware block to %s + 0x%08" B_PRIx32
		" (%" B_PRIu32 " bytes as 32-bit MMIO)\n", memoryName,
		block.destinationOffset, block.size);
	volatile uint32* destinationWords = reinterpret_cast<volatile uint32*>(
		destination + block.destinationOffset);
	const uint32 wordBytes = block.size & ~uint32{3};
	for (uint32 offset = 0; offset < wordBytes; offset += sizeof(uint32)) {
		const uint32 value = block.data[offset]
			| static_cast<uint32>(block.data[offset + 1]) << 8
			| static_cast<uint32>(block.data[offset + 2]) << 16
			| static_cast<uint32>(block.data[offset + 3]) << 24;
		destinationWords[offset / sizeof(uint32)] = value;
	}
	if (wordBytes != block.size) {
		TRACE("SST firmware block has %" B_PRIu32
			" trailing byte(s) omitted by the MRFLD 32-bit loader ABI\n",
			block.size - wordBytes);
	}
	return FirmwareStatus::kOk;
}


area_id
MapRegisters(const char* name, phys_addr_t base, size_t size,
	volatile uint8** address)
{
	void* mapped = nullptr;
	area_id area = map_physical_memory(name, base, size,
		B_ANY_KERNEL_BLOCK_ADDRESS | B_UNCACHED_MEMORY,
		B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA,
		&mapped);
	*address = area >= B_OK ? static_cast<volatile uint8*>(mapped) : nullptr;
	return area;
}

} // namespace


Card* gCard = nullptr;


Card::Card()
	:
	fLpePresent(false),
	fCodecPresent(false),
	fLpeStatus(B_NO_INIT),
	fCodecStatus(B_NO_INIT),
	fOpenCount(0),
	fI2c(nullptr),
	fI2cCookie(nullptr),
	fCodecNode(nullptr),
	fCodecRevision(0),
	fVolume(max98090::kDefaultSpeakerVolume),
	fMuted(false),
	fHeadphoneVolume(max98090::kDefaultHeadphoneVolume),
	fHeadphoneMuted(false),
	fHeadphonePresent(false),
	fMicrophonePresent(false),
	fIramArea(B_BAD_VALUE),
	fDramArea(B_BAD_VALUE),
	fShimArea(B_BAD_VALUE),
	fMailboxArea(B_BAD_VALUE),
	fDdrArea(B_BAD_VALUE),
	fIram(nullptr),
	fDram(nullptr),
	fShim(nullptr),
	fMailbox(nullptr),
	fDdr(nullptr),
	fDdrSize(0),
	fIpcIrq(0),
	fPmcReserved(false),
	fPmcArea(B_BAD_VALUE),
	fPmcRegisters(nullptr),
	fPlaybackArea(B_BAD_VALUE),
	fPlaybackBuffer(nullptr),
	fPlaybackPhysical(0),
	fPeriodFrames(0),
	fPeriodCount(0),
	fStreamState(kStreamIdle),
	fHardwareConfigured(false),
	fRouteConfigured(false),
	fRouteFault(B_OK),
	fAllocationFault(B_OK),
	fExchangeWanted(false),
	fPeriodElapsedCount(0),
	fPlaybackFault(B_OK),
	fLastHardwareCounter(0)
{
	mutex_init(&fLock, "BYT MAX98090 card");
	mutex_init(&fCodecLock, "BYT MAX98090 codec");
	mutex_init(&fJackLock, "BYT MAX98090 jack");
	mutex_init(&fStreamLock, "BYT MAX98090 stream");
	mutex_init(&fIpcLock, "BYT MAX98090 IPC");
	memset(&fPmcInfo, 0, sizeof(fPmcInfo));
}


Card::~Card()
{
	_TeardownJackDetection();
	const status_t stopStatus = _ForceStop();
	if (stopStatus != B_OK) {
		ERROR("stream teardown during destruction failed: %s; "
			"preserving the DMA area\n", strerror(stopStatus));
	}
	_UnmapLpe();
	if (fPmcArea >= B_OK)
		delete_area(fPmcArea);
	if (fPmcReserved) {
		gPci->unreserve_device(fPmcInfo.bus, fPmcInfo.device,
			fPmcInfo.function, "byt_max98090", this);
	}
	mutex_destroy(&fIpcLock);
	mutex_destroy(&fStreamLock);
	mutex_destroy(&fJackLock);
	mutex_destroy(&fCodecLock);
	mutex_destroy(&fLock);
}


status_t
Card::AttachLpe(device_node* acpiNode)
{
	mutex_lock(&fLock);
	if (fLpePresent) {
		mutex_unlock(&fLock);
		return B_BUSY;
	}
	fLpePresent = true;
	mutex_unlock(&fLock);

	const status_t status = _InitializeLpe(acpiNode);
	mutex_lock(&fLock);
	fLpeStatus = status;
	const bool initializeCodec = fCodecPresent && fPmcRegisters != nullptr;
	mutex_unlock(&fLock);

	if (initializeCodec) {
		const status_t codecStatus = _InitializeCodec();
		mutex_lock(&fLock);
		fCodecStatus = codecStatus;
		mutex_unlock(&fLock);
	}

	if (status != B_OK) {
		ERROR("LPE initialization incomplete: %s; device remains "
			"published for diagnostics\n", strerror(status));
	}
	return B_OK;
}


void
Card::DetachLpe()
{
	mutex_lock(&fLock);
	fLpePresent = false;
	fLpeStatus = B_NO_INIT;
	mutex_unlock(&fLock);
	const status_t status = _ForceStop();
	if (status != B_OK) {
		ERROR("stream teardown during LPE detach failed: %s\n",
			strerror(status));
	}
	_UnmapLpe();
}


status_t
Card::AttachCodec(device_node* codecNode, i2c_device_interface* interface,
	i2c_device cookie)
{
	if (codecNode == nullptr || interface == nullptr || cookie == nullptr)
		return B_BAD_VALUE;

	mutex_lock(&fLock);
	if (fCodecPresent) {
		mutex_unlock(&fLock);
		return B_BUSY;
	}
	fCodecPresent = true;
	fI2c = interface;
	fI2cCookie = cookie;
	fCodecNode = codecNode;
	const bool canInitialize = fPmcRegisters != nullptr;
	mutex_unlock(&fLock);

	if (!canInitialize) {
		TRACE("MAX98090 attached before LPE; deferring codec setup\n");
		return B_OK;
	}

	const status_t status = _InitializeCodec();
	mutex_lock(&fLock);
	fCodecStatus = status;
	mutex_unlock(&fLock);
	return B_OK;
}


void
Card::DetachCodec(i2c_device cookie)
{
	mutex_lock(&fLock);
	const bool matches = fI2cCookie == cookie;
	if (matches) {
		fCodecPresent = false;
		fCodecStatus = B_NO_INIT;
	}
	mutex_unlock(&fLock);

	if (!matches)
		return;

	_TeardownJackDetection();
	const status_t status = _ForceStop();
	if (status != B_OK) {
		ERROR("stream teardown during codec detach failed: %s\n",
			strerror(status));
	}

	mutex_lock(&fLock);
	if (fI2cCookie == cookie) {
		fI2c = nullptr;
		fI2cCookie = nullptr;
		fCodecNode = nullptr;
	}
	mutex_unlock(&fLock);
}


bool
Card::Ready()
{
	mutex_lock(&fLock);
	const bool ready = fLpePresent && fCodecPresent && fLpeStatus == B_OK
		&& fCodecStatus == B_OK;
	mutex_unlock(&fLock);
	return ready;
}


status_t
Card::Open()
{
	if (!Ready()) {
		TRACE("open rejected: LPE firmware and MAX98090 are not both "
			"ready; firmware path is %s\n", kRequiredFirmwarePath);
		return B_DEV_NOT_READY;
	}
	const status_t jackStatus = _InitializeJackDetection();
	if (jackStatus != B_OK) {
		TRACE("jack detection remains unavailable: %s\n",
			strerror(jackStatus));
	}
	mutex_lock(&fLock);
	fOpenCount++;
	const int32 openCount = fOpenCount;
	mutex_unlock(&fLock);
	TRACE("opened fixed 48 kHz stereo output device (open count %" B_PRId32
		")\n", openCount);
	return B_OK;
}


status_t
Card::Close()
{
	mutex_lock(&fLock);
	if (fOpenCount > 0)
		fOpenCount--;
	const bool lastClose = fOpenCount == 0;
	if (lastClose)
		fExchangeWanted = false;
	mutex_unlock(&fLock);
	if (lastClose)
		return _ForceStop();
	return B_OK;
}


status_t
Card::_InitializeLpe(device_node* acpiNode)
{
	status_t status = _EnablePlatformClock();
	if (status != B_OK)
		return status;
	status = _MapLpeResources(acpiNode);
	if (status != B_OK)
		return status;
	return _LoadAndStartFirmware();
}


status_t
Card::_EnablePlatformClock()
{
	if (fPmcRegisters != nullptr)
		return B_OK;

	for (long index = 0; gPci->get_nth_pci_info(index, &fPmcInfo) == B_OK;
			index++) {
		if (fPmcInfo.vendor_id == kIntelVendor
			&& fPmcInfo.device_id == kBayTrailPmc) {
			break;
		}
	}
	if (fPmcInfo.vendor_id != kIntelVendor || fPmcInfo.device_id != kBayTrailPmc) {
		ERROR("Intel Bay Trail PMC 8086:0f1c not found\n");
		return B_ENTRY_NOT_FOUND;
	}

	status_t status = gPci->reserve_device(fPmcInfo.bus, fPmcInfo.device,
		fPmcInfo.function, "byt_max98090", this);
	if (status != B_OK)
		return status;
	fPmcReserved = true;

	const uint32 bar = gPci->read_pci_config(fPmcInfo.bus, fPmcInfo.device,
		fPmcInfo.function, kPmcBarOffset, 4) & kPmcBarMask;
	if (bar == 0)
		return B_BAD_DATA;
	fPmcArea = MapRegisters("BYT PMC", bar, kPmcMapSize, &fPmcRegisters);
	if (fPmcArea < B_OK)
		return fPmcArea;

	volatile uint32* clock = reinterpret_cast<volatile uint32*>(
		fPmcRegisters + kPlatformClock0);
	const uint32 oldValue = *clock;
	*clock = (oldValue
			& ~(kPlatformClockControlMask | kPlatformClockFrequencyMask))
		| kPlatformClockForceOn | kPlatformClock19M2;
	TRACE("PMC PLT_CLK_0 enabled at 19.2 MHz (0x%08" B_PRIx32
		" -> 0x%08" B_PRIx32 ")\n", oldValue, *clock);
	return B_OK;
}


status_t
Card::_MapLpeResources(device_node* acpiNode)
{
	acpi_device_module_info* acpi = nullptr;
	acpi_device device = nullptr;
	status_t status = gDeviceManager->get_driver(acpiNode,
		reinterpret_cast<driver_module_info**>(&acpi),
		reinterpret_cast<void**>(&device));
	if (status != B_OK)
		return status;

	AcpiResources resources;
	status = acpi->walk_resources(device, const_cast<char*>("_CRS"),
		ReadResources, &resources);
	if (status != B_OK)
		return status;
	if (resources.lpeBase == 0 || resources.lpeSize < kRequiredLpeSize) {
		ERROR("ACPI memory resource 0 is missing or too small\n");
		return B_BAD_DATA;
	}
	if (resources.irqIndex <= kIpcIrqIndex
		|| resources.irq != kWinkyIpcIrq) {
		ERROR("ACPI IPC IRQ resource 5 is missing or unexpected "
			"(count=%" B_PRIu32 ", IRQ=%" B_PRIu32 ")\n",
			resources.irqIndex, resources.irq);
		return B_BAD_DATA;
	}
	if (resources.ddrBase != kWinkyImrBase
		|| resources.ddrSize != kWinkyImrSize) {
		ERROR("ACPI IMR resource 2 is unexpected (base=0x%"
			B_PRIxPHYSADDR ", size=0x%" B_PRIxSIZE ")\n",
			resources.ddrBase, resources.ddrSize);
		return B_BAD_DATA;
	}
	fIpcIrq = resources.irq;
	fDdrSize = resources.ddrSize;

	fIramArea = MapRegisters("BYT SST IRAM", resources.lpeBase + kIramOffset,
		kIramSize, &fIram);
	fDramArea = MapRegisters("BYT SST DRAM", resources.lpeBase + kDramOffset,
		kDramSize, &fDram);
	fShimArea = MapRegisters("BYT SST SHIM", resources.lpeBase + kShimOffset,
		kShimSize, &fShim);
	fMailboxArea = MapRegisters("BYT SST mailbox",
		resources.lpeBase + kMailboxOffset, kMailboxSize, &fMailbox);
	if (fIramArea < B_OK || fDramArea < B_OK || fShimArea < B_OK
		|| fMailboxArea < B_OK) {
		return B_ERROR;
	}

	fDdrArea = MapRegisters("BYT SST IMR", resources.ddrBase,
		resources.ddrSize, &fDdr);
	if (fDdrArea < B_OK)
		return fDdrArea;

	const uint32 configuredImrBase = *reinterpret_cast<volatile uint32*>(
		fMailbox + kMailboxImrBase);
	const uint32 configuredImrSize = *reinterpret_cast<volatile uint32*>(
		fMailbox + kMailboxImrSize);
	if (configuredImrBase != resources.ddrBase
		|| configuredImrSize != resources.ddrSize) {
		ERROR("coreboot IMR mailbox configuration is unexpected "
			"(base=0x%08" B_PRIx32 ", size=0x%08" B_PRIx32 ")\n",
			configuredImrBase, configuredImrSize);
		return B_BAD_DATA;
	}
	TRACE("mapped LPE resource 0 at 0x%" B_PRIxPHYSADDR
		", IMR resource 2 at 0x%" B_PRIxPHYSADDR ", IPC IRQ index 5=%" B_PRIu32
		"\n", resources.lpeBase, resources.ddrBase, fIpcIrq);
	return B_OK;
}


status_t
Card::_LoadFirmwareFile(uint8** data, size_t* size)
{
	*data = nullptr;
	*size = 0;
	const directory_which directories[] = {
		B_SYSTEM_NONPACKAGED_DATA_DIRECTORY,
		B_SYSTEM_DATA_DIRECTORY
	};

	int fd = -1;
	char path[B_PATH_NAME_LENGTH];
	for (size_t i = 0; i < B_COUNT_OF(directories); i++) {
		if (find_directory(directories[i], -1, false, path, sizeof(path)) != B_OK)
			continue;
		strlcat(path, kFirmwareSubpath, sizeof(path));
		fd = open(path, O_RDONLY);
		if (fd >= 0) {
			TRACE("loading external SST firmware from %s\n", path);
			break;
		}
	}
	if (fd < 0) {
		ERROR("required external firmware is missing: %s\n",
			kRequiredFirmwarePath);
		return B_ENTRY_NOT_FOUND;
	}

	struct stat info;
	if (fstat(fd, &info) != 0 || info.st_size <= 0
		|| info.st_size > 16 * 1024 * 1024) {
		close(fd);
		return B_BAD_DATA;
	}

	uint8* bytes = static_cast<uint8*>(malloc(static_cast<size_t>(info.st_size)));
	if (bytes == nullptr) {
		close(fd);
		return B_NO_MEMORY;
	}
	size_t done = 0;
	while (done < static_cast<size_t>(info.st_size)) {
		const ssize_t bytesRead = read(fd, bytes + done,
			static_cast<size_t>(info.st_size) - done);
		if (bytesRead <= 0) {
			free(bytes);
			close(fd);
			return B_IO_ERROR;
		}
		done += static_cast<size_t>(bytesRead);
	}
	close(fd);
	*data = bytes;
	*size = done;
	return B_OK;
}


status_t
Card::_LoadAndStartFirmware()
{
	uint8* firmware = nullptr;
	size_t firmwareSize = 0;
	status_t status = _LoadFirmwareFile(&firmware, &firmwareSize);
	if (status != B_OK)
		return status;

	uint64 csr = Read64(fShim, kShimCsr);
	const uint64 initialCsr = csr;
	const uint64 resetAsserted = MrfldAssertReset(csr);
	Write64(fShim, kShimCsr, resetAsserted);
	csr = Read64(fShim, kShimCsr);
	const uint64 resetReleased = MrfldReleaseLpeReset(csr);
	Write64(fShim, kShimCsr, resetReleased);
	csr = Read64(fShim, kShimCsr);
	TRACE("SST reset CSR: initial=0x%016" B_PRIx64
		", asserted=0x%016" B_PRIx64 ", released=0x%016" B_PRIx64 "\n",
		initialCsr, resetAsserted, csr);

	FirmwareCopyContext copy = {fIram, fDram, fDdr};
	const FirmwareLimits limits = {kIramSize, kDramSize, fDdrSize};
	const FirmwareStatus parseStatus = ParseFirmware(firmware, firmwareSize,
		limits, CopyFirmwareBlock, &copy);
	free(firmware);
	if (parseStatus != FirmwareStatus::kOk) {
		ERROR("firmware validation failed: %s\n",
			FirmwareStatusName(parseStatus));
		return B_BAD_DATA;
	}
	memory_write_barrier();

	volatile uint32* firmwareDdrBase = reinterpret_cast<volatile uint32*>(
		fDram + kDramFirmwareDdrBase);
	volatile uint32* firmwareFeatures = reinterpret_cast<volatile uint32*>(
		fDram + kDramFirmwareFeatures);
	*firmwareDdrBase = static_cast<uint32>(kWinkyImrBase);
	*firmwareFeatures = kDramFirmwareBssReset;
	memory_write_barrier();
	if (*firmwareDdrBase != static_cast<uint32>(kWinkyImrBase)
		|| *firmwareFeatures != kDramFirmwareBssReset) {
		ERROR("SST DCCM firmware configuration did not read back "
			"(IMR=0x%08" B_PRIx32 ", features=0x%08" B_PRIx32 ")\n",
			*firmwareDdrBase, *firmwareFeatures);
		return B_IO_ERROR;
	}

	Write64(fShim, kShimIsrx, kIpcBusyInterrupt);
	Write64(fShim, kShimImrx, kPollingInterruptMask);
	csr = Read64(fShim, kShimCsr);
	const uint64 startAsserted = MrfldAssertReset(csr);
	Write64(fShim, kShimCsr, startAsserted);
	csr = Read64(fShim, kShimCsr);
	const uint64 executionReleased = MrfldReleaseForExecution(csr);
	Write64(fShim, kShimCsr, executionReleased);
	csr = Read64(fShim, kShimCsr);
	TRACE("SST start CSR: asserted=0x%016" B_PRIx64
		", running=0x%016" B_PRIx64 "; IMRX=0x%016" B_PRIx64 "\n",
		startAsserted, csr, Read64(fShim, kShimImrx));

	const bigtime_t deadline = system_time() + kFirmwareInitTimeout;
	while (system_time() < deadline) {
		const uint64 ipcd = Read64(fShim, kShimIpcd);
		if (!IpcBusy(ipcd)) {
			snooze(1000);
			continue;
		}

		const uint8 message = IpcMessageId(ipcd);
		const uint32 payloadSize = IpcPayloadSize(ipcd);
		if (!IpcLarge(ipcd) || !IpcIsAsyncMessage(ipcd)) {
			_IpcAcknowledgeDsp(ipcd);
			TRACE("acknowledged unexpected pre-init DSP IPC envelope "
				"(msg=0x%02x task=%u driver=%u size=%" B_PRIu32 ")\n",
				message, IpcTaskId(ipcd), IpcDriverId(ipcd), payloadSize);
			continue;
		}

		if (payloadSize < sizeof(MrfldDspHeader)
			|| payloadSize > kMailboxChannelSize) {
			_IpcAcknowledgeDsp(ipcd);
			ERROR("firmware init IPC payload is malformed (%" B_PRIu32
				" bytes)\n", payloadSize);
			return B_BAD_DATA;
		}
		uint8 payload[sizeof(MrfldDspHeader)
			+ sizeof(MrfldFirmwareInitPayload)] = {};
		const volatile uint8* source = fMailbox + kMailboxReceiveOffset;
		const size_t copySize = payloadSize < sizeof(payload)
			? payloadSize : sizeof(payload);
		for (size_t i = 0; i < copySize; i++)
			payload[i] = source[i];
		const uint16 command = payload[4]
			| static_cast<uint16>(payload[5]) << 8;
		if (command == kMrfldFirmwareAsyncError) {
			_IpcAcknowledgeDsp(ipcd);
			ERROR("SST firmware reported an asynchronous boot error "
				"(header=0x%016" B_PRIx64 ", size=%" B_PRIu32 ")\n",
				ipcd, payloadSize);
			return B_ERROR;
		}
		if (command != kMrfldFirmwareInitCommand) {
			_IpcAcknowledgeDsp(ipcd);
			TRACE("acknowledged pre-init DSP command 0x%04" B_PRIx16
				"\n", command);
			continue;
		}
		MrfldFirmwareInitInfo init = {};
		if (!DecodeMrfldFirmwareInit(payload, payloadSize, init)) {
			_IpcAcknowledgeDsp(ipcd);
			ERROR("firmware init command body is malformed (%" B_PRIu32
				" bytes)\n", payloadSize);
			return B_BAD_DATA;
		}
		_IpcAcknowledgeDsp(ipcd);
		if (init.result != 0) {
			ERROR("firmware init completed with error %" B_PRIu16 "\n",
				init.result);
			return B_ERROR;
		}
		TRACE("SST firmware init-complete received: header=0x%016"
			B_PRIx64 ", version %02x.%02x.%02x.%02x\n", ipcd,
			init.version[3], init.version[2], init.version[1],
			init.version[0]);
		fHardwareConfigured = false;
		fRouteConfigured = false;
		fRouteFault = B_OK;
		fAllocationFault = B_OK;
		return B_OK;
	}

	ERROR("timed out waiting for SST firmware init-complete IPC "
		"(CSR=0x%016" B_PRIx64 ", ISRX=0x%016" B_PRIx64
		", IMRX=0x%016" B_PRIx64 ", IPCX=0x%016" B_PRIx64
		", IPCD=0x%016" B_PRIx64 ")\n",
		Read64(fShim, kShimCsr), Read64(fShim, kShimIsrx),
		Read64(fShim, kShimImrx), Read64(fShim, kShimIpcx),
		Read64(fShim, kShimIpcd));
	return B_TIMED_OUT;
}


status_t
Card::_WriteCodec(uint8 reg, uint8 value)
{
	if (fI2c == nullptr)
		return B_NO_INIT;
	status_t status = fI2c->acquire_bus(fI2cCookie);
	if (status != B_OK)
		return status;
	const uint8 command[2] = {reg, value};
	status = fI2c->exec_command(fI2cCookie, I2C_OP_WRITE_STOP, command,
		sizeof(command), nullptr, 0);
	fI2c->release_bus(fI2cCookie);
	return status;
}


status_t
Card::_ReadCodec(uint8 reg, uint8& value)
{
	if (fI2c == nullptr)
		return B_NO_INIT;
	status_t status = fI2c->acquire_bus(fI2cCookie);
	if (status != B_OK)
		return status;
	status = fI2c->exec_command(fI2cCookie, I2C_OP_READ_STOP, &reg, 1,
		&value, 1);
	fI2c->release_bus(fI2cCookie);
	return status;
}


status_t
Card::_InitializeCodec()
{
	mutex_lock(&fCodecLock);
	status_t status = _WriteCodec(max98090::kSoftwareReset, max98090::kReset);
	if (status != B_OK) {
		mutex_unlock(&fCodecLock);
		return status;
	}
	snooze(20000);
	status = _ReadCodec(max98090::kRevision, fCodecRevision);
	if (status != B_OK) {
		mutex_unlock(&fCodecLock);
		return status;
	}

	const struct {
		uint8 reg;
		uint8 value;
	} settings[] = {
		{max98090::kSystemClock, max98090::kSystemClock19M2},
		{max98090::kClockMode, max98090::kConsumerClockRatio},
		{max98090::kClockRatioNiMsb, max98090::kConsumerClockRatio},
		{max98090::kClockRatioNiLsb, max98090::kConsumerClockRatio},
		{max98090::kMasterMode, max98090::kMasterModeConsumer},
		{max98090::kInterfaceFormat, max98090::kInterfaceI2sS16Normal},
		{max98090::kTdmFormat, max98090::kTdmDisabled},
		{max98090::kTdmControl, max98090::kTdmDisabled},
		{max98090::kIoConfiguration, max98090::kIoPlayback},
		{max98090::kFilterConfiguration,
			max98090::kFilterMusicPlaybackDcBlock},
		{max98090::kDaiPlaybackLevel, max98090::kDaiPlaybackUnmutedUnity},
		{max98090::kHeadphoneControl, 0},
		{max98090::kLeftHeadphoneVolume,
			static_cast<uint8>(max98090::kDefaultHeadphoneVolume
				| max98090::kHeadphoneMute)},
		{max98090::kRightHeadphoneVolume,
			static_cast<uint8>(max98090::kDefaultHeadphoneVolume
				| max98090::kHeadphoneMute)},
		{max98090::kLeftSpeakerMixer,
			max98090::kLeftDacToLeftSpeaker},
		{max98090::kRightSpeakerMixer,
			max98090::kRightDacToRightSpeaker},
		{max98090::kSpeakerControl, max98090::kSpeakerControlValue},
		{max98090::kLeftSpeakerVolume,
			max98090::kDefaultSpeakerRegisterValue},
		{max98090::kRightSpeakerVolume,
			max98090::kDefaultSpeakerRegisterValue},
		{max98090::kOutputEnable, max98090::kDacAndSpeakerEnable},
		{max98090::kDeviceShutdown, max98090::kShutdownRelease}
	};
	for (size_t i = 0; i < B_COUNT_OF(settings); i++) {
		status = _WriteCodec(settings[i].reg, settings[i].value);
		if (status != B_OK) {
			mutex_unlock(&fCodecLock);
			return status;
		}
	}
	fHeadphonePresent = false;
	fMicrophonePresent = false;
	mutex_unlock(&fCodecLock);

	TRACE("MAX98090 revision 0x%02x initialized for 48 kHz I2S playback\n",
		fCodecRevision);
	const status_t jackStatus = _InitializeJackDetection();
	if (jackStatus != B_OK) {
		TRACE("codec ready without jack detection: %s; it will be retried "
			"when opened\n", strerror(jackStatus));
	}
	return B_OK;
}


status_t
Card::_SetCodecVolume(uint8 volume, bool muted)
{
	if (volume > max98090::kSpeakerVolumeMaximum)
		volume = max98090::kSpeakerVolumeMaximum;
	mutex_lock(&fCodecLock);
	const uint8 value = (max98090::kSpeakerVolumeRawMinimum + volume)
		| ((muted || fHeadphonePresent) ? max98090::kSpeakerMute : 0);
	status_t status = _WriteCodec(max98090::kLeftSpeakerVolume, value);
	if (status != B_OK) {
		mutex_unlock(&fCodecLock);
		return status;
	}
	status = _WriteCodec(max98090::kRightSpeakerVolume, value);
	if (status != B_OK) {
		mutex_unlock(&fCodecLock);
		return status;
	}
	fVolume = volume;
	fMuted = muted;
	mutex_unlock(&fCodecLock);
	return B_OK;
}


status_t
Card::_SetHeadphoneVolume(uint8 volume, bool muted)
{
	if (volume > max98090::kHeadphoneVolumeMaximum)
		volume = max98090::kHeadphoneVolumeMaximum;
	mutex_lock(&fCodecLock);
	const uint8 value = volume
		| ((muted || !fHeadphonePresent) ? max98090::kHeadphoneMute : 0);
	status_t status = _WriteCodec(max98090::kLeftHeadphoneVolume, value);
	if (status != B_OK) {
		mutex_unlock(&fCodecLock);
		return status;
	}
	status = _WriteCodec(max98090::kRightHeadphoneVolume, value);
	if (status != B_OK) {
		mutex_unlock(&fCodecLock);
		return status;
	}
	fHeadphoneVolume = volume;
	fHeadphoneMuted = muted;
	mutex_unlock(&fCodecLock);
	return B_OK;
}


status_t
Card::_InitializeJackDetection()
{
	mutex_lock(&fJackLock);
	if (fHeadphoneDetect.IsValid()) {
		mutex_unlock(&fJackLock);
		return B_OK;
	}
	mutex_lock(&fLock);
	device_node* codecNode = fCodecPresent ? fCodecNode : nullptr;
	mutex_unlock(&fLock);
	if (codecNode == nullptr) {
		mutex_unlock(&fJackLock);
		return B_NO_INIT;
	}

	gpio::Pin headphone;
	status_t status = headphone.AcquireAcpi(gGpio, codecNode, 0, 0);
	if (status != B_OK) {
		mutex_unlock(&fJackLock);
		return status;
	}

	status = headphone.Watch({gpio::Edge::Both, kJackDebounce},
		&_HeadphoneEvent, this);
	if (status != B_OK) {
		headphone.Reset();
		mutex_unlock(&fJackLock);
		return status;
	}

	gpio::Level headphoneLevel;
	status = headphone.Read(headphoneLevel);
	if (status != B_OK) {
		headphone.Reset();
		mutex_unlock(&fJackLock);
		return status;
	}
	fHeadphoneDetect = std::move(headphone);
	gpio::Pin microphone;
	status_t microphoneStatus = microphone.AcquireAcpi(gGpio, codecNode, 1, 0);
	gpio::Level microphoneLevel = gpio::Level::High;
	if (microphoneStatus == B_OK) {
		microphoneStatus = microphone.Watch(
			{gpio::Edge::Both, kJackDebounce}, &_MicrophoneEvent, this);
	}
	if (microphoneStatus == B_OK)
		microphoneStatus = microphone.Read(microphoneLevel);
	if (microphoneStatus == B_OK) {
		fMicrophoneDetect = std::move(microphone);
	} else {
		microphone.Reset();
		TRACE("microphone-presence GPIO unavailable: %s\n",
			strerror(microphoneStatus));
	}
	mutex_unlock(&fJackLock);

	const bool headphonePresent = headphoneLevel == gpio::Level::High;
	const bool microphonePresent = microphoneLevel == gpio::Level::Low;
	status = _ApplyJackState(headphonePresent, microphonePresent);
	if (status != B_OK) {
		_TeardownJackDetection();
		return status;
	}
	TRACE("jack GPIOs active: headphone=%s microphone=%s, 200 ms debounce\n",
		headphonePresent ? "present" : "absent",
		microphonePresent ? "present" : "absent");
	return B_OK;
}


void
Card::_TeardownJackDetection()
{
	mutex_lock(&fJackLock);
	fHeadphoneDetect.Reset();
	fMicrophoneDetect.Reset();
	mutex_unlock(&fJackLock);
}


status_t
Card::_ApplyJackState(bool headphonePresent, bool microphonePresent)
{
	mutex_lock(&fCodecLock);
	if (fI2c == nullptr) {
		mutex_unlock(&fCodecLock);
		return B_NO_INIT;
	}
	if (headphonePresent == fHeadphonePresent
		&& microphonePresent == fMicrophonePresent) {
		mutex_unlock(&fCodecLock);
		return B_OK;
	}

	const uint8 headphoneValue = fHeadphoneVolume
		| ((!headphonePresent || fHeadphoneMuted)
			? max98090::kHeadphoneMute : 0);
	const uint8 speakerValue = (max98090::kSpeakerVolumeRawMinimum + fVolume)
		| ((headphonePresent || fMuted) ? max98090::kSpeakerMute : 0);
	status_t status;
	if (headphonePresent) {
		status = _WriteCodec(max98090::kLeftSpeakerVolume, speakerValue);
		if (status == B_OK)
			status = _WriteCodec(max98090::kRightSpeakerVolume, speakerValue);
		if (status == B_OK) {
			status = _WriteCodec(max98090::kOutputEnable,
				max98090::kDacAndHeadphoneEnable);
		}
		if (status == B_OK) {
			status = _WriteCodec(max98090::kLeftHeadphoneVolume,
				headphoneValue);
		}
		if (status == B_OK) {
			status = _WriteCodec(max98090::kRightHeadphoneVolume,
				headphoneValue);
		}
	} else {
		status = _WriteCodec(max98090::kLeftHeadphoneVolume, headphoneValue);
		if (status == B_OK) {
			status = _WriteCodec(max98090::kRightHeadphoneVolume,
				headphoneValue);
		}
		if (status == B_OK) {
			status = _WriteCodec(max98090::kOutputEnable,
				max98090::kDacAndSpeakerEnable);
		}
		if (status == B_OK)
			status = _WriteCodec(max98090::kLeftSpeakerVolume, speakerValue);
		if (status == B_OK)
			status = _WriteCodec(max98090::kRightSpeakerVolume, speakerValue);
	}
	if (status == B_OK) {
		fHeadphonePresent = headphonePresent;
		fMicrophonePresent = microphonePresent;
	}
	mutex_unlock(&fCodecLock);

	if (status == B_OK) {
		EVENT("jack state: headphones %s, microphone %s; routing playback "
			"to %s\n", headphonePresent ? "present" : "absent",
			microphonePresent ? "present" : "absent",
			headphonePresent ? "headphones" : "speakers");
	} else {
		ERROR("failed to apply jack state: %s\n", strerror(status));
	}
	return status;
}


void
Card::_HeadphoneEvent(void* context, const gpio::Event& event)
{
	Card* card = static_cast<Card*>(context);
	bool microphonePresent;
	mutex_lock(&card->fCodecLock);
	microphonePresent = card->fMicrophonePresent;
	mutex_unlock(&card->fCodecLock);
	card->_ApplyJackState(event.level == gpio::Level::High,
		microphonePresent);
}


void
Card::_MicrophoneEvent(void* context, const gpio::Event& event)
{
	Card* card = static_cast<Card*>(context);
	bool headphonePresent;
	mutex_lock(&card->fCodecLock);
	headphonePresent = card->fHeadphonePresent;
	mutex_unlock(&card->fCodecLock);
	card->_ApplyJackState(headphonePresent,
		event.level == gpio::Level::Low);
}


void
Card::_UnmapLpe()
{
	area_id* areas[] = {
		&fIramArea, &fDramArea, &fShimArea, &fMailboxArea, &fDdrArea
	};
	for (size_t i = 0; i < B_COUNT_OF(areas); i++) {
		if (*areas[i] >= B_OK)
			delete_area(*areas[i]);
		*areas[i] = B_BAD_VALUE;
	}
	fIram = fDram = fShim = fMailbox = fDdr = nullptr;
	fDdrSize = 0;
	fRouteConfigured = false;
	fStreamState = kStreamIdle;
	fAllocationFault = B_OK;
	fPlaybackFault = B_OK;
	fLastHardwareCounter = 0;
}


status_t
Card::_GetDescription(multi_description* description)
{
	static const multi_channel_info channels[] = {
		{0, B_MULTI_OUTPUT_CHANNEL, B_CHANNEL_LEFT | B_CHANNEL_STEREO_BUS, 0},
		{1, B_MULTI_OUTPUT_CHANNEL, B_CHANNEL_RIGHT | B_CHANNEL_STEREO_BUS, 0},
		{2, B_MULTI_OUTPUT_BUS, B_CHANNEL_LEFT | B_CHANNEL_STEREO_BUS, 0},
		{3, B_MULTI_OUTPUT_BUS, B_CHANNEL_RIGHT | B_CHANNEL_STEREO_BUS, 0}
	};
	description->interface_version = B_CURRENT_INTERFACE_VERSION;
	description->interface_minimum = B_CURRENT_INTERFACE_VERSION;
	strlcpy(description->friendly_name, "Bay Trail MAX98090",
		sizeof(description->friendly_name));
	strlcpy(description->vendor_info, "Intel / Maxim",
		sizeof(description->vendor_info));
	description->output_channel_count = 2;
	description->input_channel_count = 0;
	description->output_bus_channel_count = 2;
	description->input_bus_channel_count = 0;
	description->aux_bus_channel_count = 0;
	description->output_rates = B_SR_48000;
	description->input_rates = 0;
	description->min_cvsr_rate = 0;
	description->max_cvsr_rate = 0;
	description->output_formats = B_FMT_16BIT;
	description->input_formats = 0;
	description->lock_sources = B_MULTI_LOCK_INTERNAL;
	description->timecode_sources = 0;
	description->interface_flags = B_MULTI_INTERFACE_PLAYBACK;
	description->start_latency = 0;
	description->control_panel[0] = '\0';
	if (description->request_channel_count >= static_cast<int32>(
			B_COUNT_OF(channels))) {
		if (user_memcpy(description->channels, channels, sizeof(channels))
			!= B_OK) {
			return B_BAD_ADDRESS;
		}
	}
	return B_OK;
}


status_t
Card::_GetEnabledChannels(multi_channel_enable* enable)
{
	multi_channel_enable result = {};
	if (user_memcpy(&result, enable, sizeof(result)) != B_OK)
		return B_BAD_ADDRESS;
	uint8 bits = kFixedPlaybackChannelMask;
	if (user_memcpy(result.enable_bits, &bits, sizeof(bits)) != B_OK)
		return B_BAD_ADDRESS;
	result.lock_source = B_MULTI_LOCK_INTERNAL;
	result.lock_data = 0;
	result.timecode_source = 0;
	return user_memcpy(enable, &result, sizeof(result));
}


status_t
Card::_SetEnabledChannels(multi_channel_enable* enable)
{
	multi_channel_enable request = {};
	if (user_memcpy(&request, enable, sizeof(request)) != B_OK)
		return B_BAD_ADDRESS;
	uint8 bits = 0;
	if (user_memcpy(&bits, request.enable_bits, sizeof(bits)) != B_OK)
		return B_BAD_ADDRESS;
	if (!AcceptFixedPlaybackChannelMask(bits)
		|| request.lock_source != B_MULTI_LOCK_INTERNAL) {
		return B_BAD_VALUE;
	}
	TRACE("accepted fixed stereo channel mask 0x%02x\n", bits);
	return B_OK;
}


status_t
Card::_GetGlobalFormat(multi_format_info* format)
{
	format->output.rate = B_SR_48000;
	format->output.format = B_FMT_16BIT;
	format->output.cvsr = 48000;
	format->input.rate = 0;
	format->input.format = 0;
	format->input.cvsr = 0;
	format->output_latency = 0;
	format->input_latency = 0;
	format->timecode_kind = B_MULTI_NO_TIMECODE;
	return B_OK;
}


status_t
Card::_SetGlobalFormat(multi_format_info* format)
{
	if (format->output.rate != B_SR_48000
		|| format->output.format != B_FMT_16BIT) {
		return B_BAD_VALUE;
	}
	return B_OK;
}


status_t
Card::_ListMixControls(multi_mix_control_info* controls)
{
	if (controls->control_count < 9)
		return B_BUFFER_OVERFLOW;
	multi_mix_control list[9] = {};
	list[0].id = kMixGroupId;
	list[0].flags = B_MULTI_MIX_GROUP;
	list[0].string = S_OUTPUT;
	strlcpy(list[0].name, "Speaker", sizeof(list[0].name));
	list[1].id = kMixVolumeId;
	list[1].parent = kMixGroupId;
	list[1].flags = B_MULTI_MIX_GAIN;
	list[1].string = S_VOLUME;
	list[1].gain.min_gain = 0;
	list[1].gain.max_gain = max98090::kSpeakerVolumeMaximum;
	list[1].gain.granularity = 1;
	list[2].id = kMixMuteId;
	list[2].parent = kMixGroupId;
	list[2].flags = B_MULTI_MIX_ENABLE;
	list[2].string = S_MUTE;
	list[3].id = kMixHeadphoneGroupId;
	list[3].flags = B_MULTI_MIX_GROUP;
	list[3].string = S_OUTPUT;
	strlcpy(list[3].name, "Headphones", sizeof(list[3].name));
	list[4].id = kMixHeadphoneVolumeId;
	list[4].parent = kMixHeadphoneGroupId;
	list[4].flags = B_MULTI_MIX_GAIN;
	list[4].string = S_VOLUME;
	list[4].gain.min_gain = 0;
	list[4].gain.max_gain = max98090::kHeadphoneVolumeMaximum;
	list[4].gain.granularity = 1;
	list[5].id = kMixHeadphoneMuteId;
	list[5].parent = kMixHeadphoneGroupId;
	list[5].flags = B_MULTI_MIX_ENABLE;
	list[5].string = S_MUTE;
	list[6].id = kMixRouteId;
	list[6].flags = B_MULTI_MIX_MUX;
	strlcpy(list[6].name, "Active output", sizeof(list[6].name));
	list[7].id = kMixRouteSpeakerId;
	list[7].parent = kMixRouteId;
	list[7].flags = B_MULTI_MIX_MUX_VALUE;
	strlcpy(list[7].name, "Speaker", sizeof(list[7].name));
	list[8].id = kMixRouteHeadphoneId;
	list[8].parent = kMixRouteId;
	list[8].flags = B_MULTI_MIX_MUX_VALUE;
	strlcpy(list[8].name, "Headphones", sizeof(list[8].name));
	if (user_memcpy(controls->controls, list, sizeof(list)) != B_OK)
		return B_BAD_ADDRESS;
	controls->control_count = 9;
	return B_OK;
}


status_t
Card::_GetMix(multi_mix_value_info* values)
{
	mutex_lock(&fCodecLock);
	const uint8 speakerVolume = fVolume;
	const bool speakerMuted = fMuted;
	const uint8 headphoneVolume = fHeadphoneVolume;
	const bool headphoneMuted = fHeadphoneMuted;
	const bool headphonePresent = fHeadphonePresent;
	mutex_unlock(&fCodecLock);

	for (int32 i = 0; i < values->item_count; i++) {
		multi_mix_value value;
		if (user_memcpy(&value, values->values + i, sizeof(value)) != B_OK)
			return B_BAD_ADDRESS;
		if (value.id == kMixVolumeId)
			value.gain = speakerVolume;
		else if (value.id == kMixMuteId)
			value.enable = speakerMuted;
		else if (value.id == kMixHeadphoneVolumeId)
			value.gain = headphoneVolume;
		else if (value.id == kMixHeadphoneMuteId)
			value.enable = headphoneMuted;
		else if (value.id == kMixRouteId)
			value.mux = headphonePresent
				? kMixRouteHeadphone : kMixRouteSpeaker;
		else
			return B_BAD_VALUE;
		if (user_memcpy(values->values + i, &value, sizeof(value)) != B_OK)
			return B_BAD_ADDRESS;
	}
	return B_OK;
}


status_t
Card::_SetMix(multi_mix_value_info* values)
{
	mutex_lock(&fCodecLock);
	uint8 speakerVolume = fVolume;
	bool speakerMuted = fMuted;
	uint8 headphoneVolume = fHeadphoneVolume;
	bool headphoneMuted = fHeadphoneMuted;
	const bool headphonePresent = fHeadphonePresent;
	mutex_unlock(&fCodecLock);

	bool setSpeaker = false;
	bool setHeadphone = false;
	for (int32 i = 0; i < values->item_count; i++) {
		multi_mix_value value;
		if (user_memcpy(&value, values->values + i, sizeof(value)) != B_OK)
			return B_BAD_ADDRESS;
		if (value.id == kMixVolumeId) {
			if (value.gain < 0
				|| value.gain > max98090::kSpeakerVolumeMaximum) {
				return B_BAD_VALUE;
			}
			speakerVolume = static_cast<uint8>(value.gain);
			setSpeaker = true;
		} else if (value.id == kMixMuteId) {
			speakerMuted = value.enable;
			setSpeaker = true;
		} else if (value.id == kMixHeadphoneVolumeId) {
			if (value.gain < 0
				|| value.gain > max98090::kHeadphoneVolumeMaximum) {
				return B_BAD_VALUE;
			}
			headphoneVolume = static_cast<uint8>(value.gain);
			setHeadphone = true;
		} else if (value.id == kMixHeadphoneMuteId) {
			headphoneMuted = value.enable;
			setHeadphone = true;
		} else if (value.id == kMixRouteId) {
			const uint32 activeRoute = headphonePresent
				? kMixRouteHeadphone : kMixRouteSpeaker;
			if (value.mux != activeRoute)
				return B_NOT_ALLOWED;
		} else {
			return B_BAD_VALUE;
		}
	}
	if (setSpeaker) {
		const status_t status = _SetCodecVolume(speakerVolume, speakerMuted);
		if (status != B_OK)
			return status;
	}
	if (setHeadphone)
		return _SetHeadphoneVolume(headphoneVolume, headphoneMuted);
	return B_OK;
}


status_t
Card::_GetBuffers(multi_buffer_list* buffers)
{
	if (buffers->request_playback_channels != 2)
		return B_BAD_VALUE;

	status_t status = _ForceStop();
	if (status != B_OK)
		return status;

	mutex_lock(&fStreamLock);
	fPeriodCount = NormalizePeriodCount(buffers->request_playback_buffers);
	fPeriodFrames = NormalizePeriodFrames(
		buffers->request_playback_buffer_size);
	const size_t periodBytes = fPeriodFrames * 2 * sizeof(int16);
	const size_t totalBytes = periodBytes * fPeriodCount;
	const size_t areaBytes = (totalBytes + B_PAGE_SIZE - 1)
		& ~(B_PAGE_SIZE - 1);
	virtual_address_restrictions virtualRestrictions = {};
	virtualRestrictions.address_specification = B_ANY_KERNEL_ADDRESS;
	physical_address_restrictions physicalRestrictions = {};
	physicalRestrictions.high_address = kSstDmaAddressSpaceSize;
	fPlaybackArea = vm_create_anonymous_area(B_SYSTEM_TEAM,
		"BYT SST playback ring", areaBytes, B_CONTIGUOUS,
		B_READ_AREA | B_WRITE_AREA, 0, 0, &virtualRestrictions,
		&physicalRestrictions, true, &fPlaybackBuffer);
	if (fPlaybackArea < B_OK) {
		fPlaybackBuffer = nullptr;
		fPeriodFrames = 0;
		fPeriodCount = 0;
		mutex_unlock(&fStreamLock);
		return fPlaybackArea;
	}

	physical_entry entry;
	status = get_memory_map(fPlaybackBuffer, areaBytes, &entry, 1);
	if (status != B_OK || !FitsSstDmaRange(entry.address, areaBytes)) {
		delete_area(fPlaybackArea);
		fPlaybackArea = B_BAD_VALUE;
		fPlaybackBuffer = nullptr;
		fPeriodFrames = 0;
		fPeriodCount = 0;
		mutex_unlock(&fStreamLock);
		return status != B_OK ? status : B_BAD_VALUE;
	}
	fPlaybackPhysical = entry.address;
	memset(fPlaybackBuffer, 0, totalBytes);

	buffers->flags = B_MULTI_BUFFER_PLAYBACK;
	buffers->return_playback_buffers = fPeriodCount;
	buffers->return_playback_channels = 2;
	buffers->return_playback_buffer_size = fPeriodFrames;
	buffers->return_record_buffers = 0;
	buffers->return_record_channels = 0;
	buffers->return_record_buffer_size = 0;
	for (int32 period = 0; period < fPeriodCount; period++) {
		uint8* base = static_cast<uint8*>(fPlaybackBuffer)
			+ period * periodBytes;
		buffer_desc descriptors[2] = {};
		descriptors[0].base = reinterpret_cast<char*>(base);
		descriptors[0].stride = 4;
		descriptors[1].base = reinterpret_cast<char*>(base + sizeof(int16));
		descriptors[1].stride = 4;
		if (!IS_USER_ADDRESS(buffers->playback_buffers[period])
			|| user_memcpy(buffers->playback_buffers[period], descriptors,
				sizeof(descriptors)) != B_OK) {
			delete_area(fPlaybackArea);
			fPlaybackArea = B_BAD_VALUE;
			fPlaybackBuffer = nullptr;
			fPlaybackPhysical = 0;
			fPeriodFrames = 0;
			fPeriodCount = 0;
			mutex_unlock(&fStreamLock);
			return B_BAD_ADDRESS;
		}
	}
	fAllocationFault = B_OK;
	fLastHardwareCounter = 0;
	TRACE("allocated %" B_PRIuSIZE " byte 32-bit playback ring at "
		"0x%" B_PRIxPHYSADDR "\n", totalBytes, fPlaybackPhysical);
	mutex_unlock(&fStreamLock);
	return B_OK;
}


// ---------------------------------------------------------------------------
// IPC engine (polling)
//
// All interrupts are masked (kPollingInterruptMask). Whenever the driver needs
// to wait for a DSP response or period-elapsed notification, it polls the SHIM
// registers. This is the initial working milestone; IRQ-driven handling is a
// future refinement.
// ---------------------------------------------------------------------------

void
Card::_IpcClearHostDone()
{
	const uint64 ipcx = Read64(fShim, kShimIpcx);
	if (!IpcDone(ipcx))
		return;
	Write64(fShim, kShimIpcx, ipcx & ~(uint64{1} << 62));
	Write64(fShim, kShimIsrx, kIpcDoneInterrupt);
}


void
Card::_IpcAcknowledgeDsp(uint64 header)
{
	Write64(fShim, kShimIsrx, kIpcBusyInterrupt);
	Write64(fShim, kShimIpcd, AcknowledgeMrfldIpc(header));
}


status_t
Card::_IpcReceive(uint8 expectedMessage, uint8 expectedDriverId,
	void* response, uint32 responseCapacity, uint32* responseSize,
	bool* received)
{
	if (responseSize != nullptr)
		*responseSize = 0;
	*received = false;

	const uint64 ipcd = Read64(fShim, kShimIpcd);
	if (!IpcBusy(ipcd))
		return B_OK;

	const uint32 payloadSize = IpcPayloadSize(ipcd);
	if (IpcIsAsyncMessage(ipcd)) {
		status_t status = B_OK;
		if (!IpcLarge(ipcd)
			|| payloadSize < sizeof(MrfldDspHeader)
			|| payloadSize > kMailboxChannelSize) {
			ERROR("malformed asynchronous DSP IPC: header=0x%016"
				B_PRIx64 ", size=%" B_PRIu32 "\n", ipcd, payloadSize);
			status = B_BAD_DATA;
		} else {
			const volatile uint8* payload
				= fMailbox + kMailboxReceiveOffset;
			const uint8 pipeId = payload[1];
			const uint16 command = payload[4]
				| static_cast<uint16>(payload[5]) << 8;
			if (command == kMrfldPeriodElapsed
				&& pipeId == kPlaybackPipeId) {
				atomic_add(&fPeriodElapsedCount, 1);
			} else if (command == kMrfldBufferUnderrun
				&& pipeId == kPlaybackPipeId) {
				ERROR("DSP reported playback buffer underrun\n");
				atomic_set(&fPlaybackFault, B_IO_ERROR);
			} else {
				TRACE("acknowledged asynchronous DSP command 0x%04"
					B_PRIx16 " for pipe 0x%02x\n", command, pipeId);
			}
		}
		_IpcAcknowledgeDsp(ipcd);
		return status;
	}

	if (!IpcMatchesReply(ipcd, expectedMessage, expectedDriverId)) {
		ERROR("unexpected DSP reply: msg=0x%02x driver=%u; expected "
			"msg=0x%02x driver=%u\n", IpcMessageId(ipcd),
			IpcDriverId(ipcd), expectedMessage, expectedDriverId);
		_IpcAcknowledgeDsp(ipcd);
		return B_BAD_DATA;
	}

	status_t status = B_OK;
	if (IpcLarge(ipcd)) {
		if (payloadSize > kMailboxChannelSize
			|| (response != nullptr && payloadSize > responseCapacity)) {
			ERROR("DSP reply payload is too large (%" B_PRIu32
				" bytes)\n", payloadSize);
			status = B_BUFFER_OVERFLOW;
		} else if (response != nullptr) {
			const volatile uint8* source
				= fMailbox + kMailboxReceiveOffset;
			uint8* destination = static_cast<uint8*>(response);
			for (uint32 i = 0; i < payloadSize; i++)
				destination[i] = source[i];
			if (responseSize != nullptr)
				*responseSize = payloadSize;
		}
	}

	const uint8 result = IpcResult(ipcd);
	if (result != 0) {
		ERROR("DSP reply failed: msg=0x%02x driver=%u result=%u "
			"detail=0x%08" B_PRIx32 " raw=0x%016" B_PRIx64 "\n",
			IpcMessageId(ipcd), IpcDriverId(ipcd), result,
			IpcPayloadSize(ipcd), ipcd);
		status = B_ERROR;
	}

	_IpcAcknowledgeDsp(ipcd);
	*received = true;
	return status;
}


status_t
Card::_IpcPollService()
{
	if (fShim == nullptr || fMailbox == nullptr)
		return B_NO_INIT;

	mutex_lock(&fIpcLock);
	_IpcClearHostDone();
	bool received = false;
	const status_t status = _IpcReceive(0xff, 0xff, nullptr, 0, nullptr,
		&received);
	mutex_unlock(&fIpcLock);
	return status;
}


status_t
Card::_IpcSend(uint8 ipcMsg, uint8 taskId, const void* data, uint32 length,
	bool responseRequired, void* response, uint32 responseCapacity,
	uint32* responseSize)
{
	if (fShim == nullptr || fMailbox == nullptr)
		return B_NO_INIT;
	if (length > kMailboxChannelSize || (length > 0 && data == nullptr))
		return B_BAD_VALUE;

	mutex_lock(&fIpcLock);
	const uint8 driverId = kMrfldSerializedDriverId;
	const bigtime_t readyDeadline = system_time() + kIpcTimeout;
	while (system_time() < readyDeadline) {
		_IpcClearHostDone();
		bool received = false;
		status_t status = _IpcReceive(0xff, 0xff, nullptr, 0, nullptr,
			&received);
		if (status != B_OK) {
			mutex_unlock(&fIpcLock);
			return status;
		}
		if (!IpcBusy(Read64(fShim, kShimIpcx)))
			break;
		snooze(kIpcPollInterval);
	}
	if (IpcBusy(Read64(fShim, kShimIpcx))) {
		ERROR("IPCX remained busy before msg 0x%02x\n", ipcMsg);
		mutex_unlock(&fIpcLock);
		return B_BUSY;
	}

	const uint8* source = static_cast<const uint8*>(data);
	for (uint32 i = 0; i < length; i++)
		fMailbox[i] = source[i];

	const uint64 header = PackMrfldIpcHeader(length, ipcMsg, taskId,
		driverId, responseRequired, true, false, true);
	TRACE("IPC send: msg=0x%02x task=%u driver=%u bytes=%" B_PRIu32
		" response=%s raw=0x%016" B_PRIx64 "\n", ipcMsg, taskId, driverId,
		length, responseRequired ? "yes" : "no", header);
	Write64(fShim, kShimIpcx, header);

	if (!responseRequired) {
		mutex_unlock(&fIpcLock);
		return B_OK;
	}

	const bigtime_t responseDeadline = system_time() + kIpcTimeout;
	while (system_time() < responseDeadline) {
		_IpcClearHostDone();
		bool received = false;
		const status_t status = _IpcReceive(ipcMsg, driverId, response,
			responseCapacity, responseSize, &received);
		if (status != B_OK || received) {
			mutex_unlock(&fIpcLock);
			return status;
		}
		snooze(kIpcPollInterval);
	}

	ERROR("DSP reply timed out: msg=0x%02x driver=%u\n", ipcMsg, driverId);
	mutex_unlock(&fIpcLock);
	return B_TIMED_OUT;
}


status_t
Card::_IpcSendByteStream(uint8 ipcMsg, uint8 taskId, const void* data,
	uint16 length, bool blocked)
{
	return _IpcSend(ipcMsg, taskId, data, length, blocked, nullptr, 0,
		nullptr);
}


status_t
Card::_IpcSendAllocate(uint8 taskId, uint8 pipeId, const void* data,
	size_t length, void* response, uint32 responseCapacity,
	uint32* responseSize)
{
	if (length > kMailboxChannelSize - sizeof(MrfldDspHeader))
		return B_BAD_VALUE;

	uint8 message[kMailboxChannelSize] = {};
	const MrfldDspHeader dspHeader = MakeMrfldDspHeader(
		kMrfldAllocateStream, pipeId, static_cast<uint16>(length));
	memcpy(message, &dspHeader, sizeof(dspHeader));
	memcpy(message + sizeof(dspHeader), data, length);
	return _IpcSend(kMrfldIpcCommand, taskId, message,
		static_cast<uint32>(sizeof(dspHeader) + length), true, response,
		responseCapacity, responseSize);
}


status_t
Card::_IpcSendStreamCommand(uint16 commandId, uint8 taskId, uint8 pipeId,
	bool responseRequired)
{
	uint8 message[sizeof(MrfldDspHeader) + sizeof(uint16)] = {};
	const uint16 payloadLength
		= commandId == kMrfldStartStream ? sizeof(uint16) : 0;
	const MrfldDspHeader dspHeader = MakeMrfldDspHeader(commandId, pipeId,
		payloadLength);
	memcpy(message, &dspHeader, sizeof(dspHeader));
	return _IpcSend(kMrfldIpcCommand, taskId, message,
		sizeof(dspHeader) + payloadLength, responseRequired, nullptr, 0,
		nullptr);
}


// ---------------------------------------------------------------------------
// Playback route configuration
// ---------------------------------------------------------------------------

status_t
Card::_PreparePlaybackHardware()
{
	if (fHardwareConfigured)
		return B_OK;
	if (fRouteFault != B_OK)
		return fRouteFault;

	// 0. SBA virtual-bus start (cmd 85)
	const SstDspHeader vbStart = WinkyVirtualBusStart();
	status_t status = _IpcSendByteStream(kIpcCmd, kTaskSba,
		&vbStart, sizeof(vbStart), true);
	if (status != B_OK)
		return _FailPlaybackRoute("VB start", status);

	// 1. SSP configure (cmd 117)
	const SstSspCommand ssp = WinkySspConfiguration();
	status = _IpcSendByteStream(kIpcCmd, kTaskSba,
		&ssp, sizeof(ssp), true);
	if (status != B_OK)
		return _FailPlaybackRoute("SSP configure", status);

	fHardwareConfigured = true;
	return B_OK;
}


status_t
Card::_ConfigurePlaybackRoute()
{
	if (fRouteConfigured)
		return B_OK;
	if (fRouteFault != B_OK)
		return fRouteFault;
	if (!fHardwareConfigured || fStreamState != kStreamAllocated)
		return B_NOT_ALLOWED;

	// 2. SSP slot map (cmd 130) — sent as SET_PARAMS
	const SstSspSlotMapCommand slotMap = WinkySspSlotMap();
	status_t status = _IpcSendByteStream(kIpcSetParams, kTaskSba,
		&slotMap, sizeof(slotMap), true);
	if (status != B_OK)
		return _FailPlaybackRoute("SSP slot map", status);

	// 3. MMX media1 gain 0 dB — sent as SET_PARAMS
	const SstGainCommand media1Gain = MakeMedia1Gain0dB();
	status = _IpcSendByteStream(kIpcSetParams, kTaskMmx,
		&media1Gain, sizeof(media1Gain), true);
	if (status != B_OK)
		return _FailPlaybackRoute("media1 gain", status);

	// 4. MMX SWM media1_in → media0_out
	const SstSwmCommand mmxSwm = MakeMedia1ToMedia0Swm();
	const uint16 mmxSwmSize = static_cast<uint16>(
		sizeof(SstByteStreamDspHeader) + mmxSwm.header.length);
	status = _IpcSendByteStream(kIpcCmd, kTaskMmx,
		&mmxSwm, mmxSwmSize, true);
	if (status != B_OK)
		return _FailPlaybackRoute("MMX SWM", status);

	// 5. MMX enable media0 path
	const SstMediaPathCommand media0Path = MakeMedia0PathEnable();
	status = _IpcSendByteStream(kIpcCmd, kTaskMmx,
		&media0Path, sizeof(media0Path), true);
	if (status != B_OK)
		return _FailPlaybackRoute("media0 path enable", status);

	// 6. SBA enable pcm0 input
	const SstMediaPathCommand pcm0In = MakePcm0InputEnable();
	status = _IpcSendByteStream(kIpcCmd, kTaskSba,
		&pcm0In, sizeof(pcm0In), true);
	if (status != B_OK)
		return _FailPlaybackRoute("pcm0 input enable", status);

	// 7. SBA gain pcm0 input 0 dB — sent as SET_PARAMS
	const SstGainCommand pcm0Gain = MakePcm0InputGain0dB();
	status = _IpcSendByteStream(kIpcSetParams, kTaskSba,
		&pcm0Gain, sizeof(pcm0Gain), true);
	if (status != B_OK)
		return _FailPlaybackRoute("pcm0 gain", status);

	// 8. SBA SWM pcm0_in → codec_out0
	const SstSwmCommand sbaSwm = MakePcm0ToCodecOut0Swm();
	const uint16 sbaSwmSize = static_cast<uint16>(
		sizeof(SstByteStreamDspHeader) + sbaSwm.header.length);
	status = _IpcSendByteStream(kIpcCmd, kTaskSba,
		&sbaSwm, sbaSwmSize, true);
	if (status != B_OK)
		return _FailPlaybackRoute("SBA SWM", status);

	// 9. SBA gain codec_out0 0 dB — sent as SET_PARAMS
	const SstGainCommand codecOut0Gain = MakeCodecOut0Gain0dB();
	status = _IpcSendByteStream(kIpcSetParams, kTaskSba,
		&codecOut0Gain, sizeof(codecOut0Gain), true);
	if (status != B_OK)
		return _FailPlaybackRoute("codec_out0 gain", status);

	fRouteConfigured = true;
	TRACE("SST playback hardware and route configured (10 commands)\n");
	return B_OK;
}


status_t
Card::_FailPlaybackRoute(const char* step, status_t status)
{
	fRouteFault = status;
	ERROR("%s failed: %s; route retries suppressed until DSP reload\n",
		step, strerror(status));
	return status;
}


// ---------------------------------------------------------------------------
// Stream allocation and lifecycle
// ---------------------------------------------------------------------------

status_t
Card::_AllocateStream()
{
	if (fPlaybackArea < B_OK || fPlaybackBuffer == nullptr)
		return B_NO_INIT;
	if (fAllocationFault != B_OK)
		return fAllocationFault;

	const uint32 periodBytes = fPeriodFrames * 4;
	const uint32 totalBytes = periodBytes * static_cast<uint32>(fPeriodCount);
	const uint32 tsAddress = MrfldTimestampAddress(kWinkyMailboxLpeAddress,
		kPlaybackStreamId);

	const SstMrfldAllocation alloc = BuildWinkyAllocation(
		static_cast<uint32>(fPlaybackPhysical), totalBytes, periodBytes,
		tsAddress);
	TRACE("allocation request: hardware=%s route=%s task=%u pipe=0x%02x "
		"ring=0x%08" B_PRIx32 "+%" B_PRIu32 " fragment=%" B_PRIu32
		" timestamp=0x%08" B_PRIx32 "\n",
		fHardwareConfigured ? "VB+SSP ready" : "not ready",
		fRouteConfigured ? "ready" : "pending", kTaskMmx, kPlaybackPipeId,
		alloc.ringBuffers[0].address, alloc.ringBuffers[0].size,
		alloc.fragmentSizeBytes, alloc.timestampAddress);
	TraceAllocationBody(alloc);

	for (uint32 attempt = 0; attempt < 2; attempt++) {
		uint8 response[kMailboxChannelSize] = {};
		uint32 responseSize = 0;
		status_t status = _IpcSendAllocate(kTaskMmx, kPlaybackPipeId,
			&alloc, sizeof(alloc), response, sizeof(response), &responseSize);
		if (status != B_OK) {
			return _FailStreamAllocation(status);
		}

		uint16 result = 0;
		if (!ParseAllocationResult(response, responseSize, result)) {
			ERROR("DSP allocation response is malformed (%" B_PRIu32
				" bytes)\n", responseSize);
			return _FailStreamAllocation(B_BAD_DATA);
		}
		if (result == 0)
			break;
		if (ShouldRecoverStaleAllocation(result, attempt)) {
			TRACE("freeing stale DSP allocation for pipe 0x%02x\n",
				kPlaybackPipeId);
			status = _IpcSendStreamCommand(kMrfldFreeStream, kTaskMmx,
				kPlaybackPipeId, true);
			if (status != B_OK) {
				ERROR("stale stream cleanup failed: %s\n",
					strerror(status));
				return _FailStreamAllocation(status);
			}
			continue;
		}
		ERROR("DSP alloc result %" B_PRIu16 "\n", result);
		return _FailStreamAllocation(B_ERROR);
	}

	fStreamState = kStreamAllocated;
	fAllocationFault = B_OK;
	fPeriodElapsedCount = 0;
	fPlaybackFault = B_OK;
	fLastHardwareCounter = ReadTimestampU64(fMailbox,
		MrfldTimestampOffset(kPlaybackStreamId) + 8);
	TRACE("stream allocated: pipe 0x%02x, ts 0x%08" B_PRIx32 "\n",
		kPlaybackPipeId, tsAddress);
	return B_OK;
}


status_t
Card::_FailStreamAllocation(status_t status)
{
	fAllocationFault = status;
	ERROR("stream allocation failed: %s; retries suppressed until buffers "
		"are recreated or DSP reload\n", strerror(status));
	return status;
}


status_t
Card::_StartStream()
{
	if (fStreamState != kStreamAllocated)
		return B_NOT_ALLOWED;

	status_t status = _IpcSendStreamCommand(kMrfldStartStream, kTaskMmx,
		kPlaybackPipeId, false);
	if (status != B_OK)
		return status;

	fStreamState = kStreamRunning;
	TRACE("playback stream started\n");
	return B_OK;
}


status_t
Card::_StopStream()
{
	if (fStreamState == kStreamIdle)
		return B_OK;

	const bool sendDrop = fStreamState != kStreamStopping;
	if (sendDrop)
		fStreamState = kStreamStopping;

	status_t dropStatus = B_OK;
	if (sendDrop) {
		dropStatus = _IpcSendStreamCommand(kMrfldDropStream, kTaskMmx,
			kPlaybackPipeId, false);
		if (dropStatus != B_OK) {
			ERROR("stream drop failed: %s\n",
				strerror(dropStatus));
		}
	}

	const status_t freeStatus = _IpcSendStreamCommand(kMrfldFreeStream,
		kTaskMmx, kPlaybackPipeId, true);
	if (freeStatus != B_OK) {
		ERROR("stream free failed: %s\n", strerror(freeStatus));
		return freeStatus;
	}

	fStreamState = kStreamIdle;
	fLastHardwareCounter = 0;
	return dropStatus;
}


status_t
Card::_FreeStream()
{
	if (fStreamState != kStreamIdle)
		return _StopStream();
	return B_OK;
}


status_t
Card::_ForceStop()
{
	mutex_lock(&fLock);
	fExchangeWanted = false;
	mutex_unlock(&fLock);

	mutex_lock(&fStreamLock);
	const status_t stopStatus = _FreeStream();
	if (stopStatus != B_OK && fStreamState != kStreamIdle) {
		mutex_unlock(&fStreamLock);
		return stopStatus;
	}
	if (fPlaybackArea >= B_OK)
		delete_area(fPlaybackArea);
	fPlaybackArea = B_BAD_VALUE;
	fPlaybackBuffer = nullptr;
	fPlaybackPhysical = 0;
	fPeriodFrames = 0;
	fPeriodCount = 0;
	fPlaybackFault = B_OK;
	fLastHardwareCounter = 0;
	mutex_unlock(&fStreamLock);
	return stopStatus;
}


// ---------------------------------------------------------------------------
// B_MULTI_BUFFER_EXCHANGE
//
// On first exchange: start the virtual bus and configure SSP, allocate the
// media input stream, configure its dependent route, then start playback.
// Poll period-elapsed and read the firmware timestamp without holding fLock.
// ---------------------------------------------------------------------------

status_t
Card::_BufferExchange(multi_buffer_info* info)
{
	mutex_lock(&fStreamLock);
	if (fPlaybackArea < B_OK || fPlaybackBuffer == nullptr
		|| fPeriodFrames == 0 || fPeriodCount == 0) {
		mutex_unlock(&fStreamLock);
		snooze(kExchangeFailureBackoff);
		return B_NO_INIT;
	}

	const uint32 periodBytes = fPeriodFrames * 4;
	const int32 periodCount = fPeriodCount;
	mutex_lock(&fLock);
	fExchangeWanted = true;
	mutex_unlock(&fLock);

	status_t status = B_OK;
	bigtime_t deadline = 0;
	uint64 hardwareCounter = fLastHardwareCounter;
	bool periodAdvanced = false;
	uint32 cycle = 0;
	uint64 frames = 0;
	bigtime_t realTime = 0;
	bool exchangeWanted = false;
	bool backoff = false;
	multi_buffer_info result = {};
	if (fRouteFault != B_OK) {
		status = fRouteFault;
		goto fail;
	}
	if (fStreamState == kStreamStopping) {
		status = _FreeStream();
		if (status != B_OK)
			goto fail;
	}
	if (!fHardwareConfigured) {
		status = _PreparePlaybackHardware();
		if (status != B_OK)
			goto fail;
	}
	if (fStreamState == kStreamIdle) {
		status = _AllocateStream();
		if (status != B_OK)
			goto fail;
	}
	if (!fRouteConfigured) {
		status = _ConfigurePlaybackRoute();
		if (status != B_OK)
			goto fail;
	}
	if (fStreamState == kStreamAllocated) {
		status = _StartStream();
		if (status != B_OK) {
			const status_t freeStatus = _FreeStream();
			if (freeStatus != B_OK) {
				ERROR("stream cleanup after start failure failed: %s\n",
					strerror(freeStatus));
			}
			goto fail;
		}
	}

	deadline = system_time() + kExchangeTimeout;
	while (system_time() < deadline) {
		mutex_lock(&fLock);
		exchangeWanted = fExchangeWanted;
		mutex_unlock(&fLock);
		if (!exchangeWanted || fStreamState != kStreamRunning) {
			status = B_INTERRUPTED;
			goto done;
		}

		status = _IpcPollService();
		if (status != B_OK)
			goto fail;
		status = atomic_get(&fPlaybackFault);
		if (status != B_OK)
			goto fail;

		hardwareCounter = ReadTimestampU64(fMailbox,
			MrfldTimestampOffset(kPlaybackStreamId) + 8);
		if (hardwareCounter < fLastHardwareCounter) {
			ERROR("DSP hardware counter regressed from 0x%016"
				B_PRIx64 " to 0x%016" B_PRIx64 "\n",
				fLastHardwareCounter, hardwareCounter);
			status = B_BAD_DATA;
			goto fail;
		}
		if (hardwareCounter / periodBytes
				> fLastHardwareCounter / periodBytes) {
			periodAdvanced = true;
			break;
		}

		snooze(kIpcPollInterval);
	}
	if (!periodAdvanced) {
		ERROR("timed out waiting for a playback period\n");
		status = B_TIMED_OUT;
		goto fail;
	}

	mutex_lock(&fLock);
	exchangeWanted = fExchangeWanted;
	mutex_unlock(&fLock);
	if (!exchangeWanted || fStreamState != kStreamRunning) {
		status = B_INTERRUPTED;
		goto done;
	}

	cycle = PlaybackBufferCycle(hardwareCounter, periodBytes, periodCount);
	frames = PlayedFrames(hardwareCounter, 4);
	realTime = system_time();

	result.playback_buffer_cycle = cycle;
	result.played_real_time = realTime;
	result.played_frames_count = frames;
	result.record_buffer_cycle = 0;
	result.recorded_real_time = 0;
	result.recorded_frames_count = 0;
	result.flags = B_MULTI_BUFFER_PLAYBACK;

	if (user_memcpy(info, &result, sizeof(result)) != B_OK)
		status = B_BAD_ADDRESS;
	else
		fLastHardwareCounter = hardwareCounter;

done:
	backoff = status != B_OK && status != B_INTERRUPTED;
	mutex_unlock(&fStreamLock);
	if (backoff)
		snooze(kExchangeFailureBackoff);
	return status;

fail:
	mutex_lock(&fLock);
	fExchangeWanted = false;
	mutex_unlock(&fLock);
	if (fStreamState != kStreamIdle) {
		const status_t cleanupStatus = _FreeStream();
		if (cleanupStatus != B_OK) {
			ERROR("stream cleanup after exchange failure failed: %s\n",
				strerror(cleanupStatus));
		}
	}
	goto done;
}


status_t
Card::Control(uint32 op, void* buffer, size_t)
{
	if (!Ready())
		return B_DEV_NOT_READY;
	switch (op) {
		case B_MULTI_GET_DESCRIPTION:
			return _GetDescription(static_cast<multi_description*>(buffer));
		case B_MULTI_GET_ENABLED_CHANNELS:
			return _GetEnabledChannels(
				static_cast<multi_channel_enable*>(buffer));
		case B_MULTI_SET_ENABLED_CHANNELS:
			return _SetEnabledChannels(
				static_cast<multi_channel_enable*>(buffer));
		case B_MULTI_GET_GLOBAL_FORMAT:
			return _GetGlobalFormat(static_cast<multi_format_info*>(buffer));
		case B_MULTI_SET_GLOBAL_FORMAT:
			return _SetGlobalFormat(static_cast<multi_format_info*>(buffer));
		case B_MULTI_LIST_MIX_CONTROLS:
			return _ListMixControls(
				static_cast<multi_mix_control_info*>(buffer));
		case B_MULTI_GET_MIX:
			return _GetMix(static_cast<multi_mix_value_info*>(buffer));
		case B_MULTI_SET_MIX:
			return _SetMix(static_cast<multi_mix_value_info*>(buffer));
		case B_MULTI_GET_BUFFERS:
			return _GetBuffers(static_cast<multi_buffer_list*>(buffer));
		case B_MULTI_BUFFER_FORCE_STOP:
			return _ForceStop();
		case B_MULTI_BUFFER_EXCHANGE:
			return _BufferExchange(
				static_cast<multi_buffer_info*>(buffer));
		default:
			return B_DEV_INVALID_IOCTL;
	}
}

} // namespace jr::byt_audio
