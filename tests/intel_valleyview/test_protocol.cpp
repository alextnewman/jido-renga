// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot

#include "framework/jr_test.h"

#include <common/intel_valleyview/Protocol.h>


using namespace valleyview;


JR_TEST(intel_valleyview, matches_only_the_winky_gpu)
{
	JR_CHECK(IsSupportedDevice(kIntelVendorId, kWinkyDeviceId));
	JR_CHECK(!IsSupportedDevice(0x1234, kWinkyDeviceId));
	JR_CHECK(!IsSupportedDevice(kIntelVendorId, 0x0f30));
	JR_CHECK(!IsSupportedDevice(kIntelVendorId, 0x0f32));
	JR_CHECK(!IsSupportedDevice(kIntelVendorId, 0x0f33));
}


JR_TEST(intel_valleyview, defaults_to_the_safe_disabled_policy)
{
	JR_CHECK(kDefaultEnabled);
	JR_CHECK(kDefaultAllowModeset);
	JR_CHECK(kDevicePublicationReady);
	JR_CHECK_EQ((uint32)kDisplayUnavailable, 0u);
}


JR_TEST(intel_valleyview, validates_versioned_abi_headers_exactly)
{
	const AbiHeader valid = MakeAbiHeader(sizeof(DriverStatus));
	JR_CHECK(IsValidAbiHeader(valid, sizeof(DriverStatus)));

	AbiHeader invalid = valid;
	invalid.magic ^= 1;
	JR_CHECK(!IsValidAbiHeader(invalid, sizeof(DriverStatus)));

	invalid = valid;
	invalid.version++;
	JR_CHECK(!IsValidAbiHeader(invalid, sizeof(DriverStatus)));

	invalid = valid;
	invalid.size--;
	JR_CHECK(!IsValidAbiHeader(invalid, sizeof(DriverStatus)));
	JR_CHECK(!IsValidAbiHeader(valid, sizeof(DriverStatus) - 1));
}


JR_TEST(intel_valleyview, assigns_stable_private_operations)
{
	JR_CHECK_EQ(kGetDeviceName, 10000);
	JR_CHECK_EQ(kGetDriverStatus, 10001);
	JR_CHECK_EQ(kGetDeviceIdentity, 10002);
	JR_CHECK_EQ(kGetFirmwareSnapshot, 10003);
	JR_CHECK_EQ(kGetSharedInfo, 10004);
	JR_CHECK_EQ(kCloneFramebuffer, 10005);
	JR_CHECK_EQ(kPublishGraphics, 10006);
	JR_CHECK_EQ(kGetGpuDiagnostics, 10007);
	JR_CHECK_EQ(kRunGpuSelfTest, 10008);
	JR_CHECK_EQ(kGetP0Status, 10009);
	JR_CHECK_EQ(kGetBrightness, 10010);
	JR_CHECK_EQ(kSetBrightness, 10011);
	JR_CHECK_EQ(kGetDpms, 10012);
	JR_CHECK_EQ(kSetDpms, 10013);
	JR_CHECK_EQ(kSetCursorShape, 10014);
	JR_CHECK_EQ(kMoveCursor, 10015);
	JR_CHECK_EQ(kShowCursor, 10016);
	JR_CHECK_EQ(kBcsFill, 10017);
	JR_CHECK_EQ(kBcsBlit, 10018);
	JR_CHECK_EQ(kRunP0SelfTest, 10019);
	JR_CHECK_NE(kGetDeviceName, kGetDriverStatus);
	JR_CHECK_NE(kGetDriverStatus, kGetDeviceIdentity);
}


JR_TEST(intel_valleyview, validates_gpu_diagnostic_abi)
{
	GpuDiagnostics diagnostics = {};
	diagnostics.header = MakeAbiHeader(sizeof(diagnostics));
	diagnostics.command = kGpuSelfTestArm;
	JR_CHECK(IsValidAbiHeader(diagnostics.header, sizeof(diagnostics)));
	JR_CHECK_EQ(diagnostics.command, 0x42435330u);
	JR_CHECK_EQ((uint32)kGpuStageRestored, 8u);
}
