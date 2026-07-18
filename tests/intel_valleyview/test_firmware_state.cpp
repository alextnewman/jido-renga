// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot

#include "framework/jr_test.h"

#include <common/intel_valleyview/FirmwareState.h>


using namespace valleyview;


JR_TEST(intel_valleyview_firmware, register_map_stays_within_valleyview_bar)
{
	JR_CHECK_EQ(kVlvDisplayBase, 0x180000u);
	JR_CHECK_EQ(kDpllA, 0x186014u);
	JR_CHECK_EQ(kHTotalA, 0x1e0000u);
	JR_CHECK_EQ(kDpC, 0x1e4200u);
	JR_CHECK_EQ(kPpsStatusA, 0x1e1200u);
	JR_CHECK_EQ(kPwmControlA, 0x1e1254u);
	JR_CHECK_EQ(kPipeConfigA, 0x1f0008u);
	JR_CHECK_EQ(kPlaneAddressVlvA, 0x1f017cu);
	JR_CHECK_EQ(kPlaneSurfaceLiveA, 0x1f01acu);
	JR_CHECK_EQ(kPanelFitterControl, 0x1e1230u);
	JR_CHECK_EQ(kGttOffsetInBar, 0x200000u);
	JR_CHECK_EQ(kOpRegionMboxesOffset, 88u);
	JR_CHECK_EQ(kAsleRvdaOffset, 186u);
	JR_CHECK_EQ(kAsleRvdsOffset, 194u);
	JR_CHECK(kLastSnapshotRegister + sizeof(uint32) <= 0x200000u);
}


JR_TEST(intel_valleyview_firmware, decodes_firmware_lit_state)
{
	FirmwareSnapshot snapshot = {};
	snapshot.dpllA = kDpllVcoEnable | kDpllLock;
	snapshot.hTotal = (1439u << 16) | 1365u;
	snapshot.vTotal = (799u << 16) | 767u;
	snapshot.pipeSource = (1365u << 16) | 767u;
	snapshot.pipeConfig = kPipeEnable;
	snapshot.planeControl = kPlaneEnable;
	snapshot.planeStride = 5504;
	snapshot.dpC = kDpPortEnable;
	snapshot.ppsStatus = kPpsOn | kPpsReady;
	snapshot.ppsOnDelays = 2u << 30;
	snapshot.pwmControl2 = kPwmEnable;
	snapshot.pwmControl = (1000u << 17) | 1000u;
	snapshot.cursorControl = kCursorEnable;

	DecodeFirmwareSnapshot(snapshot);

	JR_CHECK_EQ(snapshot.hDisplay, 1366);
	JR_CHECK_EQ(snapshot.hTotalPixels, 1440);
	JR_CHECK_EQ(snapshot.vDisplay, 768);
	JR_CHECK_EQ(snapshot.vTotalLines, 800);
	JR_CHECK_EQ(snapshot.sourceWidth, 1366);
	JR_CHECK_EQ(snapshot.sourceHeight, 768);
	JR_CHECK_EQ(snapshot.pwmPeriod, 2000);
	JR_CHECK_EQ(snapshot.pwmDuty, 1000);
	JR_CHECK_EQ(snapshot.ppsPort, 2);
	JR_CHECK((snapshot.flags & kSnapshotDpllEnabled) != 0);
	JR_CHECK((snapshot.flags & kSnapshotDpllLocked) != 0);
	JR_CHECK((snapshot.flags & kSnapshotPipeEnabled) != 0);
	JR_CHECK((snapshot.flags & kSnapshotPlaneEnabled) != 0);
	JR_CHECK((snapshot.flags & kSnapshotPortEnabled) != 0);
	JR_CHECK((snapshot.flags & kSnapshotPpsOn) != 0);
	JR_CHECK((snapshot.flags & kSnapshotPpsReady) != 0);
	JR_CHECK((snapshot.flags & kSnapshotPwmEnabled) != 0);
	JR_CHECK((snapshot.flags & kSnapshotCursorEnabled) != 0);
}


JR_TEST(intel_valleyview_firmware, requires_exact_linear_boot_scanout_for_adoption)
{
	FirmwareSnapshot snapshot = {};
	snapshot.flags = kSnapshotMmioMapped;
	snapshot.dpllA = kDpllVcoEnable | kDpllLock;
	snapshot.pipeSource = (1023u << 16) | 767u;
	snapshot.pipeConfig = kPipeEnable;
	snapshot.planeControl = kPlaneEnable | kPlaneFormatBgrx888;
	snapshot.planeStride = 4096;
	snapshot.dpC = kDpPortEnable;
	snapshot.ppsStatus = kPpsOn | kPpsReady;
	snapshot.bootFramebufferStatus = B_OK;
	snapshot.bootFramebufferPhysical = 0x80000000;
	snapshot.bootFramebufferSize = 4096u * 768;
	snapshot.bootWidth = 1024;
	snapshot.bootHeight = 768;
	snapshot.bootDepth = 32;
	snapshot.bootBytesPerRow = 4096;
	snapshot.gmadrBase = 0x80000000;
	snapshot.gmadrSize = 256u * 1024 * 1024;
	snapshot.gttPte = 0x70000000u | kGen7PtePresent;
	snapshot.gttRequiredPages = 768;
	snapshot.gttPresentPages = 768;

	DecodeFirmwareSnapshot(snapshot);
	JR_CHECK_EQ(snapshot.scanoutAperture, 0x80000000ull);
	JR_CHECK_EQ(snapshot.scanoutPhysical, 0x70000000ull);
	JR_CHECK((snapshot.flags & kSnapshotScanoutMatchesBoot) != 0);
	JR_CHECK((snapshot.flags & kSnapshotGttRangePresent) != 0);
	JR_CHECK((snapshot.flags & kSnapshotAdoptionCompatible) != 0);

	// PTE backing is a distinct address domain and need not equal GMADR.
	snapshot.gttPte = 0x60000000u | kGen7PtePresent;
	DecodeFirmwareSnapshot(snapshot);
	JR_CHECK((snapshot.flags & kSnapshotAdoptionCompatible) != 0);

	// A different aperture address must fail the gate closed.
	snapshot.bootFramebufferPhysical = 0x81000000;
	DecodeFirmwareSnapshot(snapshot);
	JR_CHECK((snapshot.flags & kSnapshotScanoutMatchesBoot) == 0);
	JR_CHECK((snapshot.flags & kSnapshotAdoptionCompatible) == 0);

	snapshot.bootFramebufferPhysical = 0x80000000;
	snapshot.gttPresentPages = 767;
	DecodeFirmwareSnapshot(snapshot);
	JR_CHECK((snapshot.flags & kSnapshotGttRangePresent) == 0);
	JR_CHECK((snapshot.flags & kSnapshotAdoptionCompatible) == 0);

	snapshot.gttPresentPages = 768;
	snapshot.bootBytesPerRow = 4092;
	DecodeFirmwareSnapshot(snapshot);
	JR_CHECK((snapshot.flags & kSnapshotAdoptionCompatible) == 0);

	snapshot.bootBytesPerRow = 4096;
	snapshot.planeControl |= kPlaneTiled;
	DecodeFirmwareSnapshot(snapshot);
	JR_CHECK((snapshot.flags & kSnapshotAdoptionCompatible) == 0);

	snapshot.planeControl &= ~kPlaneTiled;
	snapshot.bootFramebufferPhysical = 0x8ff00000;
	snapshot.bootFramebufferSize = 2u * 1024 * 1024;
	DecodeFirmwareSnapshot(snapshot);
	JR_CHECK((snapshot.flags & kSnapshotScanoutMatchesBoot) == 0);
}


JR_TEST(intel_valleyview_firmware, resolves_nonzero_ggtt_offset_into_aperture)
{
	// A firmware scanout parked at a nonzero GGTT offset must resolve to
	// GMADR base plus that offset (plus any linear offset) in the aperture
	// domain, independent of the unrelated PTE backing page.
	FirmwareSnapshot snapshot = {};
	snapshot.flags = kSnapshotMmioMapped;
	snapshot.dpllA = kDpllVcoEnable | kDpllLock;
	snapshot.pipeSource = (1023u << 16) | 767u;
	snapshot.pipeConfig = kPipeEnable;
	snapshot.planeControl = kPlaneEnable | kPlaneFormatBgrx888;
	snapshot.planeStride = 4096;
	// planeGgttOffset is what DecodeFirmwareSnapshot actually consumes; the
	// driver derives it from planeSurface (masked to a page boundary) before
	// calling this pure function, so the test supplies it pre-derived too.
	snapshot.planeGgttOffset = 0x00200000;
	snapshot.planeLinearOffset = 0;
	snapshot.dpC = kDpPortEnable;
	snapshot.ppsStatus = kPpsOn | kPpsReady;
	snapshot.bootFramebufferStatus = B_OK;
	snapshot.bootFramebufferPhysical = 0xd0200000;
	snapshot.bootFramebufferSize = 4096u * 768;
	snapshot.bootWidth = 1024;
	snapshot.bootHeight = 768;
	snapshot.bootDepth = 32;
	snapshot.bootBytesPerRow = 4096;
	snapshot.gmadrBase = 0xd0000000;
	snapshot.gmadrSize = 256u * 1024 * 1024;
	snapshot.gttRequiredPages = 768;
	snapshot.gttPresentPages = 768;

	DecodeFirmwareSnapshot(snapshot);
	JR_CHECK_EQ(snapshot.scanoutAperture, 0xd0200000ull);
	JR_CHECK((snapshot.flags & kSnapshotScanoutMatchesBoot) != 0);
	JR_CHECK((snapshot.flags & kSnapshotAdoptionCompatible) != 0);
}


JR_TEST(intel_valleyview_firmware, validates_opregion_and_finds_aligned_vbt)
{
	uint8 bytes[kOpRegionSize] = {};
	const char signature[] = "IntelGraphicsMem";
	for (size_t index = 0; index < 16; index++)
		bytes[index] = signature[index];
	uint8* vbt = bytes + kOpRegionVbtOffset + 12;
	vbt[0] = '$';
	vbt[1] = 'V';
	vbt[2] = 'B';
	vbt[3] = 'T';
	vbt[22] = kVbtHeaderSize;
	vbt[24] = 80;
	vbt[28] = kVbtHeaderSize;
	const char bdbSignature[] = "BIOS_DATA_BLOCK ";
	for (size_t index = 0; index < 16; index++)
		vbt[kVbtHeaderSize + index] = bdbSignature[index];
	vbt[kVbtHeaderSize + 18] = kBdbHeaderSize;
	vbt[kVbtHeaderSize + 20] = 32;

	JR_CHECK(HasSignature(bytes, sizeof(bytes), signature, 16));
	uint32 offset = 0;
	JR_CHECK(FindVbt(bytes + kOpRegionVbtOffset,
		sizeof(bytes) - kOpRegionVbtOffset, offset));
	JR_CHECK_EQ(offset, 12u);
	uint32 declaredSize = 0;
	JR_CHECK(ValidateVbt(vbt,
		sizeof(bytes) - kOpRegionVbtOffset - offset, declaredSize));
	JR_CHECK_EQ(declaredSize, 80u);

	bytes[0] ^= 1;
	JR_CHECK(!HasSignature(bytes, sizeof(bytes), signature, 16));
}


JR_TEST(intel_valleyview_firmware, rejects_truncated_or_overflowing_vbt)
{
	uint8 vbt[80] = {};
	vbt[0] = '$';
	vbt[1] = 'V';
	vbt[2] = 'B';
	vbt[3] = 'T';
	vbt[22] = kVbtHeaderSize;
	vbt[24] = sizeof(vbt);
	vbt[28] = kVbtHeaderSize;
	const char bdbSignature[] = "BIOS_DATA_BLOCK ";
	for (size_t index = 0; index < 16; index++)
		vbt[kVbtHeaderSize + index] = bdbSignature[index];
	vbt[kVbtHeaderSize + 18] = kBdbHeaderSize;
	vbt[kVbtHeaderSize + 20] = 32;

	uint32 declaredSize = 0;
	JR_CHECK(!ValidateVbt(vbt, kVbtHeaderSize - 1, declaredSize));
	vbt[24] = sizeof(vbt) + 1;
	JR_CHECK(!ValidateVbt(vbt, sizeof(vbt), declaredSize));
	vbt[24] = sizeof(vbt);
	vbt[28] = sizeof(vbt) - kBdbHeaderSize + 1;
	JR_CHECK(!ValidateVbt(vbt, sizeof(vbt), declaredSize));
}
