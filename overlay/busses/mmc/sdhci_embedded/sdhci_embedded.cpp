// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot

// C-ABI glue between Haiku's device_manager and the embedded SDHCI stack.
//
// This is the only file that speaks the module_info dialect; everything below
// it is clean C++. It creates exactly two nodes:
//
//   node #1  controller  (this driver_module)   <- binds the ACPI device
//   node #2  disk        (the device_module)    -> published to /dev/disk/mmc
//
// BytAcpi mirrors Haiku's ACPI SDHCI preconditions and identifies Bay Trail
// controller roles by HID. The Winky BSP removes the competing generic sdhci
// add-on at image composition time, so a positive claim has one hardware owner.

#include <new>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ACPI.h>
#include <common/iosf_mbi.h>
#include <device_manager.h>
#include <Drivers.h>
#include <KernelExport.h>
#include <fs/devfs.h>
#include "IORequest.h"

#include "BytAcpi.h"
#include "Card.h"
#include "Disk.h"
#include "SdhciController.h"
#include "Trace.h"

using namespace jr::sdhci;


device_manager_info* gDeviceManager = nullptr;
iosf_mbi_module_info* gIosfMbi = nullptr;


static const char* const kControllerModuleName
	= "busses/mmc/sdhci_embedded/controller/driver_v1";
static const char* const kDiskModuleName
	= "busses/mmc/sdhci_embedded/disk/device_v1";
static const char* const kDiskIdGenerator = "sdhci_embedded/disk";
static const TraceLabel kModuleTrace = { "sdhci_emb" };


// ---------------------------------------------------------------------------
// node #1: controller driver_module
// ---------------------------------------------------------------------------

static float
sdhci_embedded_supports_device(device_node* node)
{
	static bool sReportedProbePath;
	if (!sReportedProbePath) {
		sReportedProbePath = true;
		JR_TRACE_ALWAYS(kModuleTrace, "device-manager probe path active\n");
	}

	const MatchProfile* profile = ProfileForBytAcpiNode(node);

	// Haiku's ACPI enumerator asks every busses/mmc driver about every ACPI
	// device. Report the route once above, then keep the broad scan quiet while
	// making SDHCI-class candidates visible before the BYT HID filter decides
	// whether we own one.
	const char* cid = nullptr;
	if (gDeviceManager->get_attr_string(node, ACPI_DEVICE_CID_ITEM, &cid, false)
			== B_OK
		&& strcmp(cid, "PNP0D40") == 0) {
		const char* hid = nullptr;
		const char* uid = nullptr;
		gDeviceManager->get_attr_string(node, ACPI_DEVICE_HID_ITEM, &hid, false);
		gDeviceManager->get_attr_string(node, ACPI_DEVICE_UID_ITEM, &uid, false);
		JR_TRACE_ALWAYS(kModuleTrace, "probing ACPI %s:%s (%s)\n",
			hid != nullptr ? hid : "<unknown>", uid != nullptr ? uid : "<none>",
			profile != nullptr ? "Bay Trail profile" : "not a supported BYT host");
	}

	return profile != nullptr ? 1.0f : 0.0f;
}


static status_t
sdhci_embedded_register_device(device_node* node)
{
	const MatchProfile* profile = ProfileForBytAcpiNode(node);
	if (profile == nullptr)
		return B_BAD_VALUE;

	const char* hid = nullptr;
	const char* uid = nullptr;
	gDeviceManager->get_attr_string(node, ACPI_DEVICE_HID_ITEM, &hid, false);
	gDeviceManager->get_attr_string(node, ACPI_DEVICE_UID_ITEM, &uid, false);
	JR_TRACE_ALWAYS(kModuleTrace, "claiming ACPI %s:%s as %s\n",
		hid != nullptr ? hid : "<unknown>", uid != nullptr ? uid : "<none>",
		profile->prettyName);

	device_attr attrs[] = {
		{ B_DEVICE_PRETTY_NAME, B_STRING_TYPE, { .string = profile->prettyName } },
		{ B_DEVICE_FLAGS, B_UINT32_TYPE, { .ui32 = B_KEEP_DRIVER_LOADED } },
		{ nullptr }
	};

	return gDeviceManager->register_node(node, kControllerModuleName, attrs,
		nullptr, nullptr);
}


static status_t
sdhci_embedded_init_driver(device_node* node, void** cookie)
{
	SdhciController* controller = new(std::nothrow) SdhciController(node);
	if (controller == nullptr)
		return B_NO_MEMORY;

	// Publish synchronously so boot-media discovery precedes the RAMDisk fallback.
	status_t status = controller->Boot();
	if (status != B_OK) {
		JR_ERROR(kModuleTrace, "controller boot failed: %" B_PRId32 "\n", status);
		delete controller;
		return status;
	}

	*cookie = controller;
	return B_OK;
}


static void
sdhci_embedded_uninit_driver(void* cookie)
{
	delete static_cast<SdhciController*>(cookie);
}


static status_t
sdhci_embedded_register_child_devices(void* cookie)
{
	SdhciController* controller = static_cast<SdhciController*>(cookie);

	int32 id = gDeviceManager->create_id(kDiskIdGenerator);
	if (id < 0)
		return id;

	// Standard path: /dev/disk/mmc/<id>/raw. Standard forever.
	char name[64];
	snprintf(name, sizeof(name), "disk/mmc/%" B_PRId32 "/raw", id);

	return gDeviceManager->publish_device(controller->Node(), name,
		kDiskModuleName);
}


static driver_module_info sControllerModule = {
	{
		kControllerModuleName,
		0,
		nullptr
	},
	sdhci_embedded_supports_device,
	sdhci_embedded_register_device,
	sdhci_embedded_init_driver,
	sdhci_embedded_uninit_driver,
	sdhci_embedded_register_child_devices,
	nullptr,	// rescan_child_devices
	nullptr,	// device_removed
};


// ---------------------------------------------------------------------------
// node #2: disk device_module (devfs entry points -> Disk strategy)
// ---------------------------------------------------------------------------

struct DiskCookie {
	SdhciController*	controller;
};


struct DiskHandle {
	DiskCookie* device;
};


static status_t
sdhci_embedded_disk_init_device(void* driverCookie, void** deviceCookie)
{
	// driverCookie is the controller node's driver cookie (the controller).
	SdhciController* controller = static_cast<SdhciController*>(driverCookie);

	DiskCookie* cookie = new(std::nothrow) DiskCookie;
	if (cookie == nullptr)
		return B_NO_MEMORY;

	cookie->controller = controller;
	if (controller->ActiveDisk() == nullptr) {
		delete cookie;
		return B_NO_INIT;
	}
	*deviceCookie = cookie;
	return B_OK;
}


static void
sdhci_embedded_disk_uninit_device(void* deviceCookie)
{
	delete static_cast<DiskCookie*>(deviceCookie);
}


static status_t
sdhci_embedded_disk_open(void* deviceCookie, const char* /*path*/,
	int /*openMode*/, void** _cookie)
{
	DiskHandle* handle = new(std::nothrow) DiskHandle;
	if (handle == nullptr)
		return B_NO_MEMORY;
	handle->device = static_cast<DiskCookie*>(deviceCookie);
	*_cookie = handle;
	return B_OK;
}


static status_t
sdhci_embedded_disk_close(void* /*cookie*/)
{
	return B_OK;
}


static status_t
sdhci_embedded_disk_free(void* cookie)
{
	delete static_cast<DiskHandle*>(cookie);
	return B_OK;
}


static Disk*
active_disk(DiskHandle* handle)
{
	return handle != nullptr && handle->device != nullptr
		? handle->device->controller->ActiveDisk() : nullptr;
}


static status_t
sdhci_embedded_disk_read(void* cookie, off_t position, void* buffer,
	size_t* _length)
{
	Disk* disk = active_disk(static_cast<DiskHandle*>(cookie));
	if (disk == nullptr || buffer == nullptr || _length == nullptr)
		return B_NO_INIT;
	if (position < 0)
		return B_BAD_VALUE;

	const uint64_t size = disk->Capacity() * disk->BlockSize();
	if (static_cast<uint64_t>(position) >= size)
		return ERANGE;
	size_t length = *_length;
	if (length > size - static_cast<uint64_t>(position))
		length = static_cast<size_t>(size - static_cast<uint64_t>(position));

	IORequest request;
	status_t status = request.Init(position, reinterpret_cast<addr_t>(buffer),
		length, false, 0);
	if (status != B_OK)
		return status;
	status = disk->DoIO(&request);
	if (status != B_OK)
		return status;
	status = request.Wait(0, 0);
	*_length = request.TransferredBytes();
	return status;
}


static status_t
sdhci_embedded_disk_write(void* cookie, off_t position, const void* buffer,
	size_t* _length)
{
	Disk* disk = active_disk(static_cast<DiskHandle*>(cookie));
	if (disk == nullptr || buffer == nullptr || _length == nullptr)
		return B_NO_INIT;
	if (position < 0)
		return B_BAD_VALUE;

	const uint64_t size = disk->Capacity() * disk->BlockSize();
	if (static_cast<uint64_t>(position) >= size)
		return ERANGE;
	size_t length = *_length;
	if (length > size - static_cast<uint64_t>(position))
		length = static_cast<size_t>(size - static_cast<uint64_t>(position));

	IORequest request;
	status_t status = request.Init(position, reinterpret_cast<addr_t>(buffer),
		length, true, 0);
	if (status != B_OK)
		return status;
	status = disk->DoIO(&request);
	if (status != B_OK)
		return status;
	status = request.Wait(0, 0);
	*_length = request.TransferredBytes();
	return status;
}


static status_t
sdhci_embedded_disk_io(void* cookie, io_request* request)
{
	Disk* disk = active_disk(static_cast<DiskHandle*>(cookie));
	if (disk == nullptr)
		return B_NO_INIT;
	return disk->DoIO(request);
}


static status_t
sdhci_embedded_disk_control(void* cookie, uint32 op, void* buffer, size_t length);


static status_t
sdhci_embedded_disk_control_impl(DiskHandle* handle, uint32 op, void* buffer,
	size_t length)
{
	Disk* disk = active_disk(handle);
	if (disk == nullptr)
		return B_NO_INIT;
	SdhciController* controller = handle->device->controller;

	switch (op) {
		case B_GET_DEVICE_SIZE:
		{
			uint64_t bytes = disk->Capacity() * disk->BlockSize();
			if (buffer == nullptr || length < sizeof(size_t))
				return B_BAD_VALUE;
			if (bytes > SIZE_MAX)
				return B_NOT_SUPPORTED;
			size_t value = static_cast<size_t>(bytes);
			return user_memcpy(buffer, &value, sizeof(value));
		}

		case B_GET_GEOMETRY:
		{
			if (buffer == nullptr || length < sizeof(device_geometry))
				return B_BAD_VALUE;

			device_geometry geometry;
			memset(&geometry, 0, sizeof(geometry));
			devfs_compute_geometry_size(&geometry, disk->Capacity(),
				disk->BlockSize());
			geometry.device_type = B_DISK;
			geometry.removable = controller->IsRemovable();
			geometry.read_only = false;
			geometry.write_once = false;
			geometry.bytes_per_physical_sector = disk->BlockSize();
			return user_memcpy(buffer, &geometry, sizeof(geometry));
		}

		case B_GET_MEDIA_STATUS:
		{
			if (buffer == nullptr || length < sizeof(status_t))
				return B_BAD_VALUE;
			status_t status = controller->MediaPresent() ? B_OK : B_DEV_NO_MEDIA;
			return user_memcpy(buffer, &status, sizeof(status));
		}

		case B_FLUSH_DRIVE_CACHE:
			return controller->ActiveCard()->Flush(controller->Engine());
	}

	return B_DEV_INVALID_IOCTL;
}


static status_t
sdhci_embedded_disk_control(void* cookie, uint32 op, void* buffer, size_t length)
{
	return sdhci_embedded_disk_control_impl(static_cast<DiskHandle*>(cookie), op,
		buffer, length);
}


static device_module_info sDiskModule = {
	{
		kDiskModuleName,
		0,
		nullptr
	},
	sdhci_embedded_disk_init_device,
	sdhci_embedded_disk_uninit_device,
	nullptr,	// device_removed
	sdhci_embedded_disk_open,
	sdhci_embedded_disk_close,
	sdhci_embedded_disk_free,
	sdhci_embedded_disk_read,
	sdhci_embedded_disk_write,
	sdhci_embedded_disk_io,
	sdhci_embedded_disk_control,
	nullptr,	// select
	nullptr,	// deselect
};


// ---------------------------------------------------------------------------
// module registry
// ---------------------------------------------------------------------------

module_dependency module_dependencies[] = {
	{ B_DEVICE_MANAGER_MODULE_NAME, (module_info**)&gDeviceManager },
	{ B_IOSF_MBI_MODULE_NAME, (module_info**)&gIosfMbi },
	{}
};


module_info* modules[] = {
	(module_info*)&sControllerModule,
	(module_info*)&sDiskModule,
	nullptr
};
