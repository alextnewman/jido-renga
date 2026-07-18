// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot

#include "Driver.h"

#include <common/intel_valleyview/FirmwareState.h>
#include <common/intel_valleyview/Protocol.h>

#include <driver_settings.h>
#include <KernelExport.h>

#include <boot_item.h>
#include <frame_buffer_console.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vm/vm.h>


device_manager_info* gDeviceManager;


namespace {

struct Settings {
	bool	enabled;
	bool	allowModeset;
};


Settings
ReadSettings()
{
	Settings settings = {
		valleyview::kDefaultEnabled,
		valleyview::kDefaultAllowModeset
	};

	void* handle = load_driver_settings("intel_valleyview");
	if (handle == NULL)
		return settings;

	settings.enabled = get_driver_boolean_parameter(handle, "enabled",
		settings.enabled, true);
	settings.allowModeset = get_driver_boolean_parameter(handle, "allow_modeset",
		settings.allowModeset, true);
	unload_driver_settings(handle);
	return settings;
}


uint32
ReadMmio(const volatile uint8* registers, uint32 offset)
{
	return *(const volatile uint32*)(registers + offset);
}


status_t
CaptureMmio(ValleyViewDevice& device)
{
	valleyview::FirmwareSnapshot& snapshot = device.snapshot;
	if ((device.pciInfo.u.h0.base_register_flags[0] & PCI_address_space) != 0)
		return B_BAD_DATA;

	phys_addr_t physical = device.pciInfo.u.h0.base_registers[0];
	if ((device.pciInfo.u.h0.base_register_flags[0] & PCI_address_type)
			== PCI_address_type_64) {
		physical |= (uint64)device.pciInfo.u.h0.base_registers[1] << 32;
	}

	const size_t size = device.pciInfo.u.h0.base_register_sizes[0];
	snapshot.mmioPhysical = physical;
	snapshot.mmioSize = size;
	if (physical == 0 || size < valleyview::kLastSnapshotRegister + sizeof(uint32))
		return B_BAD_DATA;

	volatile uint8* registers = NULL;
	area_id area = map_physical_memory("intel_valleyview snapshot", physical,
		size, B_ANY_KERNEL_BLOCK_ADDRESS | B_UNCACHED_MEMORY,
		B_KERNEL_READ_AREA, (void**)&registers);
	if (area < B_OK)
		return area;

	snapshot.flags |= valleyview::kSnapshotMmioMapped;
	snapshot.dpllA = ReadMmio(registers, valleyview::kDpllA);
	snapshot.hTotal = ReadMmio(registers, valleyview::kHTotalA);
	snapshot.hBlank = ReadMmio(registers, valleyview::kHBlankA);
	snapshot.hSync = ReadMmio(registers, valleyview::kHSyncA);
	snapshot.vTotal = ReadMmio(registers, valleyview::kVTotalA);
	snapshot.vBlank = ReadMmio(registers, valleyview::kVBlankA);
	snapshot.vSync = ReadMmio(registers, valleyview::kVSyncA);
	snapshot.pipeSource = ReadMmio(registers, valleyview::kPipeSourceA);
	snapshot.pipeConfig = ReadMmio(registers, valleyview::kPipeConfigA);
	snapshot.planeAddressVlv
		= ReadMmio(registers, valleyview::kPlaneAddressVlvA);
	snapshot.planeControl = ReadMmio(registers, valleyview::kPlaneControlA);
	snapshot.planeLinearOffset
		= ReadMmio(registers, valleyview::kPlaneLinearOffsetA);
	snapshot.planeStride = ReadMmio(registers, valleyview::kPlaneStrideA);
	snapshot.planeSurface = ReadMmio(registers, valleyview::kPlaneSurfaceA);
	snapshot.planeSurfaceLive
		= ReadMmio(registers, valleyview::kPlaneSurfaceLiveA);
	snapshot.planeTileOffset
		= ReadMmio(registers, valleyview::kPlaneTileOffsetA);
	snapshot.panelFitterControl
		= ReadMmio(registers, valleyview::kPanelFitterControl);
	snapshot.panelFitterProgrammedRatios
		= ReadMmio(registers, valleyview::kPanelFitterProgrammedRatios);
	snapshot.panelFitterAutoRatios
		= ReadMmio(registers, valleyview::kPanelFitterAutoRatios);
	snapshot.dpC = ReadMmio(registers, valleyview::kDpC);
	snapshot.ppsStatus = ReadMmio(registers, valleyview::kPpsStatusA);
	snapshot.ppsControl = ReadMmio(registers, valleyview::kPpsControlA);
	snapshot.ppsOnDelays = ReadMmio(registers, valleyview::kPpsOnDelaysA);
	snapshot.ppsOffDelays = ReadMmio(registers, valleyview::kPpsOffDelaysA);
	snapshot.ppsDivisor = ReadMmio(registers, valleyview::kPpsDivisorA);
	snapshot.pwmControl2 = ReadMmio(registers, valleyview::kPwmControl2A);
	snapshot.pwmControl = ReadMmio(registers, valleyview::kPwmControlA);
	snapshot.cursorControl = ReadMmio(registers, valleyview::kCursorControlA);
	snapshot.cursorBase = ReadMmio(registers, valleyview::kCursorBaseA);
	snapshot.cursorPosition = ReadMmio(registers, valleyview::kCursorPositionA);

	// GMADR (BAR2 on Gen4+) is the CPU-visible GGTT aperture.
	if ((device.pciInfo.u.h0.base_register_flags[2] & PCI_address_space) == 0) {
		uint64 gmadr = device.pciInfo.u.h0.base_registers[2];
		if ((device.pciInfo.u.h0.base_register_flags[2] & PCI_address_type)
				== PCI_address_type_64) {
			gmadr |= (uint64)device.pciInfo.u.h0.base_registers[3] << 32;
		}
		snapshot.gmadrBase = gmadr;
		snapshot.gmadrSize = device.pciInfo.u.h0.base_register_sizes[2];
	}

	// Linux's firmware-state readout uses DSPSURF plus DSPLINOFF for display
	// version 4+, including ValleyView. Validate every GTT entry covering the
	// linear scanout footprint; PTE backing addresses are diagnostic only.
	snapshot.planeGgttOffset = snapshot.planeSurface
		& ~valleyview::kPageMask;
	const uint64 scanoutOffset
		= static_cast<uint64>(snapshot.planeGgttOffset)
			+ snapshot.planeLinearOffset;
	const uint64 footprint = static_cast<uint64>(snapshot.planeStride)
		* ((snapshot.pipeSource & 0xffff) + 1);
	const uint64 firstPage = scanoutOffset / valleyview::kPageSize;
	const uint64 pageOffset = scanoutOffset & valleyview::kPageMask;
	if (footprint != 0 && footprint <= UINT64_MAX - pageOffset) {
		const uint64 pageCount = (pageOffset + footprint
				+ valleyview::kPageMask) / valleyview::kPageSize;
		const uint64 firstPteOffset = valleyview::kGttOffsetInBar
			+ firstPage * valleyview::kGen7PteSize;
		const uint64 pteBytes = pageCount * valleyview::kGen7PteSize;
		if (pageCount <= UINT32_MAX
			&& valleyview::RangeFits(firstPteOffset, pteBytes, size)) {
			snapshot.gttRequiredPages = static_cast<uint32>(pageCount);
			for (uint32 index = 0; index < snapshot.gttRequiredPages;
					index++) {
				const uint32 pte = ReadMmio(registers,
					static_cast<uint32>(firstPteOffset
						+ index * valleyview::kGen7PteSize));
				if (index == 0)
					snapshot.gttPte = pte;
				if ((pte & valleyview::kGen7PtePresent) != 0)
					snapshot.gttPresentPages++;
			}
		}
	}
	delete_area(area);

	valleyview::DecodeFirmwareSnapshot(snapshot);
	return B_OK;
}


status_t
CaptureBootFramebuffer(ValleyViewDevice& device)
{
	frame_buffer_boot_info* info = (frame_buffer_boot_info*)get_boot_item(
		FRAME_BUFFER_BOOT_INFO, NULL);
	if (info == NULL)
		return B_ENTRY_NOT_FOUND;
	if (info->physical_frame_buffer == 0 || info->width <= 0
		|| info->height <= 0 || info->depth <= 0
		|| info->bytes_per_row <= 0) {
		return B_BAD_DATA;
	}

	valleyview::FirmwareSnapshot& snapshot = device.snapshot;
	snapshot.bootFramebufferArea = info->area;
	snapshot.bootFramebufferPhysical = info->physical_frame_buffer;
	snapshot.bootWidth = info->width;
	snapshot.bootHeight = info->height;
	snapshot.bootDepth = info->depth;
	snapshot.bootBytesPerRow = info->bytes_per_row;
	snapshot.bootFramebufferSize
		= static_cast<uint64>(info->bytes_per_row) * info->height;
	return B_OK;
}


uint32
ReadLe32(const uint8* bytes)
{
	return static_cast<uint32>(bytes[0])
		| static_cast<uint32>(bytes[1]) << 8
		| static_cast<uint32>(bytes[2]) << 16
		| static_cast<uint32>(bytes[3]) << 24;
}


uint64
ReadLe64(const uint8* bytes)
{
	return static_cast<uint64>(ReadLe32(bytes))
		| static_cast<uint64>(ReadLe32(bytes + 4)) << 32;
}


bool
CaptureRvdaVbt(valleyview::FirmwareSnapshot& snapshot,
	const uint8* opRegionBytes)
{
	if (snapshot.opRegionMajor < 2
		|| (snapshot.opRegionMboxes & valleyview::kMailboxAsle) == 0) {
		return false;
	}

	const uint8* asle = opRegionBytes + valleyview::kOpRegionAsleOffset;
	uint64 address = ReadLe64(asle + valleyview::kAsleRvdaOffset);
	const uint32 size = ReadLe32(asle + valleyview::kAsleRvdsOffset);
	if (address == 0 || size < 4 || size > valleyview::kMaxVbtSize)
		return false;

	if (snapshot.opRegionMajor > 2
		|| (snapshot.opRegionMajor == 2 && snapshot.opRegionMinor >= 1)) {
		if (address < valleyview::kOpRegionSize)
			return false;
		address += snapshot.asls;
	}

	const uint8* bytes = NULL;
	area_id area = map_physical_memory("intel_valleyview RVDA", address, size,
		B_ANY_KERNEL_ADDRESS, B_KERNEL_READ_AREA, (void**)&bytes);
	if (area < B_OK)
		return false;

	uint32 declaredSize;
	const bool valid = valleyview::ValidateVbt(bytes, size, declaredSize);
	delete_area(area);
	if (!valid)
		return false;

	snapshot.flags |= valleyview::kSnapshotVbtPresent;
	snapshot.vbtAddress = address;
	snapshot.vbtSize = declaredSize;
	snapshot.vbtSource = valleyview::kVbtRvda;
	return true;
}


status_t
CaptureOpRegion(ValleyViewDevice& device)
{
	valleyview::FirmwareSnapshot& snapshot = device.snapshot;
	snapshot.asls = device.pci->read_pci_config(device.pciDevice,
		valleyview::kOpRegionAsls, 4);
	if (snapshot.asls == 0)
		return B_ENTRY_NOT_FOUND;

	const uint8* bytes = NULL;
	area_id area = map_physical_memory("intel_valleyview OpRegion",
		snapshot.asls, valleyview::kOpRegionSize, B_ANY_KERNEL_ADDRESS,
		B_KERNEL_READ_AREA, (void**)&bytes);
	if (area < B_OK)
		return area;

	if (!valleyview::HasSignature(bytes, valleyview::kOpRegionSize,
			"IntelGraphicsMem", 16)) {
		delete_area(area);
		return B_BAD_DATA;
	}

	snapshot.flags |= valleyview::kSnapshotOpRegionValid;
	snapshot.opRegionSizeKiB = static_cast<uint16>(ReadLe32(bytes + 16));
	snapshot.opRegionRevision = bytes[21];
	snapshot.opRegionMinor = bytes[22];
	snapshot.opRegionMajor = bytes[23];
	snapshot.opRegionMboxes
		= ReadLe32(bytes + valleyview::kOpRegionMboxesOffset);

	if (!CaptureRvdaVbt(snapshot, bytes)) {
		uint32 vbtOffset;
		if (valleyview::FindVbt(bytes + valleyview::kOpRegionVbtOffset,
				valleyview::kOpRegionSize - valleyview::kOpRegionVbtOffset,
				vbtOffset)) {
			const uint8* vbt = bytes + valleyview::kOpRegionVbtOffset
				+ vbtOffset;
			const size_t mappedSize = valleyview::kOpRegionSize
				- valleyview::kOpRegionVbtOffset - vbtOffset;
			uint32 declaredSize;
			if (valleyview::ValidateVbt(vbt, mappedSize, declaredSize)) {
				snapshot.vbtAddress = snapshot.asls
					+ valleyview::kOpRegionVbtOffset + vbtOffset;
				snapshot.vbtSize = declaredSize;
				snapshot.vbtSource = valleyview::kVbtInline;
				snapshot.flags |= valleyview::kSnapshotVbtPresent;
			}
		}
	}

	delete_area(area);
	return B_OK;
}


float
SupportsDevice(device_node* parent)
{
	const char* bus;
	if (gDeviceManager->get_attr_string(parent, B_DEVICE_BUS, &bus, false)
			!= B_OK
		|| strcmp(bus, "pci") != 0) {
		return 0.0;
	}

	uint16 vendorId;
	uint16 deviceId;
	if (gDeviceManager->get_attr_uint16(parent, B_DEVICE_VENDOR_ID, &vendorId,
			false) != B_OK
		|| gDeviceManager->get_attr_uint16(parent, B_DEVICE_ID, &deviceId,
			false) != B_OK) {
		return -1.0;
	}

	return valleyview::IsSupportedDevice(vendorId, deviceId) ? 0.8 : 0.0;
}


status_t
RegisterDevice(device_node* parent)
{
	device_attr attributes[] = {
		{B_DEVICE_PRETTY_NAME, B_STRING_TYPE,
			{.string = "Intel ValleyView Graphics"}},
		{NULL}
	};

	return gDeviceManager->register_node(parent, kValleyViewDriverModuleName,
		attributes, NULL, NULL);
}


status_t
InitDriver(device_node* node, void** cookie)
{
	if (node == NULL || cookie == NULL)
		return B_BAD_VALUE;

	ValleyViewDevice* device
		= (ValleyViewDevice*)calloc(1, sizeof(ValleyViewDevice));
	if (device == NULL)
		return B_NO_MEMORY;

	device->node = node;
	device->sharedArea = -1;
	device->framebufferArea = -1;
	device_node* parent = gDeviceManager->get_parent_node(node);
	if (parent == NULL) {
		free(device);
		return B_NO_INIT;
	}

	status_t status = gDeviceManager->get_driver(parent,
		(driver_module_info**)&device->pci, (void**)&device->pciDevice);
	if (status == B_OK)
		device->pci->get_pci_info(device->pciDevice, &device->pciInfo);
	gDeviceManager->put_node(parent);
	if (status != B_OK) {
		free(device);
		return status;
	}

	if (!valleyview::IsSupportedDevice(device->pciInfo.vendor_id,
			device->pciInfo.device_id)) {
		free(device);
		return B_BAD_VALUE;
	}

	const Settings settings = ReadSettings();
	device->enabled = settings.enabled;
	device->allowModeset = settings.enabled && settings.allowModeset;
	mutex_init(&device->lock, "intel_valleyview device");

	device->snapshot.header
		= valleyview::MakeAbiHeader(sizeof(device->snapshot));
	device->snapshot.generation = 1;
	device->snapshot.mmioStatus = CaptureMmio(*device);
	device->snapshot.opRegionStatus = CaptureOpRegion(*device);
	device->snapshot.bootFramebufferStatus = CaptureBootFramebuffer(*device);
	valleyview::DecodeFirmwareSnapshot(device->snapshot);
	device->snapshot.adoptionStatus
		= (device->snapshot.flags
			& valleyview::kSnapshotAdoptionCompatible) != 0
		? B_OK : B_BAD_DATA;

	dprintf("intel_valleyview: found %04x:%04x at %02x:%02x.%x; driver is %s; "
		"modeset is %s; device publication is %s\n",
		device->pciInfo.vendor_id, device->pciInfo.device_id,
		device->pciInfo.bus, device->pciInfo.device, device->pciInfo.function,
		device->enabled ? "enabled" : "disabled",
		device->allowModeset ? "allowed" : "blocked",
		device->enabled && valleyview::kDevicePublicationReady
			? "ready" : "blocked");
	dprintf("intel_valleyview: snapshot mmio=%" B_PRId32
		" opregion=%" B_PRId32 " flags=%#" B_PRIx32
		" mode=%ux%u total=%ux%u stride=%" B_PRIu32
		" pipe=%#" B_PRIx32 " plane=%#" B_PRIx32 " dp_c=%#" B_PRIx32
		" pps=%#" B_PRIx32 " pwm=%u/%u boot=%" B_PRId32
		" adopt=%" B_PRId32 "\n",
		device->snapshot.mmioStatus, device->snapshot.opRegionStatus,
		device->snapshot.flags, device->snapshot.hDisplay,
		device->snapshot.vDisplay, device->snapshot.hTotalPixels,
		device->snapshot.vTotalLines, device->snapshot.planeStride,
		device->snapshot.pipeConfig, device->snapshot.planeControl,
		device->snapshot.dpC, device->snapshot.ppsStatus,
		device->snapshot.pwmDuty, device->snapshot.pwmPeriod,
		device->snapshot.bootFramebufferStatus,
		device->snapshot.adoptionStatus);

	*cookie = device;
	return B_OK;
}


void
UninitDriver(void* cookie)
{
	ValleyViewDevice* device = (ValleyViewDevice*)cookie;
	if (device == NULL)
		return;

	if (device->sharedArea >= B_OK)
		delete_area(device->sharedArea);
	if (device->framebufferArea >= B_OK)
		delete_area(device->framebufferArea);
	mutex_destroy(&device->lock);
	free(device);
}


status_t
RegisterChildDevices(void* cookie)
{
	ValleyViewDevice* device = (ValleyViewDevice*)cookie;
	if (device == NULL)
		return B_BAD_VALUE;

	status_t status = gDeviceManager->publish_device(device->node,
		"misc/intel_valleyview_probe", kValleyViewDeviceModuleName);
	if (status != B_OK)
		return status;

	if (!device->enabled || !valleyview::kDevicePublicationReady)
		return B_OK;

	return PublishValleyViewGraphics(*device);
}

} // namespace


status_t
PublishValleyViewGraphics(ValleyViewDevice& device)
{
	mutex_lock(&device.lock);
	if (device.graphicsPublished) {
		mutex_unlock(&device.lock);
		return B_OK;
	}
	if (!valleyview::kDevicePublicationReady
		|| device.snapshot.adoptionStatus != B_OK) {
		mutex_unlock(&device.lock);
		return B_NOT_ALLOWED;
	}

	if (device.framebufferArea < B_OK) {
		device.framebufferArea = map_physical_memory(
			"intel_valleyview framebuffer",
			device.snapshot.bootFramebufferPhysical,
			device.snapshot.bootFramebufferSize, B_ANY_KERNEL_ADDRESS,
			B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA, &device.framebuffer);
		if (device.framebufferArea < B_OK) {
			status_t status = device.framebufferArea;
			device.framebufferArea = -1;
			mutex_unlock(&device.lock);
			return status;
		}

		status_t status = vm_set_area_memory_type(device.framebufferArea,
			device.snapshot.bootFramebufferPhysical,
			B_WRITE_COMBINING_MEMORY);
		if (status != B_OK) {
			delete_area(device.framebufferArea);
			device.framebufferArea = -1;
			device.framebuffer = NULL;
			mutex_unlock(&device.lock);
			return status;
		}
	}

	if (device.sharedArea < B_OK) {
		device.sharedArea = create_area("intel_valleyview shared info",
			(void**)&device.sharedInfo, B_ANY_KERNEL_ADDRESS, B_PAGE_SIZE,
			B_FULL_LOCK, B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA
				| B_CLONEABLE_AREA);
		if (device.sharedArea < B_OK) {
			status_t status = device.sharedArea;
			device.sharedArea = -1;
			delete_area(device.framebufferArea);
			device.framebufferArea = -1;
			device.framebuffer = NULL;
			mutex_unlock(&device.lock);
			return status;
		}

		memset(device.sharedInfo, 0, sizeof(*device.sharedInfo));
		device.sharedInfo->header
			= valleyview::MakeAbiHeader(sizeof(*device.sharedInfo));
		device.sharedInfo->modeListArea = -1;
		device.sharedInfo->currentMode.virtual_width
			= device.snapshot.bootWidth;
		device.sharedInfo->currentMode.virtual_height
			= device.snapshot.bootHeight;
		device.sharedInfo->currentMode.space = B_RGB32;
		device.sharedInfo->bytesPerRow = device.snapshot.bootBytesPerRow;
		device.sharedInfo->framebufferPhysical
			= device.snapshot.bootFramebufferPhysical;
		device.sharedInfo->framebufferSize
			= device.snapshot.bootFramebufferSize;
	}
	char name[B_PATH_NAME_LENGTH];
	snprintf(name, sizeof(name), "graphics/intel_valleyview_%02x%02x%02x",
		device.pciInfo.bus, device.pciInfo.device,
		device.pciInfo.function);
	// Publish under device.lock so a second concurrent caller cannot pass the
	// graphicsPublished guard above and republish the same devfs path.
	status_t status = gDeviceManager->publish_device(device.node, name,
		kValleyViewDeviceModuleName);
	if (status == B_OK)
		device.graphicsPublished = true;
	mutex_unlock(&device.lock);
	return status;
}


module_dependency module_dependencies[] = {
	{B_DEVICE_MANAGER_MODULE_NAME, (module_info**)&gDeviceManager},
	{}
};

driver_module_info gValleyViewDriverModule = {
	{
		kValleyViewDriverModuleName,
		0,
		NULL
	},

	SupportsDevice,
	RegisterDevice,
	InitDriver,
	UninitDriver,
	RegisterChildDevices,
	NULL,
	NULL,
	NULL,
	NULL
};

module_info* modules[] = {
	(module_info*)&gValleyViewDriverModule,
	(module_info*)&gValleyViewDeviceModule,
	NULL
};
