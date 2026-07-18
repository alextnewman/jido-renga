// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot

#include "Driver.h"

#include <common/intel_valleyview/FirmwareState.h>
#include <common/intel_valleyview/GpuCore.h>
#include <common/intel_valleyview/P0Core.h>

#include <KernelExport.h>

#include <stdlib.h>
#include <string.h>
#include <vm/vm.h>


namespace {

constexpr bigtime_t kPlaneTransitionDelayUs = 20000;
constexpr bigtime_t kPlaneLatchTimeoutUs = 100000;
constexpr bigtime_t kPresentLatchWarningUs = 25000;
constexpr bigtime_t kPresentPollUs = 100;


uint32
ReadMmio(const ValleyViewDevice& device, uint32 offset)
{
	return *(const volatile uint32*)(device.registers + offset);
}


void
WriteMmio(ValleyViewDevice& device, uint32 offset, uint32 value)
{
	*(volatile uint32*)(device.registers + offset) = value;
}


void
FlushGgtt(ValleyViewDevice& device)
{
	memory_write_barrier();
	WriteMmio(device, valleyview::kGfxFlushControl,
		valleyview::kGfxFlushEnable);
	ReadMmio(device, valleyview::kGfxFlushControl);
}


uint32
PteOffset(uint32 ggttOffset, uint32 page)
{
	return valleyview::kGttOffsetInBar
		+ (ggttOffset / valleyview::kPageSize + page)
			* valleyview::kGen7PteSize;
}


status_t
ValidateLiveWinkyState(const ValleyViewDevice& device)
{
	if (device.registers == NULL)
		return B_NO_INIT;
	if (!valleyview::IsWinkyP0TakeoverState(device.snapshot))
		return B_BAD_DATA;

	const uint32 expectedRegisters[][2] = {
		{valleyview::kDpllA, device.snapshot.dpllA},
		{valleyview::kHTotalA, device.snapshot.hTotal},
		{valleyview::kHBlankA, device.snapshot.hBlank},
		{valleyview::kHSyncA, device.snapshot.hSync},
		{valleyview::kVTotalA, device.snapshot.vTotal},
		{valleyview::kVBlankA, device.snapshot.vBlank},
		{valleyview::kVSyncA, device.snapshot.vSync},
		{valleyview::kPipeSourceA, device.snapshot.pipeSource},
		{valleyview::kPipeConfigA, device.snapshot.pipeConfig},
		{valleyview::kPlaneAddressVlvA, device.snapshot.planeAddressVlv},
		{valleyview::kPlaneControlA, device.snapshot.planeControl},
		{valleyview::kPlaneLinearOffsetA, device.snapshot.planeLinearOffset},
		{valleyview::kPlaneStrideA, device.snapshot.planeStride},
		{valleyview::kPlaneSurfaceA, device.snapshot.planeSurface},
		{valleyview::kPlaneSurfaceLiveA, device.snapshot.planeSurfaceLive},
		{valleyview::kPlaneTileOffsetA, device.snapshot.planeTileOffset},
		{valleyview::kPanelFitterControl,
			device.snapshot.panelFitterControl},
		{valleyview::kPanelFitterProgrammedRatios,
			device.snapshot.panelFitterProgrammedRatios},
		{valleyview::kPanelFitterAutoRatios,
			device.snapshot.panelFitterAutoRatios},
		{valleyview::kDpC, device.snapshot.dpC},
		{valleyview::kPpsControlA, device.snapshot.ppsControl},
		{valleyview::kPpsOnDelaysA, device.snapshot.ppsOnDelays},
		{valleyview::kPpsOffDelaysA, device.snapshot.ppsOffDelays},
		{valleyview::kPpsDivisorA, device.snapshot.ppsDivisor},
		{valleyview::kPwmControl2A, device.snapshot.pwmControl2},
		{valleyview::kCursorControlA, device.snapshot.cursorControl},
		{valleyview::kCursorBaseA, device.snapshot.cursorBase},
		{valleyview::kCursorPositionA, device.snapshot.cursorPosition},
		{valleyview::kCursorSurfaceLiveA,
			device.snapshot.cursorSurfaceLive}
	};
	for (size_t index = 0;
			index < sizeof(expectedRegisters) / sizeof(expectedRegisters[0]);
			index++) {
		if (ReadMmio(device, expectedRegisters[index][0])
				!= expectedRegisters[index][1]) {
			return B_BUSY;
		}
	}

	const uint32 ppsStatus = ReadMmio(device, valleyview::kPpsStatusA);
	if ((ppsStatus & (valleyview::kPpsOn | valleyview::kPpsReady))
			!= (valleyview::kPpsOn | valleyview::kPpsReady)) {
		return B_BUSY;
	}
	if (valleyview::PwmPeriod(ReadMmio(device, valleyview::kPwmControlA))
			!= device.snapshot.pwmPeriod) {
		return B_BUSY;
	}
	if (device.snapshot.gttRequiredPages == 0
		|| device.snapshot.gttSignature == 0) {
		return B_BAD_DATA;
	}
	const uint64 scanoutOffset = static_cast<uint64>(
		device.snapshot.planeGgttOffset) + device.snapshot.planeLinearOffset;
	const uint64 firstPte = valleyview::kGttOffsetInBar
		+ scanoutOffset / valleyview::kPageSize
			* valleyview::kGen7PteSize;
	const uint64 pteBytes = static_cast<uint64>(
		device.snapshot.gttRequiredPages) * valleyview::kGen7PteSize;
	if (!valleyview::RangeFits(firstPte, pteBytes,
			device.snapshot.mmioSize)) {
		return B_BAD_DATA;
	}
	uint64 signature = valleyview::kGgttSignatureSeed;
	for (uint32 page = 0; page < device.snapshot.gttRequiredPages; page++) {
		const uint32 pte = ReadMmio(device,
			static_cast<uint32>(firstPte
				+ page * valleyview::kGen7PteSize));
		if ((pte & valleyview::kGen7PtePresent) == 0)
			return B_BUSY;
		signature = valleyview::AppendGgttPteSignature(signature, pte);
	}
	if (signature != device.snapshot.gttSignature)
		return B_BUSY;
	return B_OK;
}


status_t
AllocateP0Area(const char* name, size_t size, bool cloneable,
	uint32 memoryType, area_id& area, void*& address, phys_addr_t& physical)
{
	virtual_address_restrictions virtualRestrictions = {};
	virtualRestrictions.address_specification = B_ANY_KERNEL_ADDRESS;
	physical_address_restrictions physicalRestrictions = {};
	physicalRestrictions.high_address = 0x100000000ull;
	physicalRestrictions.alignment = valleyview::kPageSize;

	address = NULL;
	uint32 protection = B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA;
	if (cloneable)
		protection |= B_CLONEABLE_AREA;
	area = create_area_etc(B_SYSTEM_TEAM, name, size, B_CONTIGUOUS,
		protection, 0, 0, &virtualRestrictions, &physicalRestrictions,
		&address);
	if (area < B_OK)
		return area;

	physical_entry entry;
	if (address == NULL || get_memory_map(address, size, &entry, 1) != B_OK
		|| entry.size < size || entry.address + size > 0x100000000ull
		|| (entry.address & valleyview::kPageMask) != 0) {
		delete_area(area);
		area = -1;
		address = NULL;
		return B_BAD_DATA;
	}
	status_t status = vm_set_area_memory_type(area, entry.address,
		memoryType);
	if (status != B_OK) {
		delete_area(area);
		area = -1;
		address = NULL;
		return status;
	}
	physical = entry.address;
	return B_OK;
}


void
DeleteP0Areas(ValleyViewDevice& device)
{
	if (device.p0PrivateArea >= B_OK)
		delete_area(device.p0PrivateArea);
	for (uint32 index = 0; index < 2; index++) {
		if (device.scanoutArea[index] >= B_OK)
			delete_area(device.scanoutArea[index]);
		device.scanoutArea[index] = -1;
		device.scanout[index] = NULL;
		device.scanoutPhysical[index] = 0;
	}
	if (device.framebufferArea >= B_OK)
		delete_area(device.framebufferArea);
	device.p0PrivateArea = -1;
	device.p0Private = NULL;
	device.p0PrivatePhysical = 0;
	device.framebufferArea = -1;
	device.framebuffer = NULL;
	device.p0Physical = 0;
	device.p0Size = 0;
}


status_t
AllocateP0Memory(ValleyViewDevice& device)
{
	const uint64 liveOffset = static_cast<uint64>(
		device.snapshot.planeGgttOffset) + device.snapshot.planeLinearOffset;
	if (!valleyview::BuildP0Layout(device.snapshot.gmadrSize, liveOffset,
			device.snapshot.bootFramebufferSize, device.p0Layout)) {
		return B_BAD_DATA;
	}

	device.p0SavedPtes = static_cast<uint32*>(malloc(
		device.p0Layout.pageCount * sizeof(uint32)));
	if (device.p0SavedPtes == NULL)
		return B_NO_MEMORY;

	const uint32 firstPte = ReadMmio(device,
		PteOffset(device.p0Layout.base, 0));
	for (uint32 page = 0; page < device.p0Layout.pageCount; page++) {
		const uint32 pte = ReadMmio(device,
			PteOffset(device.p0Layout.base, page));
		device.p0SavedPtes[page] = pte;
		if (pte != firstPte) {
			free(device.p0SavedPtes);
			device.p0SavedPtes = NULL;
			return B_BUSY;
		}
	}

	status_t status = AllocateP0Area("intel_valleyview P0 framebuffer",
		valleyview::kP0FramebufferBytes, true, B_WRITE_BACK_MEMORY,
		device.framebufferArea, device.framebuffer, device.p0Physical);
	if (status != B_OK)
		return status;
	status = AllocateP0Area("intel_valleyview P0 scanout 0",
		valleyview::kP0FramebufferBytes, false, B_WRITE_COMBINING_MEMORY,
		device.scanoutArea[0], device.scanout[0],
		device.scanoutPhysical[0]);
	if (status != B_OK)
		return status;
	status = AllocateP0Area("intel_valleyview P0 scanout 1",
		valleyview::kP0FramebufferBytes, false, B_WRITE_COMBINING_MEMORY,
		device.scanoutArea[1], device.scanout[1],
		device.scanoutPhysical[1]);
	if (status != B_OK)
		return status;
	status = AllocateP0Area("intel_valleyview P0 private",
		valleyview::kP0PrivateBytes, false, B_WRITE_COMBINING_MEMORY,
		device.p0PrivateArea, device.p0Private, device.p0PrivatePhysical);
	if (status != B_OK)
		return status;

	device.p0Size = valleyview::kP0FramebufferBytes;
	memset(device.framebuffer, 0, device.p0Size);
	memset(device.scanout[0], 0, device.p0Size);
	memset(device.scanout[1], 0, device.p0Size);
	memset(device.p0Private, 0, valleyview::kP0PrivateBytes);
	memory_write_barrier();

	for (uint32 page = 0; page < device.p0Layout.pageCount; page++) {
		uint64 physical;
		uint32 pte;
		if (!valleyview::P0PagePhysical(device.p0Physical,
				device.scanoutPhysical[0], device.scanoutPhysical[1],
				device.p0PrivatePhysical, page, physical)
			|| !valleyview::EncodeBytPte(physical, true,
				valleyview::P0PageSnooped(page), pte)) {
			return B_BAD_DATA;
		}
		WriteMmio(device, PteOffset(device.p0Layout.base, page), pte);
	}
	FlushGgtt(device);

	for (uint32 page = 0; page < device.p0Layout.pageCount; page++) {
		uint64 physical;
		if (!valleyview::P0PagePhysical(device.p0Physical,
				device.scanoutPhysical[0], device.scanoutPhysical[1],
				device.p0PrivatePhysical, page, physical)) {
			return B_BAD_DATA;
		}
		const uint32 expected = static_cast<uint32>(physical)
			| valleyview::kGen7PtePresent
			| valleyview::kBytPteWriteable
			| (valleyview::P0PageSnooped(page)
				? valleyview::kBytPteSnooped : 0);
		if (ReadMmio(device, PteOffset(device.p0Layout.base, page))
				!= expected) {
			return B_IO_ERROR;
		}
	}
	return B_OK;
}


status_t
RestoreP0Ptes(ValleyViewDevice& device)
{
	if (device.p0SavedPtes == NULL)
		return B_OK;
	for (uint32 page = 0; page < device.p0Layout.pageCount; page++) {
		WriteMmio(device, PteOffset(device.p0Layout.base, page),
			device.p0SavedPtes[page]);
	}
	FlushGgtt(device);
	for (uint32 page = 0; page < device.p0Layout.pageCount; page++) {
		if (ReadMmio(device, PteOffset(device.p0Layout.base, page))
				!= device.p0SavedPtes[page]) {
			return B_IO_ERROR;
		}
	}
	return B_OK;
}


void
SaveFirmwarePlane(ValleyViewDevice& device)
{
	device.originalPipeSource
		= ReadMmio(device, valleyview::kPipeSourceA);
	device.originalPlaneControl
		= ReadMmio(device, valleyview::kPlaneControlA);
	device.originalPlaneAddress
		= ReadMmio(device, valleyview::kPlaneAddressVlvA);
	device.originalPlaneLinearOffset
		= ReadMmio(device, valleyview::kPlaneLinearOffsetA);
	device.originalPlaneStride
		= ReadMmio(device, valleyview::kPlaneStrideA);
	device.originalPlaneSurface
		= ReadMmio(device, valleyview::kPlaneSurfaceA);
	device.originalPlaneTileOffset
		= ReadMmio(device, valleyview::kPlaneTileOffsetA);
	device.originalPanelFitterControl
		= ReadMmio(device, valleyview::kPanelFitterControl);
	device.originalCxsr = ReadMmio(device, valleyview::kFwBlcSelfVlv);
	device.savedPwmControl = ReadMmio(device, valleyview::kPwmControlA);
	device.originalCursorControl
		= ReadMmio(device, valleyview::kCursorControlA);
	device.originalCursorBase = ReadMmio(device, valleyview::kCursorBaseA);
	device.originalCursorPosition
		= ReadMmio(device, valleyview::kCursorPositionA);
	for (uint32 index = 0; index < 2; index++) {
		device.originalCursorPalette[index]
			= ReadMmio(device, valleyview::kCursorPaletteA
				+ index * sizeof(uint32));
	}
}


status_t
RestoreFirmwarePlane(ValleyViewDevice& device)
{
	WriteMmio(device, valleyview::kPlaneControlA,
		device.originalPlaneControl & ~valleyview::kPlaneEnable);
	WriteMmio(device, valleyview::kPlaneSurfaceA, 0);
	ReadMmio(device, valleyview::kPlaneSurfaceA);
	snooze(kPlaneTransitionDelayUs);

	WriteMmio(device, valleyview::kPipeSourceA,
		device.originalPipeSource);
	WriteMmio(device, valleyview::kPlaneAddressVlvA,
		device.originalPlaneAddress);
	WriteMmio(device, valleyview::kPlaneLinearOffsetA,
		device.originalPlaneLinearOffset);
	WriteMmio(device, valleyview::kPlaneStrideA,
		device.originalPlaneStride);
	WriteMmio(device, valleyview::kPlaneTileOffsetA,
		device.originalPlaneTileOffset);
	WriteMmio(device, valleyview::kPanelFitterControl,
		device.originalPanelFitterControl);
	WriteMmio(device, valleyview::kPlaneControlA,
		device.originalPlaneControl);
	WriteMmio(device, valleyview::kPlaneSurfaceA,
		device.originalPlaneSurface);
	ReadMmio(device, valleyview::kPlaneSurfaceA);
	snooze(kPlaneTransitionDelayUs);
	WriteMmio(device, valleyview::kFwBlcSelfVlv, device.originalCxsr);
	ReadMmio(device, valleyview::kFwBlcSelfVlv);
	WriteMmio(device, valleyview::kPwmControlA, device.savedPwmControl);
	const bigtime_t deadline = system_time() + kPlaneLatchTimeoutUs;
	do {
		if ((ReadMmio(device, valleyview::kPlaneSurfaceLiveA)
				& ~valleyview::kPageMask)
				== (device.originalPlaneSurface & ~valleyview::kPageMask)
			&& ReadMmio(device, valleyview::kPipeSourceA)
				== device.originalPipeSource
			&& ReadMmio(device, valleyview::kPlaneStrideA)
				== device.originalPlaneStride
			&& ReadMmio(device, valleyview::kPlaneControlA)
				== device.originalPlaneControl
			&& ReadMmio(device, valleyview::kPanelFitterControl)
				== device.originalPanelFitterControl) {
			return B_OK;
		}
		snooze(100);
	} while (system_time() < deadline);
	return B_TIMED_OUT;
}


status_t
DisableCandidateCursor(ValleyViewDevice& device)
{
	WriteMmio(device, valleyview::kCursorControlA, 0);
	WriteMmio(device, valleyview::kCursorPositionA,
		device.originalCursorPosition);
	WriteMmio(device, valleyview::kCursorBaseA, 0);
	ReadMmio(device, valleyview::kCursorBaseA);
	const bigtime_t deadline = system_time() + kPlaneLatchTimeoutUs;
	do {
		if ((ReadMmio(device, valleyview::kCursorControlA)
				& valleyview::kCursorModeMask) == 0
			&& ReadMmio(device, valleyview::kCursorBaseA) == 0
			&& ReadMmio(device, valleyview::kCursorSurfaceLiveA) == 0) {
			return B_OK;
		}
		snooze(100);
	} while (system_time() < deadline);
	return B_TIMED_OUT;
}


status_t
RestoreFirmwareCursor(ValleyViewDevice& device)
{
	WriteMmio(device, valleyview::kCursorControlA, 0);
	for (uint32 index = 0; index < 2; index++) {
		WriteMmio(device, valleyview::kCursorPaletteA
			+ index * sizeof(uint32), device.originalCursorPalette[index]);
	}
	WriteMmio(device, valleyview::kCursorControlA,
		device.originalCursorControl);
	WriteMmio(device, valleyview::kCursorPositionA,
		device.originalCursorPosition);
	WriteMmio(device, valleyview::kCursorBaseA, device.originalCursorBase);
	ReadMmio(device, valleyview::kCursorBaseA);
	const bigtime_t deadline = system_time() + kPlaneLatchTimeoutUs;
	do {
		if (ReadMmio(device, valleyview::kCursorControlA)
				== device.originalCursorControl
			&& ReadMmio(device, valleyview::kCursorBaseA)
				== device.originalCursorBase
			&& ReadMmio(device, valleyview::kCursorPositionA)
				== device.originalCursorPosition
			&& ReadMmio(device, valleyview::kCursorSurfaceLiveA)
				== device.originalCursorBase) {
			return B_OK;
		}
		snooze(100);
	} while (system_time() < deadline);
	return B_TIMED_OUT;
}


status_t
TakeNativeScanout(ValleyViewDevice& device)
{
	status_t status = ValidateLiveWinkyState(device);
	if (status != B_OK)
		return status;

	SaveFirmwarePlane(device);
	WriteMmio(device, valleyview::kPwmControlA,
		valleyview::SetPwmDuty(device.savedPwmControl, 0));
	WriteMmio(device, valleyview::kFwBlcSelfVlv,
		device.originalCxsr & ~valleyview::kFwCxsrEnable);
	ReadMmio(device, valleyview::kFwBlcSelfVlv);
	if ((device.originalCxsr & valleyview::kFwCxsrEnable) != 0)
		snooze(kPlaneTransitionDelayUs);

	WriteMmio(device, valleyview::kPlaneControlA,
		device.originalPlaneControl & ~valleyview::kPlaneEnable);
	WriteMmio(device, valleyview::kPlaneSurfaceA, 0);
	ReadMmio(device, valleyview::kPlaneSurfaceA);
	snooze(kPlaneTransitionDelayUs);

	WriteMmio(device, valleyview::kPipeSourceA,
		valleyview::EncodePipeSource(valleyview::kP0Width,
			valleyview::kP0Height));
	WriteMmio(device, valleyview::kPlaneAddressVlvA, 0);
	WriteMmio(device, valleyview::kPlaneLinearOffsetA, 0);
	WriteMmio(device, valleyview::kPlaneTileOffsetA, 0);
	WriteMmio(device, valleyview::kPlaneStrideA,
		valleyview::kP0BytesPerRow);
	WriteMmio(device, valleyview::kPlaneControlA,
		device.originalPlaneControl | valleyview::kPlaneEnable);
	WriteMmio(device, valleyview::kPlaneSurfaceA,
		device.p0Layout.scanout[0]);
	ReadMmio(device, valleyview::kPlaneSurfaceA);

	const bigtime_t deadline = system_time() + kPlaneLatchTimeoutUs;
	do {
		if ((ReadMmio(device, valleyview::kPlaneSurfaceLiveA)
				& ~valleyview::kPageMask)
				== device.p0Layout.scanout[0]
			&& ReadMmio(device, valleyview::kPlaneStrideA)
				== valleyview::kP0BytesPerRow
			&& ReadMmio(device, valleyview::kPipeSourceA)
				== valleyview::EncodePipeSource(valleyview::kP0Width,
					valleyview::kP0Height)
			&& (ReadMmio(device, valleyview::kPlaneControlA)
				& valleyview::kPlaneEnable) != 0
			&& ReadMmio(device, valleyview::kPanelFitterControl)
				== device.originalPanelFitterControl) {
			WriteMmio(device, valleyview::kPwmControlA,
				device.savedPwmControl);
			return B_OK;
		}
		snooze(100);
	} while (system_time() < deadline);

	status = RestoreFirmwarePlane(device);
	if (status != B_OK) {
		device.p0MemoryQuarantined = true;
		device.gpuFaulted = true;
		return status;
	}
	return B_TIMED_OUT;
}


void
ProgramCursor(ValleyViewDevice& device)
{
	const int32 x = device.cursorX - static_cast<int32>(device.cursorHotX);
	const int32 y = device.cursorY - static_cast<int32>(device.cursorHotY);
	const uint32 control = device.cursorVisible && !device.softBlanked
		? device.cursorMode : 0;
	WriteMmio(device, valleyview::kCursorControlA, control);
	WriteMmio(device, valleyview::kCursorPositionA,
		valleyview::EncodeCursorPosition(x, y));
	WriteMmio(device, valleyview::kCursorBaseA, device.p0Layout.cursor);
	ReadMmio(device, valleyview::kCursorBaseA);
}


void
InitializeCursorMemory(ValleyViewDevice& device)
{
	uint8* cursor = static_cast<uint8*>(device.p0Private);
	memset(cursor, 0, valleyview::kP0CursorBytes);
	for (uint32 y = 0; y < valleyview::kCursorMaxHeight; y++)
		memset(cursor + y * 16, 0xff, 8);
	memory_write_barrier();
	WriteMmio(device, valleyview::kCursorPaletteA, 0x00ffffff);
	WriteMmio(device, valleyview::kCursorPaletteA + 4, 0);
	device.cursorMode = valleyview::kCursorMode64TwoColor;
	device.cursorReady = true;
	ProgramCursor(device);
}


int32
ScanoutIndex(const ValleyViewDevice& device, uint32 surface)
{
	surface &= ~valleyview::kPageMask;
	for (int32 index = 0; index < 2; index++) {
		if (surface == device.p0Layout.scanout[index])
			return index;
	}
	return -1;
}


bool
IsP0PlaneSurface(const ValleyViewDevice& device, uint32 surface)
{
	surface &= ~valleyview::kPageMask;
	return surface == device.p0Layout.framebuffer
		|| ScanoutIndex(device, surface) >= 0;
}


uint32
PresentTestPixel(uint32 x, uint32 y)
{
	return 0xff000000u | ((x * 37 + y * 17) & 0xff)
		| (((x * 11) ^ (y * 29)) & 0xff) << 8
		| ((x + y * 3) & 0xff) << 16;
}


status_t
VerifyBcsPresent(ValleyViewDevice& device)
{
	constexpr uint8 kSourcePadding = 0x3c;
	constexpr uint8 kDestinationPattern = 0xa5;
	memset(device.framebuffer, kSourcePadding, device.p0Size);
	memset(device.scanout[1], kDestinationPattern, device.p0Size);
	for (uint32 y = 0; y < valleyview::kP0Height; y++) {
		uint32* row = reinterpret_cast<uint32*>(
			static_cast<uint8*>(device.framebuffer)
				+ y * valleyview::kP0BytesPerRow);
		for (uint32 x = 0; x < valleyview::kP0Width; x++)
			row[x] = PresentTestPixel(x, y);
	}
	memory_write_barrier();

	status_t status = SubmitBcsPresent(device, device.p0Layout.framebuffer,
		device.p0Layout.scanout[1]);
	if (status == B_OK) {
		memory_read_barrier();
		const uint32 visibleBytes = valleyview::kP0Width * sizeof(uint32);
		for (uint32 y = 0; status == B_OK && y < valleyview::kP0Height; y++) {
			const uint8* row = static_cast<const uint8*>(device.scanout[1])
				+ y * valleyview::kP0BytesPerRow;
			const uint32* pixels = reinterpret_cast<const uint32*>(row);
			for (uint32 x = 0; x < valleyview::kP0Width; x++) {
				if (pixels[x] != PresentTestPixel(x, y)) {
					status = B_BAD_DATA;
					break;
				}
			}
			for (uint32 x = visibleBytes;
					status == B_OK && x < valleyview::kP0BytesPerRow; x++) {
				if (row[x] != kDestinationPattern) {
					status = B_BAD_DATA;
					break;
				}
			}
		}
	}

	if (!device.gpuFaulted) {
		memset(device.framebuffer, 0, device.p0Size);
		memset(device.scanout[0], 0, device.p0Size);
		memset(device.scanout[1], 0, device.p0Size);
		memory_write_barrier();
	}
	return status;
}


status_t
WaitForPrivateScanoutDetachedLocked(ValleyViewDevice& device,
	bigtime_t timeout)
{
	const bigtime_t deadline = system_time() + timeout;
	do {
		const bool planeDisabled
			= (ReadMmio(device, valleyview::kPlaneControlA)
				& valleyview::kPlaneEnable) == 0;
		const bool privateSurfaceDetached
			= !IsP0PlaneSurface(device,
				ReadMmio(device, valleyview::kPlaneSurfaceLiveA));
		if (planeDisabled && privateSurfaceDetached)
			return B_OK;
		snooze(kPresentPollUs);
	} while (system_time() < deadline);
	return B_TIMED_OUT;
}


status_t
CopyPresentFrameLocked(ValleyViewDevice& device, int32 target)
{
	if (target < 0 || target >= 2)
		return B_BAD_VALUE;

	const bigtime_t started = system_time();
	status_t status = B_NOT_SUPPORTED;
	if (device.presentUsesBcs) {
		// SubmitBcsPresent retires its MI_FLUSH_DW marker before this surface
		// can be armed for display.
		status = SubmitBcsPresent(device, device.p0Layout.framebuffer,
			device.p0Layout.scanout[target]);
		if (status == B_OK)
			device.presentBcsCopies++;
		else {
			device.presentBcsStatus = status;
			device.presentUsesBcs = false;
		}
	}
	if (status != B_OK) {
		if (device.gpuFaulted)
			return status;
		memcpy(device.scanout[target], device.framebuffer, device.p0Size);
		memory_write_barrier();
		device.presentCpuCopies++;
		status = B_OK;
	}

	const bigtime_t elapsed = system_time() - started;
	device.presentCopyLastUs = elapsed;
	if (static_cast<uint64>(elapsed) > device.presentCopyMaxUs)
		device.presentCopyMaxUs = elapsed;
	return status;
}


status_t
LatchScanoutLocked(ValleyViewDevice& device, int32 target,
	bigtime_t timeout)
{
	if (target < 0 || target >= 2)
		return B_BAD_VALUE;

	WriteMmio(device, valleyview::kPlaneSurfaceA,
		device.p0Layout.scanout[target]);
	ReadMmio(device, valleyview::kPlaneSurfaceA);
	const bigtime_t started = system_time();
	const bigtime_t deadline = started + timeout;
	do {
		if (ScanoutIndex(device,
				ReadMmio(device, valleyview::kPlaneSurfaceLiveA)) == target) {
			const bigtime_t elapsed = system_time() - started;
			device.presentFlipLastUs = elapsed;
			if (static_cast<uint64>(elapsed) > device.presentFlipMaxUs)
				device.presentFlipMaxUs = elapsed;
			device.activeScanout = target;
			return B_OK;
		}
		snooze(kPresentPollUs);
	} while (system_time() < deadline);
	return B_TIMED_OUT;
}


int32
PresentWorker(void* cookie)
{
	ValleyViewDevice& device = *static_cast<ValleyViewDevice*>(cookie);
	for (;;) {
		mutex_lock(&device.presentLock);
		if (!device.presentRunning) {
			mutex_unlock(&device.presentLock);
			break;
		}
		if (!device.presentEnabled) {
			mutex_unlock(&device.presentLock);
			snooze(1000);
			continue;
		}

		if (device.pendingScanout >= 0) {
			const int32 live = ScanoutIndex(device,
				ReadMmio(device, valleyview::kPlaneSurfaceLiveA));
			if (live == device.pendingScanout) {
				const bigtime_t elapsed
					= system_time() - device.presentFlipStarted;
				device.presentFlipLastUs = elapsed;
				if (static_cast<uint64>(elapsed) > device.presentFlipMaxUs)
					device.presentFlipMaxUs = elapsed;
				device.activeScanout = live;
				device.pendingScanout = -1;
				device.presentPendingTimedOut = false;
				device.presentFrames++;
				device.presentStatus = B_OK;
			} else {
				const bigtime_t elapsed
					= system_time() - device.presentFlipStarted;
				if (elapsed >= kPresentLatchWarningUs
					&& !device.presentPendingTimedOut) {
					device.presentPendingTimedOut = true;
					device.presentFailures++;
					device.presentStatus = B_TIMED_OUT;
					dprintf("intel_valleyview: P0 present latch pending for %"
						B_PRIdBIGTIME " us target=%" B_PRId32
						" live=%" B_PRId32 "\n", elapsed,
						device.pendingScanout, live);
				}
				mutex_unlock(&device.presentLock);
				snooze(kPresentPollUs);
				continue;
			}
		}

		const int32 live = ScanoutIndex(device,
			ReadMmio(device, valleyview::kPlaneSurfaceLiveA));
		if (live < 0) {
			device.presentFailures++;
			device.presentStatus = B_BAD_DATA;
			device.presentEnabled = false;
			dprintf("intel_valleyview: P0 present lost live scanout\n");
			mutex_unlock(&device.presentLock);
			continue;
		}

		const int32 target = 1 - live;
		status_t status = CopyPresentFrameLocked(device, target);
		if (status == B_OK) {
			WriteMmio(device, valleyview::kPlaneSurfaceA,
				device.p0Layout.scanout[target]);
			ReadMmio(device, valleyview::kPlaneSurfaceA);
			device.pendingScanout = target;
			device.presentFlipStarted = system_time();
			device.presentPendingTimedOut = false;
		} else {
			device.presentFailures++;
			device.presentStatus = status;
			device.presentEnabled = false;
			dprintf("intel_valleyview: P0 present copy failed: %"
				B_PRId32 "\n", status);
		}
		mutex_unlock(&device.presentLock);
	}
	return B_OK;
}


status_t
StartPresentWorker(ValleyViewDevice& device)
{
	mutex_lock(&device.presentLock);
	if (device.presentRunning) {
		mutex_unlock(&device.presentLock);
		return B_OK;
	}
	const int32 live = ScanoutIndex(device,
		ReadMmio(device, valleyview::kPlaneSurfaceLiveA));
	if (live < 0) {
		mutex_unlock(&device.presentLock);
		return B_BAD_DATA;
	}

	device.activeScanout = live;
	device.pendingScanout = -1;
	device.presentStatus = B_OK;
	device.presentEnabled = true;
	device.presentRunning = true;
	device.presentThread = spawn_kernel_thread(PresentWorker,
		"intel_valleyview present", B_URGENT_DISPLAY_PRIORITY, &device);
	if (device.presentThread < B_OK) {
		status_t status = device.presentThread;
		device.presentThread = -1;
		device.presentRunning = false;
		device.presentEnabled = false;
		device.presentStatus = status;
		mutex_unlock(&device.presentLock);
		return status;
	}
	status_t status = resume_thread(device.presentThread);
	if (status != B_OK) {
		kill_thread(device.presentThread);
		device.presentThread = -1;
		device.presentRunning = false;
		device.presentEnabled = false;
		device.presentStatus = status;
	}
	mutex_unlock(&device.presentLock);
	return status;
}


status_t
StopPresentWorker(ValleyViewDevice& device)
{
	mutex_lock(&device.presentLock);
	const thread_id thread = device.presentThread;
	device.presentRunning = false;
	device.presentEnabled = false;
	mutex_unlock(&device.presentLock);

	status_t status = B_OK;
	if (thread >= B_OK) {
		status_t threadResult = B_OK;
		status = wait_for_thread(thread, &threadResult);
		if (status == B_OK)
			status = threadResult;
	}

	mutex_lock(&device.presentLock);
	device.presentThread = -1;
	mutex_unlock(&device.presentLock);
	return status;
}


status_t
BlankDisplay(ValleyViewDevice& device)
{
	if (device.softBlanked)
		return B_OK;
	mutex_lock(&device.presentLock);
	device.presentEnabled = false;
	WriteMmio(device, valleyview::kPwmControlA,
		valleyview::SetPwmDuty(device.savedPwmControl, 0));
	WriteMmio(device, valleyview::kFwBlcSelfVlv,
		ReadMmio(device, valleyview::kFwBlcSelfVlv)
			& ~valleyview::kFwCxsrEnable);
	ReadMmio(device, valleyview::kFwBlcSelfVlv);
	WriteMmio(device, valleyview::kCursorControlA, 0);
	WriteMmio(device, valleyview::kCursorPositionA,
		valleyview::EncodeCursorPosition(device.cursorX
				- static_cast<int32>(device.cursorHotX),
			device.cursorY - static_cast<int32>(device.cursorHotY)));
	WriteMmio(device, valleyview::kCursorBaseA, device.p0Layout.cursor);
	WriteMmio(device, valleyview::kPlaneControlA,
		device.originalPlaneControl & ~valleyview::kPlaneEnable);
	WriteMmio(device, valleyview::kPlaneSurfaceA, 0);
	ReadMmio(device, valleyview::kPlaneSurfaceA);
	status_t status = WaitForPrivateScanoutDetachedLocked(device,
		kPlaneLatchTimeoutUs);
	if (status == B_OK) {
		device.activeScanout = -1;
		device.pendingScanout = -1;
		device.presentPendingTimedOut = false;
	} else {
		device.presentStatus = status;
		device.presentFailures++;
	}
	mutex_unlock(&device.presentLock);
	device.softBlanked = true;
	return status;
}


status_t
UnblankDisplay(ValleyViewDevice& device)
{
	if (!device.softBlanked)
		return B_OK;
	mutex_lock(&device.presentLock);
	status_t status = WaitForPrivateScanoutDetachedLocked(device,
		kPlaneLatchTimeoutUs);
	if (status != B_OK) {
		mutex_unlock(&device.presentLock);
		return status;
	}
	status = CopyPresentFrameLocked(device, 0);
	if (status != B_OK) {
		mutex_unlock(&device.presentLock);
		return status;
	}
	WriteMmio(device, valleyview::kPlaneControlA,
		device.originalPlaneControl | valleyview::kPlaneEnable);
	device.pendingScanout = 0;
	device.presentFlipStarted = system_time();
	status = LatchScanoutLocked(device, 0, kPlaneLatchTimeoutUs);
	if (status != B_OK) {
		WriteMmio(device, valleyview::kPlaneControlA,
			device.originalPlaneControl & ~valleyview::kPlaneEnable);
		WriteMmio(device, valleyview::kPlaneSurfaceA, 0);
		ReadMmio(device, valleyview::kPlaneSurfaceA);
		status_t detachStatus = WaitForPrivateScanoutDetachedLocked(device,
			kPlaneLatchTimeoutUs);
		if (detachStatus == B_OK) {
			device.activeScanout = -1;
			device.pendingScanout = -1;
		} else
			status = detachStatus;
		device.presentStatus = status;
		device.presentFailures++;
		mutex_unlock(&device.presentLock);
		return status;
	}
	device.pendingScanout = -1;
	device.presentPendingTimedOut = false;
	device.presentEnabled = true;
	mutex_unlock(&device.presentLock);
	device.softBlanked = false;
	ProgramCursor(device);
	WriteMmio(device, valleyview::kPwmControlA, device.savedPwmControl);
	return B_OK;
}

} // namespace


void
ReleaseP0Areas(ValleyViewDevice& device)
{
	DeleteP0Areas(device);
}


status_t
ValidateP0FirmwareState(const ValleyViewDevice& device)
{
	return ValidateLiveWinkyState(device);
}


status_t
InitializeP0(ValleyViewDevice& device)
{
	if (device.nativeActive)
		return B_OK;
	if (!device.enabled || !device.allowModeset || device.registers == NULL
		|| device.gpuFaulted || device.p0MemoryQuarantined) {
		return B_NOT_ALLOWED;
	}
	status_t status = ValidateP0FirmwareState(device);
	if (status != B_OK) {
		dprintf("intel_valleyview: P0 takeover rejected unknown firmware "
			"or stale display state: %" B_PRId32 "\n", status);
		device.nativeStatus = status;
		return device.nativeStatus;
	}

	status = AllocateP0Memory(device);
	if (status == B_OK)
		status = TakeNativeScanout(device);
	if (status != B_OK) {
		if (!device.p0MemoryQuarantined) {
			const status_t restoreStatus = RestoreP0Ptes(device);
			if (restoreStatus != B_OK && device.framebufferArea >= B_OK) {
				device.p0MemoryQuarantined = true;
				device.gpuFaulted = true;
				status = restoreStatus;
				dprintf("intel_valleyview: quarantining P0 allocation after "
					"GGTT rollback failure: %" B_PRId32 "\n",
					restoreStatus);
			}
		}
		if (!device.p0MemoryQuarantined)
			ReleaseP0Areas(device);
		device.framebuffer = NULL;
		device.p0Private = NULL;
		free(device.p0SavedPtes);
		device.p0SavedPtes = NULL;
		device.nativeStatus = status;
		return status;
	}

	device.nativeActive = true;
	device.nativeStatus = B_OK;
	device.dpmsMode = B_DPMS_ON;
	InitializeCursorMemory(device);
	device.bcsStatus = InitializeBcsRuntime(device);
	if (device.gpuFaulted) {
		const status_t bcsFailure = device.bcsStatus;
		const status_t shutdownStatus = ShutdownP0(device);
		if (!device.p0MemoryQuarantined)
			ReleaseP0Areas(device);
		device.nativeStatus = shutdownStatus != B_OK
			? shutdownStatus : bcsFailure;
		return device.nativeStatus;
	}
	device.presentBcsStatus = device.bcsStatus == B_OK
		? VerifyBcsPresent(device) : device.bcsStatus;
	device.presentUsesBcs = device.presentBcsStatus == B_OK;
	if (device.gpuFaulted) {
		const status_t presentFailure = device.presentBcsStatus;
		const status_t shutdownStatus = ShutdownP0(device);
		if (!device.p0MemoryQuarantined)
			ReleaseP0Areas(device);
		device.nativeStatus = shutdownStatus != B_OK
			? shutdownStatus : presentFailure;
		return device.nativeStatus;
	}
	device.presentStatus = StartPresentWorker(device);
	if (device.presentStatus != B_OK) {
		const status_t presentFailure = device.presentStatus;
		const status_t shutdownStatus = ShutdownP0(device);
		if (!device.p0MemoryQuarantined)
			ReleaseP0Areas(device);
		device.nativeStatus = shutdownStatus != B_OK
			? shutdownStatus : presentFailure;
		return device.nativeStatus;
	}
	dprintf("intel_valleyview: P0 native scanout %ux%u stride=%u ggtt=%#"
		B_PRIx32 " scanout=%#" B_PRIx32 "/%#" B_PRIx32
		" pages=%" B_PRIu32 " bcs=%" B_PRId32 " present=%s\n",
		valleyview::kP0Width, valleyview::kP0Height,
		valleyview::kP0BytesPerRow, device.p0Layout.framebuffer,
		device.p0Layout.scanout[0], device.p0Layout.scanout[1],
		device.p0Layout.pageCount, device.bcsStatus,
		device.presentUsesBcs ? "bcs" : "cpu");
	return B_OK;
}


status_t
ShutdownP0(ValleyViewDevice& device)
{
	if (!device.nativeActive)
		return B_OK;

	const status_t presentStatus = StopPresentWorker(device);
	const status_t cursorStatus = DisableCandidateCursor(device);
	const status_t bcsStatus = QuiesceBcsRuntime(device);
	const status_t planeStatus = RestoreFirmwarePlane(device);
	status_t pteStatus = B_NOT_ALLOWED;
	if (presentStatus == B_OK && cursorStatus == B_OK && bcsStatus == B_OK
		&& planeStatus == B_OK) {
		pteStatus = RestoreP0Ptes(device);
	}
	const status_t restoreCursorStatus = RestoreFirmwareCursor(device);

	device.nativeActive = false;
	device.cursorReady = false;
	device.softBlanked = false;
	device.activeScanout = -1;
	device.pendingScanout = -1;
	status_t status = presentStatus;
	if (status == B_OK)
		status = cursorStatus;
	if (status == B_OK)
		status = bcsStatus;
	if (status == B_OK)
		status = planeStatus;
	if (status == B_OK)
		status = pteStatus;
	if (status == B_OK)
		status = restoreCursorStatus;
	if (presentStatus != B_OK || cursorStatus != B_OK || bcsStatus != B_OK
		|| planeStatus != B_OK || pteStatus != B_OK) {
		device.p0MemoryQuarantined = true;
		device.gpuFaulted = true;
		dprintf("intel_valleyview: quarantining P0 allocation; present=%"
			B_PRId32 " cursor=%" B_PRId32 " bcs=%" B_PRId32
			" plane=%" B_PRId32 " ggtt=%" B_PRId32 "\n",
			presentStatus, cursorStatus, bcsStatus, planeStatus, pteStatus);
	}
	device.nativeStatus = status;
	free(device.p0SavedPtes);
	device.p0SavedPtes = NULL;
	return status;
}


void
GetP0Status(ValleyViewDevice& device, valleyview::P0Status& status)
{
	mutex_lock(&device.presentLock);
	mutex_lock(&device.bcsLock);
	memset(&status, 0, sizeof(status));
	status.header = valleyview::MakeAbiHeader(sizeof(status));
	if (device.framebufferArea >= B_OK)
		status.flags |= valleyview::kP0MemoryAllocated;
	if (device.p0SavedPtes != NULL)
		status.flags |= valleyview::kP0GgttMapped;
	if (device.nativeActive)
		status.flags |= valleyview::kP0NativeScanout;
	if (device.bcsReady)
		status.flags |= valleyview::kP0BcsReady;
	if (device.cursorReady)
		status.flags |= valleyview::kP0CursorReady;
	if (device.softBlanked)
		status.flags |= valleyview::kP0SoftBlanked;
	if (device.gpuFaulted)
		status.flags |= valleyview::kP0Faulted;
	if (device.presentRunning)
		status.flags |= valleyview::kP0PresentReady;
	if (device.presentUsesBcs)
		status.flags |= valleyview::kP0PresentBcs;
	if (device.pendingScanout >= 0)
		status.flags |= valleyview::kP0PresentPending;

	status.nativeStatus = device.nativeStatus;
	status.bcsStatus = device.bcsStatus;
	status.presentStatus = device.presentStatus;
	status.presentBcsStatus = device.presentBcsStatus;
	status.width = device.nativeActive
		? valleyview::kP0Width : device.snapshot.bootWidth;
	status.height = device.nativeActive
		? valleyview::kP0Height : device.snapshot.bootHeight;
	status.bytesPerRow = device.nativeActive
		? valleyview::kP0BytesPerRow : device.snapshot.bootBytesPerRow;
	status.ggttOffset = device.p0Layout.base;
	status.ggttPages = device.p0Layout.pageCount;
	status.physical = device.p0Physical;
	status.scanoutPhysical[0] = device.scanoutPhysical[0];
	status.scanoutPhysical[1] = device.scanoutPhysical[1];
	status.framebufferOffset = device.p0Layout.framebuffer;
	status.scanoutOffset[0] = device.p0Layout.scanout[0];
	status.scanoutOffset[1] = device.p0Layout.scanout[1];
	status.cursorOffset = device.p0Layout.cursor;
	status.ringOffset = device.p0Layout.ring;
	status.statusOffset = device.p0Layout.status;
	status.activeScanout = device.activeScanout;
	status.pendingScanout = device.pendingScanout;
	status.dpmsMode = device.dpmsMode;
	status.pwmDuty = device.nativeActive
		? valleyview::PwmDuty(ReadMmio(device, valleyview::kPwmControlA))
		: device.snapshot.pwmDuty;
	status.pwmPeriod = device.nativeActive
		? valleyview::PwmPeriod(ReadMmio(device, valleyview::kPwmControlA))
		: device.snapshot.pwmPeriod;
	if (device.registers != NULL) {
		status.pipeSource = ReadMmio(device, valleyview::kPipeSourceA);
		status.planeControl = ReadMmio(device, valleyview::kPlaneControlA);
		status.planeStride = ReadMmio(device, valleyview::kPlaneStrideA);
		status.planeSurface = ReadMmio(device, valleyview::kPlaneSurfaceA);
		status.planeSurfaceLive
			= ReadMmio(device, valleyview::kPlaneSurfaceLiveA);
		status.panelFitterControl
			= ReadMmio(device, valleyview::kPanelFitterControl);
		status.panelFitterProgrammedRatios
			= ReadMmio(device, valleyview::kPanelFitterProgrammedRatios);
		status.panelFitterAutoRatios
			= ReadMmio(device, valleyview::kPanelFitterAutoRatios);
		status.cursorControl = ReadMmio(device, valleyview::kCursorControlA);
		status.cursorBase = ReadMmio(device, valleyview::kCursorBaseA);
		status.cursorPosition = ReadMmio(device,
			valleyview::kCursorPositionA);
		status.cursorSurfaceLive = ReadMmio(device,
			valleyview::kCursorSurfaceLiveA);
	}
	status.cursorVisible = device.cursorVisible ? 1 : 0;
	status.bcsSubmissions = device.bcsSubmissions;
	status.bcsFailures = device.bcsFailures;
	status.bcsFillRequests = device.bcsFillRequests;
	status.bcsBlitRequests = device.bcsBlitRequests;
	status.bcsPresentRequests = device.bcsPresentRequests;
	status.cpuFillFallbacks = device.cpuFillFallbacks;
	status.cpuBlitFallbacks = device.cpuBlitFallbacks;
	status.presentFrames = device.presentFrames;
	status.presentFailures = device.presentFailures;
	status.presentBcsCopies = device.presentBcsCopies;
	status.presentCpuCopies = device.presentCpuCopies;
	status.presentCopyLastUs = device.presentCopyLastUs;
	status.presentCopyMaxUs = device.presentCopyMaxUs;
	status.presentFlipLastUs = device.presentFlipLastUs;
	status.presentFlipMaxUs = device.presentFlipMaxUs;
	status.cursorShapeUpdates = device.cursorShapeUpdates;
	status.cursorBitmapUpdates = device.cursorBitmapUpdates;
	status.cursorMoveUpdates = device.cursorMoveUpdates;
	status.cursorShowUpdates = device.cursorShowUpdates;
	mutex_unlock(&device.bcsLock);
	mutex_unlock(&device.presentLock);
}


status_t
GetBrightness(ValleyViewDevice& device,
	valleyview::BrightnessRequest& request)
{
	mutex_lock(&device.lock);
	if (!device.nativeActive) {
		mutex_unlock(&device.lock);
		return B_NO_INIT;
	}
	const uint32 control = device.softBlanked
		? device.savedPwmControl
		: ReadMmio(device, valleyview::kPwmControlA);
	const uint16 period = valleyview::PwmPeriod(control);
	request.header = valleyview::MakeAbiHeader(sizeof(request));
	request.value = period != 0
		? static_cast<float>(valleyview::PwmDuty(control)) / period : 0.0f;
	mutex_unlock(&device.lock);
	return period != 0 ? B_OK : B_BAD_DATA;
}


status_t
SetBrightness(ValleyViewDevice& device,
	const valleyview::BrightnessRequest& request)
{
	mutex_lock(&device.lock);
	if (!device.nativeActive) {
		mutex_unlock(&device.lock);
		return B_NO_INIT;
	}
	const uint16 period = valleyview::PwmPeriod(device.savedPwmControl);
	uint16 duty;
	if (!valleyview::BrightnessDuty(period, request.value, duty)) {
		mutex_unlock(&device.lock);
		return B_BAD_VALUE;
	}
	device.savedPwmControl
		= valleyview::SetPwmDuty(device.savedPwmControl, duty);
	if (!device.softBlanked)
		WriteMmio(device, valleyview::kPwmControlA, device.savedPwmControl);
	mutex_unlock(&device.lock);
	return B_OK;
}


status_t
GetDpms(ValleyViewDevice& device, valleyview::DpmsRequest& request)
{
	mutex_lock(&device.lock);
	if (!device.nativeActive) {
		mutex_unlock(&device.lock);
		return B_NO_INIT;
	}
	request.header = valleyview::MakeAbiHeader(sizeof(request));
	request.mode = device.dpmsMode;
	mutex_unlock(&device.lock);
	return B_OK;
}


status_t
SetDpms(ValleyViewDevice& device, const valleyview::DpmsRequest& request)
{
	if (request.mode != B_DPMS_ON && request.mode != B_DPMS_STAND_BY
		&& request.mode != B_DPMS_SUSPEND && request.mode != B_DPMS_OFF) {
		return B_BAD_VALUE;
	}

	mutex_lock(&device.lock);
	if (!device.nativeActive) {
		mutex_unlock(&device.lock);
		return B_NO_INIT;
	}
	status_t status = request.mode == B_DPMS_ON
		? UnblankDisplay(device) : BlankDisplay(device);
	if (status == B_OK)
		device.dpmsMode = request.mode;
	mutex_unlock(&device.lock);
	return status;
}


status_t
SetCursorShape(ValleyViewDevice& device,
	const valleyview::CursorShapeRequest& request)
{
	if (request.width == 0 || request.height == 0
		|| request.width > valleyview::kCursorMaxWidth
		|| request.height > valleyview::kCursorMaxHeight
		|| request.hotX >= request.width || request.hotY >= request.height) {
		return B_BAD_VALUE;
	}

	mutex_lock(&device.lock);
	if (!device.nativeActive || !device.cursorReady) {
		mutex_unlock(&device.lock);
		return B_NO_INIT;
	}

	uint8* cursor = static_cast<uint8*>(device.p0Private);
	memset(cursor, 0, valleyview::kP0CursorBytes);
	for (uint32 y = 0; y < valleyview::kCursorMaxHeight; y++)
		memset(cursor + y * 16, 0xff, 8);
	const uint32 byteWidth = (request.width + 7) / 8;
	for (uint32 y = 0; y < request.height; y++) {
		memcpy(cursor + y * 16, request.andMask + y * byteWidth,
			byteWidth);
		memcpy(cursor + y * 16 + 8, request.xorMask + y * byteWidth,
			byteWidth);
	}
	memory_write_barrier();
	device.cursorHotX = request.hotX;
	device.cursorHotY = request.hotY;
	device.cursorMode = valleyview::kCursorMode64TwoColor;
	device.cursorShapeUpdates++;
	ProgramCursor(device);
	mutex_unlock(&device.lock);
	return B_OK;
}


status_t
SetCursorBitmap(ValleyViewDevice& device,
	const valleyview::CursorBitmapRequest& request)
{
	if (request.width == 0 || request.height == 0
		|| request.width > valleyview::kCursorMaxWidth
		|| request.height > valleyview::kCursorMaxHeight
		|| request.hotX >= request.width || request.hotY >= request.height) {
		return B_BAD_VALUE;
	}

	mutex_lock(&device.lock);
	if (!device.nativeActive || !device.cursorReady) {
		mutex_unlock(&device.lock);
		return B_NO_INIT;
	}
	memcpy(device.p0Private, request.pixels, valleyview::kP0CursorBytes);
	memory_write_barrier();
	device.cursorHotX = request.hotX;
	device.cursorHotY = request.hotY;
	device.cursorMode = valleyview::kCursorMode64Argb;
	device.cursorBitmapUpdates++;
	ProgramCursor(device);
	mutex_unlock(&device.lock);
	return B_OK;
}


status_t
MoveCursor(ValleyViewDevice& device,
	const valleyview::CursorMoveRequest& request)
{
	mutex_lock(&device.lock);
	if (!device.nativeActive || !device.cursorReady) {
		mutex_unlock(&device.lock);
		return B_NO_INIT;
	}
	device.cursorX = request.x;
	device.cursorY = request.y;
	device.cursorMoveUpdates++;
	ProgramCursor(device);
	mutex_unlock(&device.lock);
	return B_OK;
}


status_t
ShowCursor(ValleyViewDevice& device,
	const valleyview::CursorShowRequest& request)
{
	mutex_lock(&device.lock);
	if (!device.nativeActive || !device.cursorReady) {
		mutex_unlock(&device.lock);
		return B_NO_INIT;
	}
	device.cursorVisible = request.visible != 0;
	device.cursorShowUpdates++;
	ProgramCursor(device);
	mutex_unlock(&device.lock);
	return B_OK;
}
