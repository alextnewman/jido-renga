// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot

#include "framework/jr_test.h"

#include <common/intel_valleyview/P0Core.h>


using namespace valleyview;


FirmwareSnapshot
WinkyFirmwareSnapshot()
{
	FirmwareSnapshot snapshot = {};
	snapshot.flags = kSnapshotMmioMapped;
	snapshot.dpllA = kWinkyDpllA;
	snapshot.hTotal = kWinkyHTotalA;
	snapshot.hBlank = kWinkyHBlankA;
	snapshot.hSync = kWinkyHSyncA;
	snapshot.vTotal = kWinkyVTotalA;
	snapshot.vBlank = kWinkyVBlankA;
	snapshot.vSync = kWinkyVSyncA;
	snapshot.pipeSource
		= EncodePipeSource(kWinkyFirmwareWidth, kWinkyFirmwareHeight);
	snapshot.pipeConfig = kWinkyPipeConfigA;
	snapshot.planeControl = kWinkyPlaneControlA;
	snapshot.planeStride = kWinkyFirmwareStride;
	snapshot.panelFitterControl = kWinkyPanelFitterControl;
	snapshot.panelFitterProgrammedRatios
		= kWinkyPanelFitterProgrammedRatios;
	snapshot.dpC = kWinkyDpC;
	snapshot.ppsStatus = kPpsOn | kPpsReady;
	snapshot.ppsOnDelays = 2u << 30;
	snapshot.pwmControl2 = kWinkyPwmControl2;
	snapshot.pwmControl
		= (static_cast<uint32>(kWinkyPwmPeriod) << 16)
			| kWinkyPwmPeriod;
	snapshot.bootFramebufferStatus = B_OK;
	snapshot.bootFramebufferPhysical = kWinkyGmadrBase;
	snapshot.bootFramebufferSize
		= static_cast<uint64>(kWinkyFirmwareStride) * kWinkyFirmwareHeight;
	snapshot.bootWidth = kWinkyFirmwareWidth;
	snapshot.bootHeight = kWinkyFirmwareHeight;
	snapshot.bootDepth = 32;
	snapshot.bootBytesPerRow = kWinkyFirmwareStride;
	snapshot.gmadrBase = kWinkyGmadrBase;
	snapshot.gmadrSize = kWinkyGmadrSize;
	snapshot.gttPte = 0x7c000001;
	snapshot.gttRequiredPages = 768;
	snapshot.gttPresentPages = 768;
	snapshot.gttSignature = kGgttSignatureSeed;
	for (uint32 page = 0; page < snapshot.gttRequiredPages; page++) {
		snapshot.gttSignature = AppendGgttPteSignature(
			snapshot.gttSignature, snapshot.gttPte + page * kPageSize);
	}
	DecodeFirmwareSnapshot(snapshot);
	snapshot.adoptionStatus = B_OK;
	return snapshot;
}


JR_TEST(intel_valleyview_p0, native_layout_is_page_exact)
{
	JR_CHECK_EQ(kP0FramebufferBytes, 0x408000u);
	JR_CHECK_EQ(kP0FramebufferPages, 1032u);
	JR_CHECK_EQ(kP0CursorPages, 4u);
	JR_CHECK_EQ(kP0CursorBytes, 0x4000u);
	JR_CHECK_EQ(kP0PageCount, 1040u);
	JR_CHECK_EQ(kP0AllocationBytes, 0x410000u);
	JR_CHECK_EQ(kP0PrivatePageCount, 8u);
	JR_CHECK_EQ(kP0PrivateBytes, 0x8000u);

	P0Layout layout = {};
	JR_CHECK(BuildP0Layout(256u * 1024 * 1024, 0, 3u * 1024 * 1024,
		layout));
	JR_CHECK_EQ(layout.base, 0x0fbf0000u);
	JR_CHECK_EQ(layout.framebuffer, layout.base);
	JR_CHECK_EQ(layout.cursor, 0x0fff8000u);
	JR_CHECK_EQ(layout.ring, 0x0fffc000u);
	JR_CHECK_EQ(layout.status, 0x0fffd000u);
	JR_CHECK_EQ(layout.testSource, 0x0fffe000u);
	JR_CHECK_EQ(layout.testDestination, 0x0ffff000u);

	uint64 physical = 0;
	JR_CHECK(P0PagePhysical(0x1000000, 0x2000000, 0, physical));
	JR_CHECK_EQ(physical, 0x1000000ull);
	JR_CHECK(P0PagePhysical(0x1000000, 0x2000000,
		kP0FramebufferPages, physical));
	JR_CHECK_EQ(physical, 0x2000000ull);
	JR_CHECK(!P0PagePhysical(0x1000000, 0x2000000, kP0PageCount,
		physical));
}


JR_TEST(intel_valleyview_p0, gates_takeover_on_exact_winky_firmware_state)
{
	FirmwareSnapshot snapshot = WinkyFirmwareSnapshot();
	JR_CHECK(IsWinkyP0TakeoverState(snapshot));

	snapshot.hTotal ^= 1;
	JR_CHECK(!IsWinkyP0TakeoverState(snapshot));
	snapshot = WinkyFirmwareSnapshot();
	snapshot.pipeSource = EncodePipeSource(kP0Width, kP0Height);
	JR_CHECK(!IsWinkyP0TakeoverState(snapshot));
	snapshot = WinkyFirmwareSnapshot();
	snapshot.panelFitterControl = 0;
	JR_CHECK(!IsWinkyP0TakeoverState(snapshot));
	snapshot = WinkyFirmwareSnapshot();
	snapshot.dpC ^= 1;
	JR_CHECK(!IsWinkyP0TakeoverState(snapshot));
	snapshot = WinkyFirmwareSnapshot();
	snapshot.pwmControl2 = 0;
	JR_CHECK(!IsWinkyP0TakeoverState(snapshot));
	snapshot = WinkyFirmwareSnapshot();
	snapshot.adoptionStatus = B_BAD_DATA;
	JR_CHECK(!IsWinkyP0TakeoverState(snapshot));
}


JR_TEST(intel_valleyview_p0, rejects_layout_overlap)
{
	P0Layout layout = {};
	JR_CHECK(!BuildP0Layout(kP0AllocationBytes - 1, 0, 0, layout));
	JR_CHECK(!BuildP0Layout(kP0AllocationBytes, 0, kPageSize, layout));
}


JR_TEST(intel_valleyview_p0, encodes_native_pipe_source)
{
	JR_CHECK_EQ(EncodePipeSource(1366, 768), 0x055502ffu);
	JR_CHECK_EQ(EncodePipeSource(0, 768), 0u);
	JR_CHECK_EQ(EncodePipeSource(1366, 0), 0u);
}


JR_TEST(intel_valleyview_p0, preserves_pwm_period_and_scales_duty)
{
	const uint32 control = 0x1e841234;
	JR_CHECK_EQ(PwmPeriod(control), 0x1e84u);
	JR_CHECK_EQ(PwmDuty(control), 0x1234u);
	JR_CHECK_EQ(SetPwmDuty(control, 0x1000), 0x1e841000u);

	uint16 duty = 0;
	JR_CHECK(BrightnessDuty(7812, 0.5f, duty));
	JR_CHECK_EQ(duty, 3906u);
	JR_CHECK(BrightnessDuty(7812, 1.0f, duty));
	JR_CHECK_EQ(duty, 7812u);
	JR_CHECK(!BrightnessDuty(0, 0.5f, duty));
	JR_CHECK(!BrightnessDuty(7812, -0.1f, duty));
}


JR_TEST(intel_valleyview_p0, encodes_signed_cursor_position)
{
	JR_CHECK_EQ(EncodeCursorPosition(12, 34), 0x0022000cu);
	JR_CHECK_EQ(EncodeCursorPosition(-12, -34), 0x8022800cu);
}
