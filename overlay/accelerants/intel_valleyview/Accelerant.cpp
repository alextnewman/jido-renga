// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot

#include <common/intel_valleyview/DisplaySharedInfo.h>
#include <common/intel_valleyview/Protocol.h>

#include <Accelerant.h>
#include <compute_display_timing.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>


namespace {

struct AccelerantInfo {
	int								device;
	bool							isClone;
	area_id							sharedArea;
	valleyview::DisplaySharedInfo*	shared;
	area_id							modeListArea;
	display_mode*					modeList;
	area_id							framebufferArea;
	void*							framebuffer;
};
AccelerantInfo* gInfo;


bool
NativeActive()
{
	return gInfo != NULL && gInfo->shared != NULL
		&& gInfo->shared->nativeActive != 0;
}


bool
SameMode(const display_mode& left, const display_mode& right)
{
	return left.space == right.space
		&& left.virtual_width == right.virtual_width
		&& left.virtual_height == right.virtual_height
		&& left.h_display_start == right.h_display_start
		&& left.v_display_start == right.v_display_start;
}


void
UninitCommon()
{
	if (gInfo == NULL)
		return;

	if (gInfo->framebufferArea >= B_OK)
		delete_area(gInfo->framebufferArea);
	if (gInfo->modeListArea >= B_OK)
		delete_area(gInfo->modeListArea);
	if (gInfo->sharedArea >= B_OK)
		delete_area(gInfo->sharedArea);
	if (gInfo->isClone)
		close(gInfo->device);
	free(gInfo);
	gInfo = NULL;
}


status_t
InitCommon(int device, bool isClone)
{
	AccelerantInfo* info = (AccelerantInfo*)calloc(1, sizeof(AccelerantInfo));
	if (info == NULL)
		return B_NO_MEMORY;

	info->device = device;
	info->isClone = isClone;
	info->sharedArea = -1;
	info->modeListArea = -1;
	info->framebufferArea = -1;

	area_id sharedArea;
	status_t status = ioctl(device, valleyview::kGetSharedInfo, &sharedArea,
		sizeof(sharedArea));
	if (status != B_OK) {
		free(info);
		return status;
	}

	info->sharedArea = clone_area("intel_valleyview shared info",
		(void**)&info->shared, B_ANY_ADDRESS, B_READ_AREA | B_WRITE_AREA,
		sharedArea);
	if (info->sharedArea < B_OK) {
		status = info->sharedArea;
		free(info);
		return status;
	}
	if (!valleyview::IsValidAbiHeader(info->shared->header,
			sizeof(*info->shared))) {
		delete_area(info->sharedArea);
		free(info);
		return B_BAD_DATA;
	}

	gInfo = info;
	return B_OK;
}


status_t
CloneFramebuffer()
{
	area_info info;
	status_t status = ioctl(gInfo->device, valleyview::kCloneFramebuffer,
		&info, sizeof(info));
	if (status != B_OK)
		return status;

	gInfo->framebufferArea = info.area;
	gInfo->framebuffer = info.address;
	return B_OK;
}


status_t
CreateModeList()
{
	display_mode mode = gInfo->shared->currentMode;
	status_t status = compute_display_timing(mode.virtual_width,
		mode.virtual_height, 60, false, &mode.timing);
	if (status != B_OK)
		return status;

	void* address = NULL;
	gInfo->modeListArea = create_area("intel_valleyview modes", &address,
		B_ANY_ADDRESS, B_PAGE_SIZE, B_NO_LOCK,
		B_READ_AREA | B_WRITE_AREA | B_CLONEABLE_AREA);
	if (gInfo->modeListArea < B_OK)
		return gInfo->modeListArea;

	gInfo->modeList = (display_mode*)address;
	gInfo->modeList[0] = mode;
	gInfo->shared->currentMode = mode;
	gInfo->shared->modeListArea = gInfo->modeListArea;
	gInfo->shared->modeCount = 1;
	return B_OK;
}


status_t
InitAccelerant(int device)
{
	status_t status = InitCommon(device, false);
	if (status != B_OK)
		return status;

	status = CreateModeList();
	if (status == B_OK)
		status = CloneFramebuffer();
	if (status != B_OK)
		UninitCommon();
	return status;
}


ssize_t
CloneInfoSize()
{
	return B_PATH_NAME_LENGTH;
}


void
GetCloneInfo(void* data)
{
	if (data == NULL)
		return;

	if (gInfo == NULL || ioctl(gInfo->device, valleyview::kGetDeviceName, data,
			B_PATH_NAME_LENGTH) != B_OK) {
		((char*)data)[0] = '\0';
	}
}


status_t
CloneAccelerant(void* data)
{
	if (data == NULL)
		return B_BAD_VALUE;

	char path[B_PATH_NAME_LENGTH + 6];
	const int length = snprintf(path, sizeof(path), "/dev/%s",
		(const char*)data);
	if (length < 0 || static_cast<size_t>(length) >= sizeof(path))
		return B_NAME_TOO_LONG;

	int device = open(path, B_READ_WRITE);
	if (device < 0)
		return errno;

	status_t status = InitCommon(device, true);
	if (status != B_OK) {
		close(device);
		return status;
	}

	gInfo->modeListArea = clone_area("intel_valleyview cloned modes",
		(void**)&gInfo->modeList, B_ANY_ADDRESS, B_READ_AREA,
		gInfo->shared->modeListArea);
	if (gInfo->modeListArea < B_OK)
		status = gInfo->modeListArea;
	else
		status = CloneFramebuffer();
	if (status != B_OK)
		UninitCommon();
	return status;
}


void
UninitAccelerant()
{
	UninitCommon();
}


status_t
GetDeviceInfo(accelerant_device_info* info)
{
	if (info == NULL)
		return B_BAD_VALUE;

	memset(info, 0, sizeof(*info));
	info->version = B_ACCELERANT_VERSION;
	strlcpy(info->name, "Intel ValleyView", sizeof(info->name));
	strlcpy(info->chipset, "ValleyView 0f31", sizeof(info->chipset));
	strlcpy(info->serial_no, "None", sizeof(info->serial_no));
	if (gInfo != NULL)
		info->memory = static_cast<uint32>(gInfo->shared->framebufferSize);
	return B_OK;
}


sem_id
RetraceSemaphore()
{
	return -1;
}


uint32
ModeCount()
{
	return gInfo != NULL ? gInfo->shared->modeCount : 0;
}


status_t
GetModeList(display_mode* modes)
{
	if (gInfo == NULL || modes == NULL)
		return B_BAD_VALUE;

	memcpy(modes, gInfo->modeList,
		gInfo->shared->modeCount * sizeof(display_mode));
	return B_OK;
}


status_t
SetMode(display_mode* mode)
{
	if (gInfo == NULL || mode == NULL)
		return B_BAD_VALUE;
	return SameMode(*mode, gInfo->shared->currentMode)
		? B_OK : B_NOT_SUPPORTED;
}


status_t
GetMode(display_mode* mode)
{
	if (gInfo == NULL || mode == NULL)
		return B_BAD_VALUE;
	*mode = gInfo->shared->currentMode;
	return B_OK;
}


status_t
GetPreferredMode(display_mode* mode)
{
	return GetMode(mode);
}


status_t
GetFramebuffer(frame_buffer_config* config)
{
	if (gInfo == NULL || config == NULL)
		return B_BAD_VALUE;

	config->frame_buffer = gInfo->framebuffer;
	config->frame_buffer_dma = NULL;
	config->bytes_per_row = gInfo->shared->bytesPerRow;
	return B_OK;
}


status_t
GetPixelClockLimits(display_mode* mode, uint32* low, uint32* high)
{
	if (mode == NULL || low == NULL || high == NULL)
		return B_BAD_VALUE;

	const uint32 total = static_cast<uint32>(mode->timing.h_total)
		* mode->timing.v_total;
	*low = total * 48L / 1000L;
	*high = 2000000;
	return *low <= *high ? B_OK : B_BAD_VALUE;
}


status_t
SetBrightness(float brightness)
{
	if (gInfo == NULL)
		return B_NO_INIT;
	valleyview::BrightnessRequest request = {};
	request.header = valleyview::MakeAbiHeader(sizeof(request));
	request.value = brightness;
	return ioctl(gInfo->device, valleyview::kSetBrightness, &request,
		sizeof(request));
}


status_t
GetBrightness(float* brightness)
{
	if (gInfo == NULL || brightness == NULL)
		return B_BAD_VALUE;
	valleyview::BrightnessRequest request = {};
	status_t status = ioctl(gInfo->device, valleyview::kGetBrightness,
		&request, sizeof(request));
	if (status == B_OK
		&& !valleyview::IsValidAbiHeader(request.header, sizeof(request))) {
		status = B_BAD_DATA;
	}
	if (status == B_OK)
		*brightness = request.value;
	return status;
}


uint32
DpmsCapabilities()
{
	return B_DPMS_ON | B_DPMS_STAND_BY | B_DPMS_SUSPEND | B_DPMS_OFF;
}


uint32
DpmsMode()
{
	if (gInfo == NULL)
		return B_DPMS_ON;
	valleyview::DpmsRequest request = {};
	if (ioctl(gInfo->device, valleyview::kGetDpms, &request,
			sizeof(request)) != B_OK) {
		return B_DPMS_ON;
	}
	return request.mode;
}


status_t
SetDpmsMode(uint32 mode)
{
	if (gInfo == NULL)
		return B_NO_INIT;
	valleyview::DpmsRequest request = {};
	request.header = valleyview::MakeAbiHeader(sizeof(request));
	request.mode = mode;
	return ioctl(gInfo->device, valleyview::kSetDpms, &request,
		sizeof(request));
}


status_t
SetCursorShape(uint16 width, uint16 height, uint16 hotX, uint16 hotY,
	const uint8* andMask, const uint8* xorMask)
{
	if (gInfo == NULL || andMask == NULL || xorMask == NULL
		|| width == 0 || height == 0 || width > valleyview::kCursorMaxWidth
		|| height > valleyview::kCursorMaxHeight) {
		return B_BAD_VALUE;
	}
	valleyview::CursorShapeRequest request = {};
	request.header = valleyview::MakeAbiHeader(sizeof(request));
	request.width = width;
	request.height = height;
	request.hotX = hotX;
	request.hotY = hotY;
	const size_t maskBytes = ((width + 7) / 8) * height;
	memcpy(request.andMask, andMask, maskBytes);
	memcpy(request.xorMask, xorMask, maskBytes);
	return ioctl(gInfo->device, valleyview::kSetCursorShape, &request,
		sizeof(request));
}


status_t
SetCursorBitmap(uint16 width, uint16 height, uint16 hotX, uint16 hotY,
	color_space colorSpace, uint16 bytesPerRow, const uint8* bitmapData)
{
	if (gInfo == NULL || bitmapData == NULL || width == 0 || height == 0
		|| width > valleyview::kCursorMaxWidth
		|| height > valleyview::kCursorMaxHeight || hotX >= width
		|| hotY >= height || bytesPerRow < width * sizeof(uint32)
		|| (colorSpace != B_RGBA32 && colorSpace != B_RGB32)) {
		return B_BAD_VALUE;
	}

	valleyview::CursorBitmapRequest* request
		= static_cast<valleyview::CursorBitmapRequest*>(
			calloc(1, sizeof(valleyview::CursorBitmapRequest)));
	if (request == NULL)
		return B_NO_MEMORY;
	request->header = valleyview::MakeAbiHeader(sizeof(*request));
	request->width = width;
	request->height = height;
	request->hotX = hotX;
	request->hotY = hotY;
	for (uint32 y = 0; y < height; y++) {
		const uint8* source = bitmapData + y * bytesPerRow;
		for (uint32 x = 0; x < width; x++) {
			uint32 pixel;
			memcpy(&pixel, source + x * sizeof(uint32), sizeof(pixel));
			if (colorSpace == B_RGB32)
				pixel |= 0xff000000;
			request->pixels[y * valleyview::kCursorMaxWidth + x] = pixel;
		}
	}
	status_t status = ioctl(gInfo->device, valleyview::kSetCursorBitmap,
		request, sizeof(*request));
	free(request);
	return status;
}


void
MoveCursor(uint16 x, uint16 y)
{
	if (gInfo == NULL)
		return;
	valleyview::CursorMoveRequest request = {};
	request.header = valleyview::MakeAbiHeader(sizeof(request));
	request.x = static_cast<int16>(x);
	request.y = static_cast<int16>(y);
	ioctl(gInfo->device, valleyview::kMoveCursor, &request, sizeof(request));
}


void
ShowCursor(bool visible)
{
	if (gInfo == NULL)
		return;
	valleyview::CursorShowRequest request = {};
	request.header = valleyview::MakeAbiHeader(sizeof(request));
	request.visible = visible ? 1 : 0;
	ioctl(gInfo->device, valleyview::kShowCursor, &request, sizeof(request));
}


} // namespace


extern "C" void*
get_accelerant_hook(uint32 feature, void*)
{
	switch (feature) {
		case B_INIT_ACCELERANT:
			return (void*)InitAccelerant;
		case B_UNINIT_ACCELERANT:
			return (void*)UninitAccelerant;
		case B_ACCELERANT_CLONE_INFO_SIZE:
			return (void*)CloneInfoSize;
		case B_GET_ACCELERANT_CLONE_INFO:
			return (void*)GetCloneInfo;
		case B_CLONE_ACCELERANT:
			return (void*)CloneAccelerant;
		case B_GET_ACCELERANT_DEVICE_INFO:
			return (void*)GetDeviceInfo;
		case B_ACCELERANT_RETRACE_SEMAPHORE:
			return (void*)RetraceSemaphore;
		case B_ACCELERANT_MODE_COUNT:
			return (void*)ModeCount;
		case B_GET_MODE_LIST:
			return (void*)GetModeList;
		case B_GET_PREFERRED_DISPLAY_MODE:
			return (void*)GetPreferredMode;
		case B_SET_DISPLAY_MODE:
			return (void*)SetMode;
		case B_GET_DISPLAY_MODE:
			return (void*)GetMode;
		case B_GET_FRAME_BUFFER_CONFIG:
			return (void*)GetFramebuffer;
		case B_GET_PIXEL_CLOCK_LIMITS:
			return (void*)GetPixelClockLimits;
		case B_SET_BRIGHTNESS:
			return NativeActive() ? (void*)SetBrightness : NULL;
		case B_GET_BRIGHTNESS:
			return NativeActive() ? (void*)GetBrightness : NULL;
		case B_DPMS_CAPABILITIES:
			return NativeActive() ? (void*)DpmsCapabilities : NULL;
		case B_DPMS_MODE:
			return NativeActive() ? (void*)DpmsMode : NULL;
		case B_SET_DPMS_MODE:
			return NativeActive() ? (void*)SetDpmsMode : NULL;
		case B_SET_CURSOR_SHAPE:
			return NativeActive() ? (void*)SetCursorShape : NULL;
		case B_SET_CURSOR_BITMAP:
			return NativeActive() ? (void*)SetCursorBitmap : NULL;
		case B_MOVE_CURSOR:
			return NativeActive() ? (void*)MoveCursor : NULL;
		case B_SHOW_CURSOR:
			return NativeActive() ? (void*)ShowCursor : NULL;
		default:
			return NULL;
	}
}
