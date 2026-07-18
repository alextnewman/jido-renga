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
			status.capabilities = valleyview::kCapabilityGpuDiagnostics
				| valleyview::kCapabilityBcsSelfTest;
			if (device->nativeActive) {
				status.capabilities |= valleyview::kCapabilityFirmwareAdoption
					| valleyview::kCapabilityModeset
					| valleyview::kCapabilityBacklight
					| valleyview::kCapabilityCursor
					| valleyview::kCapabilityDpms;
				status.displayState = device->softBlanked
					? valleyview::kSoftBlankedNative : valleyview::kActive;
			} else if (device->snapshot.adoptionStatus == B_OK) {
				status.capabilities
					|= valleyview::kCapabilityFirmwareAdoption;
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
			if (device->p0MemoryQuarantined || device->framebufferArea < B_OK
				|| buffer == NULL
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

		case valleyview::kGetGpuDiagnostics:
		{
			if (buffer == NULL
				|| length < sizeof(valleyview::GpuDiagnostics)) {
				return B_BAD_VALUE;
			}

			valleyview::GpuDiagnostics diagnostics = {};
			status_t status = CaptureGpuDiagnostics(*device, diagnostics);
			status_t copyStatus = user_memcpy(buffer, &diagnostics,
				sizeof(diagnostics));
			return copyStatus == B_OK ? status : copyStatus;
		}

		case valleyview::kRunGpuSelfTest:
		{
			if (buffer == NULL
				|| length < sizeof(valleyview::GpuDiagnostics)) {
				return B_BAD_VALUE;
			}

			valleyview::GpuDiagnostics diagnostics;
			status_t status = user_memcpy(&diagnostics, buffer,
				sizeof(diagnostics));
			if (status != B_OK)
				return status;
			if (!valleyview::IsValidAbiHeader(diagnostics.header,
					sizeof(diagnostics))
				|| diagnostics.command != valleyview::kGpuSelfTestArm) {
				return B_BAD_VALUE;
			}

			status = RunGpuSelfTest(*device, diagnostics);
			status_t copyStatus = user_memcpy(buffer, &diagnostics,
				sizeof(diagnostics));
			return copyStatus == B_OK ? status : copyStatus;
		}

		case valleyview::kGetP0Status:
		{
			if (buffer == NULL || length < sizeof(valleyview::P0Status))
				return B_BAD_VALUE;
			valleyview::P0Status status;
			mutex_lock(&device->lock);
			GetP0Status(*device, status);
			mutex_unlock(&device->lock);
			return user_memcpy(buffer, &status, sizeof(status));
		}

		case valleyview::kGetBrightness:
		{
			if (buffer == NULL
				|| length < sizeof(valleyview::BrightnessRequest)) {
				return B_BAD_VALUE;
			}
			valleyview::BrightnessRequest request = {};
			status_t status = GetBrightness(*device, request);
			if (status != B_OK)
				return status;
			return user_memcpy(buffer, &request, sizeof(request));
		}

		case valleyview::kSetBrightness:
		{
			if (buffer == NULL
				|| length < sizeof(valleyview::BrightnessRequest)) {
				return B_BAD_VALUE;
			}
			valleyview::BrightnessRequest request;
			status_t status = user_memcpy(&request, buffer,
				sizeof(request));
			if (status != B_OK)
				return status;
			if (!valleyview::IsValidAbiHeader(request.header,
					sizeof(request))) {
				return B_BAD_VALUE;
			}
			return SetBrightness(*device, request);
		}

		case valleyview::kGetDpms:
		{
			if (buffer == NULL
				|| length < sizeof(valleyview::DpmsRequest)) {
				return B_BAD_VALUE;
			}
			valleyview::DpmsRequest request = {};
			status_t status = GetDpms(*device, request);
			if (status != B_OK)
				return status;
			return user_memcpy(buffer, &request, sizeof(request));
		}

		case valleyview::kSetDpms:
		{
			if (buffer == NULL
				|| length < sizeof(valleyview::DpmsRequest)) {
				return B_BAD_VALUE;
			}
			valleyview::DpmsRequest request;
			status_t status = user_memcpy(&request, buffer,
				sizeof(request));
			if (status != B_OK)
				return status;
			if (!valleyview::IsValidAbiHeader(request.header,
					sizeof(request))) {
				return B_BAD_VALUE;
			}
			return SetDpms(*device, request);
		}

		case valleyview::kSetCursorShape:
		{
			if (buffer == NULL
				|| length < sizeof(valleyview::CursorShapeRequest)) {
				return B_BAD_VALUE;
			}
			valleyview::CursorShapeRequest request;
			status_t status = user_memcpy(&request, buffer,
				sizeof(request));
			if (status != B_OK)
				return status;
			if (!valleyview::IsValidAbiHeader(request.header,
					sizeof(request))) {
				return B_BAD_VALUE;
			}
			return SetCursorShape(*device, request);
		}

		case valleyview::kMoveCursor:
		{
			if (buffer == NULL
				|| length < sizeof(valleyview::CursorMoveRequest)) {
				return B_BAD_VALUE;
			}
			valleyview::CursorMoveRequest request;
			status_t status = user_memcpy(&request, buffer,
				sizeof(request));
			if (status != B_OK)
				return status;
			if (!valleyview::IsValidAbiHeader(request.header,
					sizeof(request))) {
				return B_BAD_VALUE;
			}
			return MoveCursor(*device, request);
		}

		case valleyview::kShowCursor:
		{
			if (buffer == NULL
				|| length < sizeof(valleyview::CursorShowRequest)) {
				return B_BAD_VALUE;
			}
			valleyview::CursorShowRequest request;
			status_t status = user_memcpy(&request, buffer,
				sizeof(request));
			if (status != B_OK)
				return status;
			if (!valleyview::IsValidAbiHeader(request.header,
					sizeof(request))) {
				return B_BAD_VALUE;
			}
			return ShowCursor(*device, request);
		}

		case valleyview::kBcsFill:
		{
			if (buffer == NULL
				|| length < sizeof(valleyview::BcsFillRequest)) {
				return B_BAD_VALUE;
			}
			valleyview::BcsFillRequest request;
			status_t status = user_memcpy(&request, buffer,
				sizeof(request));
			if (status != B_OK)
				return status;
			if (!valleyview::IsValidAbiHeader(request.header,
					sizeof(request))) {
				return B_BAD_VALUE;
			}
			return SubmitBcsFill(*device, request);
		}

		case valleyview::kBcsBlit:
		{
			if (buffer == NULL
				|| length < sizeof(valleyview::BcsBlitRequest)) {
				return B_BAD_VALUE;
			}
			valleyview::BcsBlitRequest request;
			status_t status = user_memcpy(&request, buffer,
				sizeof(request));
			if (status != B_OK)
				return status;
			if (!valleyview::IsValidAbiHeader(request.header,
					sizeof(request))) {
				return B_BAD_VALUE;
			}
			return SubmitBcsBlit(*device, request);
		}

		case valleyview::kRunP0SelfTest:
		{
			if (buffer == NULL
				|| length < sizeof(valleyview::P0SelfTest)) {
				return B_BAD_VALUE;
			}
			valleyview::P0SelfTest test;
			status_t status = user_memcpy(&test, buffer, sizeof(test));
			if (status != B_OK)
				return status;
			if (!valleyview::IsValidAbiHeader(test.header, sizeof(test))
				|| test.command != valleyview::kP0SelfTestArm) {
				return B_BAD_VALUE;
			}

			mutex_lock(&device->lock);
			GetP0Status(*device, test.before);
			status = InitializeBcsRuntime(*device);
			GetP0Status(*device, test.after);
			mutex_unlock(&device->lock);
			test.status = status;
			test.flags = test.after.flags;
			status_t copyStatus = user_memcpy(buffer, &test, sizeof(test));
			return copyStatus == B_OK ? status : copyStatus;
		}

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
