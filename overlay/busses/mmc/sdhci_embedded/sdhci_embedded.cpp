// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot

// C-ABI glue between Haiku's device_manager and the embedded SDHCI stack.
//
// This is the ONLY file that speaks the module_info dialect; everything below
// it is clean C++. It creates exactly two nodes:
//
//   node #1  controller  (this driver_module)   <- binds the ACPI device
//   node #2  disk        (the device_module)    -> published to /dev/disk/mmc
//
// Winning the match: supports_device() consults the pure ExplicitMatcher, which
// returns 0.9 for known Bay Trail silicon -- strictly greater than upstream
// sdhci's generic 0.8 -- so device_manager binds us exclusively and the generic
// mmc_bus chain never loads. On anything else we return 0.0 and stay invisible.

#include <new>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ACPI.h>
#include <device_manager.h>
#include <Drivers.h>
#include <KernelExport.h>

#include "Disk.h"
#include "Matcher.h"
#include "SdhciController.h"

using namespace jr::sdhci;


device_manager_info* gDeviceManager = nullptr;


static const char* const kControllerModuleName
	= "busses/mmc/sdhci_embedded/controller/driver_v1";
static const char* const kDiskModuleName
	= "busses/mmc/sdhci_embedded/disk/device_v1";
static const char* const kDiskIdGenerator = "sdhci_embedded/disk";


// ---------------------------------------------------------------------------
// node #1: controller driver_module
// ---------------------------------------------------------------------------

static float
sdhci_embedded_supports_device(device_node* node)
{
	const char* bus;
	if (gDeviceManager->get_attr_string(node, B_DEVICE_BUS, &bus, true) != B_OK)
		return kNoMatchScore;
	if (strcmp(bus, "acpi") != 0)
		return kNoMatchScore;

	// A real SDHCI host advertises the PNP0D40 compatible id; bail early if not.
	const char* cid;
	if (gDeviceManager->get_attr_string(node, ACPI_DEVICE_CID_ITEM, &cid, false)
			== B_OK
		&& strcmp(cid, "PNP0D40") != 0) {
		return kNoMatchScore;
	}

	const char* hid;
	if (gDeviceManager->get_attr_string(node, ACPI_DEVICE_HID_ITEM, &hid, false)
			!= B_OK) {
		return kNoMatchScore;
	}

	const char* uidString;
	uint32_t uid = kAnyUid;
	if (gDeviceManager->get_attr_string(node, ACPI_DEVICE_UID_ITEM, &uidString,
			false) == B_OK) {
		char* end = nullptr;
		unsigned long parsed = strtoul(uidString, &end, 10);
		if (end != uidString)
			uid = static_cast<uint32_t>(parsed);
	}

	// Pure decision: 0.9 on a hit (we win), 0.0 otherwise (upstream untouched).
	return ScoreFor(hid, uid);
}


static status_t
sdhci_embedded_register_device(device_node* node)
{
	const char* hid = nullptr;
	const char* uidString = nullptr;
	uint32_t uid = kAnyUid;
	gDeviceManager->get_attr_string(node, ACPI_DEVICE_HID_ITEM, &hid, false);
	if (gDeviceManager->get_attr_string(node, ACPI_DEVICE_UID_ITEM, &uidString,
			false) == B_OK) {
		char* end = nullptr;
		unsigned long parsed = strtoul(uidString, &end, 10);
		if (end != uidString)
			uid = static_cast<uint32_t>(parsed);
	}

	const MatchProfile* profile = MatchProfileFor(hid, uid);
	const char* prettyName = profile != nullptr && profile->prettyName != nullptr
		? profile->prettyName : "Embedded SDHCI Controller";

	device_attr attrs[] = {
		{ B_DEVICE_PRETTY_NAME, B_STRING_TYPE, { .string = prettyName } },
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

	// Serialized boot: map, reset, identify the card, publish -- all before we
	// return, so boot-from-SD wins the RAMDisk race.
	status_t status = controller->Boot();
	if (status != B_OK) {
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
	Disk*				disk;
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
	cookie->disk = controller->ActiveDisk();
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
	*_cookie = deviceCookie;
	return B_OK;
}


static status_t
sdhci_embedded_disk_close(void* /*cookie*/)
{
	return B_OK;
}


static status_t
sdhci_embedded_disk_free(void* /*cookie*/)
{
	return B_OK;
}


static status_t
sdhci_embedded_disk_io(void* cookie, io_request* request)
{
	DiskCookie* dc = static_cast<DiskCookie*>(cookie);
	if (dc->disk == nullptr)
		return B_NO_INIT;
	return dc->disk->DoIO(request);
}


static status_t
sdhci_embedded_disk_control(void* cookie, uint32 op, void* buffer, size_t length);


static status_t
sdhci_embedded_disk_control_impl(DiskCookie* dc, uint32 op, void* buffer,
	size_t length)
{
	Disk* disk = dc->disk;
	if (disk == nullptr)
		return B_NO_INIT;

	switch (op) {
		case B_GET_DEVICE_SIZE:
		{
			uint64_t bytes = disk->Capacity() * disk->BlockSize();
			size_t value = static_cast<size_t>(bytes);
			return user_memcpy(buffer, &value, sizeof(value));
		}

		case B_GET_GEOMETRY:
		{
			if (buffer == nullptr || length < sizeof(device_geometry))
				return B_BAD_VALUE;

			device_geometry geometry;
			memset(&geometry, 0, sizeof(geometry));
			geometry.bytes_per_sector = disk->BlockSize();
			geometry.sectors_per_track = disk->Capacity();
			geometry.cylinder_count = 1;
			geometry.head_count = 1;
			geometry.device_type = B_DISK;
			geometry.removable = false;
			geometry.read_only = false;
			geometry.write_once = false;
			return user_memcpy(buffer, &geometry, sizeof(geometry));
		}

		case B_GET_MEDIA_STATUS:
		{
			status_t status = B_OK;
			return user_memcpy(buffer, &status, sizeof(status));
		}

		case B_FLUSH_DRIVE_CACHE:
			return B_OK;
	}

	return B_DEV_INVALID_IOCTL;
}


static status_t
sdhci_embedded_disk_control(void* cookie, uint32 op, void* buffer, size_t length)
{
	return sdhci_embedded_disk_control_impl(static_cast<DiskCookie*>(cookie), op,
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
	nullptr,	// read (block devices go through io)
	nullptr,	// write
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
	{}
};


module_info* modules[] = {
	(module_info*)&sControllerModule,
	(module_info*)&sDiskModule,
	nullptr
};
