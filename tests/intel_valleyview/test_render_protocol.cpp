// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot

#include "framework/jr_test.h"

#include <common/intel_valleyview/RenderProtocol.h>


using namespace valleyview;


JR_TEST(intel_valleyview, validates_the_render_discovery_abi)
{
	JR_CHECK_EQ(kRenderProtocolMagic, 0x564c5652u);
	JR_CHECK_EQ(kRenderProtocolVersion, 5u);
	JR_CHECK(sizeof(RenderDeviceInfo) < UINT16_MAX);
	JR_CHECK(sizeof(RenderBufferCreate) < UINT16_MAX);
	JR_CHECK(sizeof(RenderContextCreate) < UINT16_MAX);
	JR_CHECK(sizeof(RenderContextDestroy) < UINT16_MAX);
	JR_CHECK(sizeof(RenderBufferMap) < UINT16_MAX);
	JR_CHECK(sizeof(RenderMemoryTest) < UINT16_MAX);
	JR_CHECK(sizeof(RcsDiagnostic) < UINT16_MAX);
	JR_CHECK_EQ(kRcsDiagnosticArm, 0x52435330u);
	JR_CHECK_EQ((uint32)kRcsStageRestored, 9u);
	JR_CHECK_EQ((uint32)kRcsShaderStageRestored, 7u);
	JR_CHECK_EQ(kRcsShaderTotalPages, 19u);
	JR_CHECK_EQ(sizeof(RenderBufferCreate), 48u);
	JR_CHECK_EQ(sizeof(RenderContextCreate), 40u);
	JR_CHECK_EQ(sizeof(RenderContextDestroy), 24u);

	const RenderAbiHeader valid = MakeRenderAbiHeader(
		sizeof(RenderDeviceInfo));
	JR_CHECK(IsValidRenderAbiHeader(valid, sizeof(RenderDeviceInfo)));

	RenderAbiHeader invalid = valid;
	invalid.magic ^= 1;
	JR_CHECK(!IsValidRenderAbiHeader(invalid, sizeof(RenderDeviceInfo)));
	invalid = valid;
	invalid.version++;
	JR_CHECK(!IsValidRenderAbiHeader(invalid, sizeof(RenderDeviceInfo)));
	invalid = valid;
	invalid.size--;
	JR_CHECK(!IsValidRenderAbiHeader(invalid, sizeof(RenderDeviceInfo)));
}


JR_TEST(intel_valleyview, requires_the_complete_safe_render_contract)
{
	RenderDeviceInfo info = {};
	info.header = MakeRenderAbiHeader(sizeof(info));
	info.status = 0;
	info.capabilities = kRenderRequiredCapabilities;
	info.submissionEngines = kRenderEngineRcs;
	JR_CHECK((kRenderRequiredCapabilities
		& kRenderCapabilityCacheDomains) != 0);
	JR_CHECK((kRenderRequiredCapabilities
		& kRenderCapabilityTiledBuffers) != 0);
	JR_CHECK((kRenderRequiredCapabilities
		& kRenderCapabilityPpgtt) != 0);
	JR_CHECK_NE(kRenderCapabilityPpgtt,
		kRenderCapabilityRenderContexts);
	JR_CHECK(IsRenderReady(info));

	info.capabilities &= ~kRenderCapabilityCommandIsolation;
	JR_CHECK(!IsRenderReady(info));
	info.capabilities = kRenderRequiredCapabilities;
	info.submissionEngines = kRenderEngineBcs;
	JR_CHECK(!IsRenderReady(info));
	info.submissionEngines = kRenderEngineRcs;
	info.status = -1;
	JR_CHECK(!IsRenderReady(info));
}


JR_TEST(intel_valleyview, separates_proven_and_submittable_engines)
{
	RenderDeviceInfo info = {};
	info.provenEngines = kRenderEngineBcs;
	info.submissionEngines = 0;
	JR_CHECK((info.provenEngines & kRenderEngineBcs) != 0);
	JR_CHECK((info.submissionEngines & kRenderEngineBcs) == 0);
	JR_CHECK_NE(kRenderDeviceGgtt, kRenderDeviceDisplayReserved);
}
