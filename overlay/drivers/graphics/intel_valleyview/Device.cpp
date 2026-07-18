// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot

#include "Driver.h"

#include <common/intel_valleyview/Protocol.h>

#include <graphic_driver.h>

#include <stdio.h>
#include <string.h>
#include <vm/vm.h>


namespace {

status_t
CopyUserString(void* buffer, size_t length, const char* value)
{
	if (buffer == NULL || value == NULL)
		return B_BAD_VALUE;

	const size_t required = strlen(value) + 1;
	if (length < required)
		return B_BUFFER_OVERFLOW;

	return user_strlcpy((char*)buffer, value, length) < B_OK
		? B_BAD_ADDRESS : B_OK;
}


status_t
InitDevice(void* driverCookie, void** deviceCookie)
{
	if (driverCookie == NULL || deviceCookie == NULL)
		return B_BAD_VALUE;

	*deviceCookie = driverCookie;
	return B_OK;
}


void
UninitDevice(void*)
{
}


status_t
Open(void* deviceCookie, const char*, int, void** cookie)
{
	if (deviceCookie == NULL || cookie == NULL)
		return B_BAD_VALUE;

	ValleyViewDevice* device = (ValleyViewDevice*)deviceCookie;
	mutex_lock(&device->lock);
	device->openCount++;
	mutex_unlock(&device->lock);

	*cookie = device;
	return B_OK;
}


status_t
Close(void*)
{
	return B_OK;
}


status_t
Free(void* cookie)
{
	ValleyViewDevice* device = (ValleyViewDevice*)cookie;
	if (device == NULL)
		return B_BAD_VALUE;

	mutex_lock(&device->lock);
	if (device->openCount <= 0) {
		mutex_unlock(&device->lock);
		return B_BAD_VALUE;
	}
	device->openCount--;
	mutex_unlock(&device->lock);
	return B_OK;
}


status_t
Control(void* cookie, uint32 operation, void* buffer, size_t length)
{
	ValleyViewDevice* device = (ValleyViewDevice*)cookie;
	if (device == NULL)
		return B_BAD_VALUE;

	switch (operation) {
		case B_GET_ACCELERANT_SIGNATURE:
			return CopyUserString(buffer, length, kValleyViewAccelerantName);

		case valleyview::kGetDeviceName:
		{
			char name[B_PATH_NAME_LENGTH];
			snprintf(name, sizeof(name), "graphics/intel_valleyview_%02x%02x%02x",
				device->pciInfo.bus, device->pciInfo.device,
				device->pciInfo.function);
			return CopyUserString(buffer, length, name);
		}

		case valleyview::kGetDriverStatus:
		{
			if (buffer == NULL || length < sizeof(valleyview::DriverStatus))
				return B_BAD_VALUE;

			valleyview::DriverStatus status = {};
			status.header = valleyview::MakeAbiHeader(sizeof(status));
			if (device->snapshot.adoptionStatus == B_OK) {
				status.capabilities
					= valleyview::kCapabilityFirmwareAdoption;
				status.displayState = valleyview::kFirmwareAdopted;
			} else
				status.displayState = valleyview::kDisplayUnavailable;
			status.enabled = device->enabled ? 1 : 0;
			status.allowModeset = device->allowModeset ? 1 : 0;
			return user_memcpy(buffer, &status, sizeof(status));
		}

		case valleyview::kGetDeviceIdentity:
		{
			if (buffer == NULL || length < sizeof(valleyview::DeviceIdentity))
				return B_BAD_VALUE;

			valleyview::DeviceIdentity identity = {};
			identity.header = valleyview::MakeAbiHeader(sizeof(identity));
			identity.vendorId = device->pciInfo.vendor_id;
			identity.deviceId = device->pciInfo.device_id;
			identity.subsystemVendorId
				= device->pciInfo.u.h0.subsystem_vendor_id;
			identity.subsystemId = device->pciInfo.u.h0.subsystem_id;
			identity.revision = device->pciInfo.revision;
			identity.bus = device->pciInfo.bus;
			identity.device = device->pciInfo.device;
			identity.function = device->pciInfo.function;
			return user_memcpy(buffer, &identity, sizeof(identity));
		}

		case valleyview::kGetFirmwareSnapshot:
			if (buffer == NULL
				|| length < sizeof(valleyview::FirmwareSnapshot)) {
				return B_BAD_VALUE;
			}
			return user_memcpy(buffer, &device->snapshot,
				sizeof(device->snapshot));

		case valleyview::kGetSharedInfo:
			if (device->sharedArea < B_OK || buffer == NULL
				|| length < sizeof(area_id)) {
				return B_NO_INIT;
			}
			return user_memcpy(buffer, &device->sharedArea, sizeof(area_id));

		case valleyview::kCloneFramebuffer:
		{
			if (device->framebufferArea < B_OK || buffer == NULL
				|| length < sizeof(area_info)) {
				return B_NO_INIT;
			}

			void* address = NULL;
			area_id area = vm_clone_area(B_CURRENT_TEAM,
				"intel_valleyview cloned framebuffer", &address,
				B_ANY_ADDRESS, B_READ_AREA | B_WRITE_AREA, 0,
				device->framebufferArea, true);
			if (area < B_OK)
				return area;

			status_t status = _user_get_area_info(area, (area_info*)buffer);
			if (status != B_OK)
				delete_area(area);
			return status;
		}

		case valleyview::kPublishGraphics:
			return PublishValleyViewGraphics(*device);

		default:
			return B_DEV_INVALID_IOCTL;
	}
}


status_t
Read(void*, off_t, void*, size_t* length)
{
	if (length != NULL)
		*length = 0;
	return B_NOT_ALLOWED;
}


status_t
Write(void*, off_t, const void*, size_t* length)
{
	if (length != NULL)
		*length = 0;
	return B_NOT_ALLOWED;
}

} // namespace


device_module_info gValleyViewDeviceModule = {
	{
		kValleyViewDeviceModuleName,
		0,
		NULL
	},

	InitDevice,
	UninitDevice,
	NULL,

	Open,
	Close,
	Free,
	Read,
	Write,
	NULL,
	Control,

	NULL,
	NULL
};
