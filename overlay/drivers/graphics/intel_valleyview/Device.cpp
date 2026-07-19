// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot

#include "Driver.h"

#include <common/intel_valleyview/Protocol.h>
#include <common/intel_valleyview/RenderMemoryCore.h>
#include <common/intel_valleyview/RenderProtocol.h>

#include <graphic_driver.h>

#include <stdio.h>
#include <stdlib.h>
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
	ValleyViewClient* client = static_cast<ValleyViewClient*>(
		calloc(1, sizeof(ValleyViewClient)));
	if (client == NULL)
		return B_NO_MEMORY;
	client->device = device;

	mutex_lock(&device->lock);
	device->openCount++;
	mutex_unlock(&device->lock);

	*cookie = client;
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
	ValleyViewClient* client = static_cast<ValleyViewClient*>(cookie);
	if (client == NULL || client->device == NULL)
		return B_BAD_VALUE;
	ValleyViewDevice* device = client->device;

	DestroyRenderClient(*client);
	free(client);

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
	ValleyViewClient* client = static_cast<ValleyViewClient*>(cookie);
	if (client == NULL || client->device == NULL)
		return B_BAD_VALUE;
	ValleyViewDevice* device = client->device;

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
					| valleyview::kCapabilityDpms
					| valleyview::kCapabilityHardwarePresent;
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

		case valleyview::kSetCursorBitmap:
		{
			if (buffer == NULL
				|| length < sizeof(valleyview::CursorBitmapRequest)) {
				return B_BAD_VALUE;
			}
			valleyview::CursorBitmapRequest* request
				= static_cast<valleyview::CursorBitmapRequest*>(
					malloc(sizeof(valleyview::CursorBitmapRequest)));
			if (request == NULL)
				return B_NO_MEMORY;
			status_t status = user_memcpy(request, buffer, sizeof(*request));
			if (status == B_OK
				&& !valleyview::IsValidAbiHeader(request->header,
					sizeof(*request))) {
				status = B_BAD_VALUE;
			}
			if (status == B_OK)
				status = SetCursorBitmap(*device, *request);
			free(request);
			return status;
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

		case valleyview::kGetRenderDeviceInfo:
		{
			if (buffer == NULL
				|| length < sizeof(valleyview::RenderDeviceInfo)) {
				return B_BAD_VALUE;
			}

			valleyview::RenderDeviceInfo info = {};
			info.header = valleyview::MakeRenderAbiHeader(sizeof(info));
			info.status = B_NOT_SUPPORTED;
			info.vendorId = device->pciInfo.vendor_id;
			info.deviceId = device->pciInfo.device_id;
			info.revision = device->pciInfo.revision;
			info.graphicsGeneration = 7;
			info.gpuAddressBits = 32;
			info.pageSize = valleyview::kPageSize;
			info.capabilities = valleyview::kRenderCapabilityDeviceInfo;

			mutex_lock(&device->lock);
			mutex_lock(&device->renderLock);
			info.apertureBase = device->snapshot.gmadrBase;
			info.apertureSize = device->snapshot.gmadrSize;
			if (info.apertureSize != 0)
				info.deviceFlags |= valleyview::kRenderDeviceGgtt;
			info.deviceFlags |= valleyview::kRenderDeviceNoLlc;
			if (device->nativeActive && !device->gpuFaulted
				&& !device->p0MemoryQuarantined
				&& !device->renderMemoryQuarantined) {
				info.capabilities
					|= valleyview::kRenderCapabilityBufferObjects
						| valleyview::kRenderCapabilityCpuMappings
						| valleyview::kRenderCapabilityGpuAddressSpaces
						| valleyview::kRenderCapabilityCacheDomains;
			}
			mutex_lock(&device->bcsLock);
			if (device->bcsReady)
				info.provenEngines |= valleyview::kRenderEngineBcs;
			mutex_unlock(&device->bcsLock);
			if (device->nativeActive) {
				info.deviceFlags |= valleyview::kRenderDeviceDisplayReserved;
				info.displayReservedOffset = device->p0Layout.base;
				info.displayReservedSize = valleyview::kP0AllocationBytes;
			}
			mutex_unlock(&device->renderLock);
			mutex_unlock(&device->lock);

			return user_memcpy(buffer, &info, sizeof(info));
		}

		case valleyview::kRenderCreateBuffer:
		{
			if (buffer == NULL
				|| length < sizeof(valleyview::RenderBufferCreate)) {
				return B_BAD_VALUE;
			}
			valleyview::RenderBufferCreate request;
			status_t status = user_memcpy(&request, buffer, sizeof(request));
			if (status != B_OK)
				return status;
			if (!valleyview::IsValidRenderAbiHeader(request.header,
					sizeof(request))) {
				return B_BAD_VALUE;
			}
			status = CreateRenderBuffer(*client, request);
			if (status != B_OK)
				return status;
			status = user_memcpy(buffer, &request, sizeof(request));
			if (status != B_OK)
				CloseRenderBuffer(*client, request.handle);
			return status;
		}

		case valleyview::kRenderMapBuffer:
		{
			if (buffer == NULL
				|| length < sizeof(valleyview::RenderBufferMap)) {
				return B_BAD_VALUE;
			}
			valleyview::RenderBufferMap request;
			status_t status = user_memcpy(&request, buffer, sizeof(request));
			if (status != B_OK)
				return status;
			if (!valleyview::IsValidRenderAbiHeader(request.header,
					sizeof(request))) {
				return B_BAD_VALUE;
			}
			status = MapRenderBuffer(*client, request);
			if (status != B_OK)
				return status;
			status = user_memcpy(buffer, &request, sizeof(request));
			if (status != B_OK) {
				status_t cleanupStatus = DiscardRenderBufferMapping(*client,
					request.handle, request.area);
				if (cleanupStatus != B_OK) {
					dprintf("intel_valleyview: render mapping cleanup failed: %"
						B_PRId32 "\n", cleanupStatus);
				}
			}
			return status;
		}

		case valleyview::kRenderCloseBuffer:
		{
			if (buffer == NULL
				|| length < sizeof(valleyview::RenderBufferClose)) {
				return B_BAD_VALUE;
			}
			valleyview::RenderBufferClose request;
			status_t status = user_memcpy(&request, buffer, sizeof(request));
			if (status != B_OK)
				return status;
			if (!valleyview::IsValidRenderAbiHeader(request.header,
					sizeof(request))) {
				return B_BAD_VALUE;
			}
			return CloseRenderBuffer(*client, request.handle);
		}

		case valleyview::kRenderSetBufferDomain:
		{
			if (buffer == NULL
				|| length < sizeof(valleyview::RenderBufferSetDomain)) {
				return B_BAD_VALUE;
			}
			valleyview::RenderBufferSetDomain request;
			status_t status = user_memcpy(&request, buffer, sizeof(request));
			if (status != B_OK)
				return status;
			if (!valleyview::IsValidRenderAbiHeader(request.header,
					sizeof(request))) {
				return B_BAD_VALUE;
			}
			status = SetRenderBufferDomain(*client, request);
			if (status != B_OK)
				return status;
			return user_memcpy(buffer, &request, sizeof(request));
		}

		case valleyview::kRunRenderMemoryTest:
		{
			if (buffer == NULL
				|| length < sizeof(valleyview::RenderMemoryTest)) {
				return B_BAD_VALUE;
			}
			valleyview::RenderMemoryTest test;
			status_t status = user_memcpy(&test, buffer, sizeof(test));
			if (status != B_OK)
				return status;
			if (!valleyview::IsValidRenderAbiHeader(test.header,
					sizeof(test))) {
				return B_BAD_VALUE;
			}
			status = RunRenderMemoryTest(*client, test);
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
