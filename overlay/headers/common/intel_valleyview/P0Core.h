// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot
#ifndef INTEL_VALLEYVIEW_P0_CORE_H
#define INTEL_VALLEYVIEW_P0_CORE_H

#include <common/intel_valleyview/GpuCore.h>


namespace valleyview {

constexpr uint32 kP0Width = 1366;
constexpr uint32 kP0Height = 768;
constexpr uint32 kP0BytesPerRow = 5504;
constexpr uint32 kP0FramebufferBytes = kP0BytesPerRow * kP0Height;
constexpr uint32 kP0FramebufferPages
	= kP0FramebufferBytes / kPageSize;
constexpr uint32 kP0CursorPage = kP0FramebufferPages;
constexpr uint32 kP0CursorPages = 4;
constexpr uint32 kP0CursorBytes = kP0CursorPages * kPageSize;
constexpr uint32 kP0RingPage = kP0CursorPage + kP0CursorPages;
constexpr uint32 kP0StatusPage = kP0RingPage + 1;
constexpr uint32 kP0TestSourcePage = kP0StatusPage + 1;
constexpr uint32 kP0TestDestinationPage = kP0TestSourcePage + 1;
constexpr uint32 kP0PageCount = kP0TestDestinationPage + 1;
constexpr uint32 kP0AllocationBytes = kP0PageCount * kPageSize;
constexpr uint32 kP0PrivatePageCount = kP0PageCount - kP0FramebufferPages;
constexpr uint32 kP0PrivateBytes = kP0PrivatePageCount * kPageSize;

constexpr uint32 kWinkyFirmwareWidth = 1024;
constexpr uint32 kWinkyFirmwareHeight = 768;
constexpr uint32 kWinkyFirmwareStride = 4096;
constexpr uint32 kWinkyFirmwarePipeSource = 0x03ff02ff;
constexpr uint32 kWinkyHorizontalTotal = 1530;
constexpr uint32 kWinkyVerticalTotal = 793;
constexpr uint64 kWinkyGmadrBase = 0xd0000000ull;
constexpr uint64 kWinkyGmadrSize = 0x10000000ull;
constexpr uint32 kWinkyDpllA = 0xf000a00f;
constexpr uint32 kWinkyHTotalA = 0x05f90555;
constexpr uint32 kWinkyHBlankA = 0x05f90555;
constexpr uint32 kWinkyHSyncA = 0x059d057d;
constexpr uint32 kWinkyVTotalA = 0x031802ff;
constexpr uint32 kWinkyVBlankA = 0x031802ff;
constexpr uint32 kWinkyVSyncA = 0x03080302;
constexpr uint32 kWinkyPipeConfigA = 0xc0000050;
constexpr uint32 kWinkyPlaneControlA = 0x98000000;
constexpr uint32 kWinkyPanelFitterControl = 0x80000000;
constexpr uint32 kWinkyPanelFitterProgrammedRatios = 0x10000bff;
constexpr uint32 kWinkyDpC = 0xb0040004;
constexpr uint32 kWinkyPwmControl2 = 0x80000000;
constexpr uint16 kWinkyPwmPeriod = 7812;


struct P0Layout {
	uint32	base;
	uint32	framebuffer;
	uint32	cursor;
	uint32	ring;
	uint32	status;
	uint32	testSource;
	uint32	testDestination;
	uint32	pageCount;
};


inline bool
IsWinkyP0TakeoverState(const FirmwareSnapshot& snapshot)
{
	const uint32 requiredFlags = kSnapshotAdoptionCompatible
		| kSnapshotDpllEnabled | kSnapshotDpllLocked
		| kSnapshotPipeEnabled | kSnapshotPlaneEnabled
		| kSnapshotPortEnabled | kSnapshotPpsOn | kSnapshotPpsReady
		| kSnapshotPwmEnabled | kSnapshotPanelFitterEnabled
		| kSnapshotScanoutMatchesBoot | kSnapshotGttRangePresent;
	return snapshot.adoptionStatus == B_OK
		&& (snapshot.flags & requiredFlags) == requiredFlags
		&& snapshot.dpllA == kWinkyDpllA
		&& snapshot.hTotal == kWinkyHTotalA
		&& snapshot.hBlank == kWinkyHBlankA
		&& snapshot.hSync == kWinkyHSyncA
		&& snapshot.vTotal == kWinkyVTotalA
		&& snapshot.vBlank == kWinkyVBlankA
		&& snapshot.vSync == kWinkyVSyncA
		&& snapshot.hDisplay == kP0Width
		&& snapshot.vDisplay == kP0Height
		&& snapshot.hTotalPixels == kWinkyHorizontalTotal
		&& snapshot.vTotalLines == kWinkyVerticalTotal
		&& snapshot.pipeSource == kWinkyFirmwarePipeSource
		&& snapshot.pipeConfig == kWinkyPipeConfigA
		&& snapshot.sourceWidth == kWinkyFirmwareWidth
		&& snapshot.sourceHeight == kWinkyFirmwareHeight
		&& snapshot.planeControl == kWinkyPlaneControlA
		&& snapshot.planeAddressVlv == 0
		&& snapshot.planeLinearOffset == 0
		&& snapshot.planeStride == kWinkyFirmwareStride
		&& snapshot.planeSurface == 0
		&& snapshot.planeSurfaceLive == 0
		&& snapshot.planeTileOffset == 0
		&& snapshot.panelFitterControl == kWinkyPanelFitterControl
		&& snapshot.panelFitterProgrammedRatios
			== kWinkyPanelFitterProgrammedRatios
		&& snapshot.panelFitterAutoRatios == 0
		&& snapshot.dpC == kWinkyDpC
		&& snapshot.dpPipe == 0
		&& snapshot.ppsPort == 2
		&& snapshot.pwmControl2 == kWinkyPwmControl2
		&& snapshot.pwmPeriod == kWinkyPwmPeriod
		&& snapshot.pwmDuty <= snapshot.pwmPeriod
		&& snapshot.cursorControl == 0
		&& snapshot.cursorBase == 0
		&& snapshot.cursorPosition == 0
		&& snapshot.cursorSurfaceLive == 0
		&& snapshot.bootWidth == kWinkyFirmwareWidth
		&& snapshot.bootHeight == kWinkyFirmwareHeight
		&& snapshot.bootDepth == 32
		&& snapshot.bootBytesPerRow == kWinkyFirmwareStride
		&& snapshot.bootFramebufferSize
			== static_cast<uint64>(kWinkyFirmwareStride)
				* kWinkyFirmwareHeight
		&& snapshot.gmadrBase == kWinkyGmadrBase
		&& snapshot.gmadrSize == kWinkyGmadrSize
		&& snapshot.scanoutAperture == kWinkyGmadrBase
		&& snapshot.bootFramebufferPhysical == kWinkyGmadrBase
		&& snapshot.gttRequiredPages == 768
		&& snapshot.gttPresentPages == 768;
}


inline bool
P0PagePhysical(uint64 framebufferPhysical, uint64 privatePhysical,
	uint32 page, uint64& physical)
{
	if (page >= kP0PageCount
		|| (framebufferPhysical & kPageMask) != 0
		|| (privatePhysical & kPageMask) != 0) {
		return false;
	}
	if (page < kP0FramebufferPages)
		physical = framebufferPhysical + page * kPageSize;
	else {
		physical = privatePhysical
			+ (page - kP0FramebufferPages) * kPageSize;
	}
	return physical < 0x100000000ull;
}


inline bool
BuildP0Layout(uint64 gmadrSize, uint64 liveOffset, uint64 liveLength,
	P0Layout& layout)
{
	if (gmadrSize > UINT32_MAX || gmadrSize < kP0AllocationBytes
		|| (gmadrSize & kPageMask) != 0) {
		return false;
	}

	const uint64 base = gmadrSize - kP0AllocationBytes;
	if (RangesOverlap(base, kP0AllocationBytes, liveOffset, liveLength))
		return false;

	layout.base = static_cast<uint32>(base);
	layout.framebuffer = layout.base;
	layout.cursor = layout.base + kP0CursorPage * kPageSize;
	layout.ring = layout.base + kP0RingPage * kPageSize;
	layout.status = layout.base + kP0StatusPage * kPageSize;
	layout.testSource = layout.base + kP0TestSourcePage * kPageSize;
	layout.testDestination
		= layout.base + kP0TestDestinationPage * kPageSize;
	layout.pageCount = kP0PageCount;
	return true;
}


inline uint32
EncodePipeSource(uint32 width, uint32 height)
{
	if (width == 0 || height == 0 || width > 0x10000 || height > 0x10000)
		return 0;
	return ((width - 1) << 16) | (height - 1);
}


inline uint16
PwmPeriod(uint32 control)
{
	return static_cast<uint16>(control >> 16);
}


inline uint16
PwmDuty(uint32 control)
{
	return static_cast<uint16>(control);
}


inline uint32
SetPwmDuty(uint32 control, uint16 duty)
{
	return (control & 0xffff0000u) | duty;
}


inline bool
BrightnessDuty(uint16 period, float brightness, uint16& duty)
{
	if (period == 0 || !(brightness >= 0.0f && brightness <= 1.0f))
		return false;
	duty = static_cast<uint16>(brightness * period + 0.5f);
	return true;
}


inline uint32
EncodeCursorPosition(int32 x, int32 y)
{
	uint32 encodedX = static_cast<uint32>(x < 0 ? -x : x)
		& kCursorPositionMask;
	uint32 encodedY = static_cast<uint32>(y < 0 ? -y : y)
		& kCursorPositionMask;
	if (x < 0)
		encodedX |= kCursorPositionNegative;
	if (y < 0)
		encodedY |= kCursorPositionNegative;
	return (encodedY << 16) | encodedX;
}

} // namespace valleyview

#endif
