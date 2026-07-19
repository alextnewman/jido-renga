// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot
#ifndef INTEL_VALLEYVIEW_RENDER_PROTOCOL_H
#define INTEL_VALLEYVIEW_RENDER_PROTOCOL_H

#include <SupportDefs.h>


namespace valleyview {

constexpr uint32 kRenderProtocolMagic = 0x564c5652;
constexpr uint16 kRenderProtocolVersion = 2;

enum RenderDeviceFlag : uint32 {
	kRenderDeviceGgtt = 1u << 0,
	kRenderDeviceNoLlc = 1u << 1,
	kRenderDeviceDisplayReserved = 1u << 2
};

enum RenderEngine : uint32 {
	kRenderEngineRcs = 1u << 0,
	kRenderEngineBcs = 1u << 1,
	kRenderEngineVcs = 1u << 2
};

enum RenderCapability : uint64 {
	kRenderCapabilityDeviceInfo = 1ull << 0,
	kRenderCapabilityBufferObjects = 1ull << 1,
	kRenderCapabilityCpuMappings = 1ull << 2,
	kRenderCapabilityGpuAddressSpaces = 1ull << 3,
	kRenderCapabilityCacheDomains = 1ull << 4,
	kRenderCapabilityTiledBuffers = 1ull << 5,
	kRenderCapabilityRenderContexts = 1ull << 6,
	kRenderCapabilityRcsSubmission = 1ull << 7,
	kRenderCapabilityCompletionFences = 1ull << 8,
	kRenderCapabilityCommandIsolation = 1ull << 9,
	kRenderCapabilityResetRecovery = 1ull << 10,
	kRenderCapabilityDrawablePresent = 1ull << 11
};

enum RenderBufferFlag : uint32 {
	kRenderBufferCpuCached = 1u << 0
};

constexpr uint32 kRenderSupportedBufferFlags = kRenderBufferCpuCached;

enum RenderBufferDomain : uint32 {
	kRenderDomainCpu = 1,
	kRenderDomainBcs = 2
};

enum RenderMemoryTestStage : uint32 {
	kRenderMemoryTestNone = 0,
	kRenderMemoryTestInputsVerified,
	kRenderMemoryTestCommandsCompleted,
	kRenderMemoryTestOutputVerified
};

constexpr uint64 kRenderRequiredCapabilities
	= kRenderCapabilityBufferObjects
		| kRenderCapabilityCpuMappings
		| kRenderCapabilityGpuAddressSpaces
		| kRenderCapabilityCacheDomains
		| kRenderCapabilityTiledBuffers
		| kRenderCapabilityRenderContexts
		| kRenderCapabilityRcsSubmission
		| kRenderCapabilityCompletionFences
		| kRenderCapabilityCommandIsolation
		| kRenderCapabilityResetRecovery
		| kRenderCapabilityDrawablePresent;

struct RenderAbiHeader {
	uint32	magic;
	uint16	version;
	uint16	size;
};

struct RenderDeviceInfo {
	RenderAbiHeader	header;
	int32			status;
	uint16			vendorId;
	uint16			deviceId;
	uint8			revision;
	uint8			graphicsGeneration;
	uint8			gpuAddressBits;
	uint8			reserved0;
	uint32			pageSize;
	uint32			deviceFlags;
	uint32			provenEngines;
	uint32			submissionEngines;
	uint64			capabilities;
	uint64			apertureBase;
	uint64			apertureSize;
	uint64			displayReservedOffset;
	uint64			displayReservedSize;
	uint64			reserved[4];
};

struct RenderBufferCreate {
	RenderAbiHeader	header;
	uint64			requestedSize;
	uint64			size;
	uint64			gpuOffset;
	uint32			flags;
	uint32			handle;
};

struct RenderBufferMap {
	RenderAbiHeader	header;
	uint32			handle;
	uint32			flags;
	int32			area;
	uint32			reserved;
	uint64			address;
	uint64			size;
};

struct RenderBufferClose {
	RenderAbiHeader	header;
	uint32			handle;
	uint32			reserved;
};

struct RenderBufferSetDomain {
	RenderAbiHeader	header;
	uint32			handle;
	RenderBufferDomain domain;
	RenderBufferDomain previousDomain;
	uint32			reserved;
};

struct RenderMemoryTest {
	RenderAbiHeader		header;
	uint32				sourceHandle;
	uint32				destinationHandle;
	uint32				seed;
	RenderMemoryTestStage	stage;
	int32					status;
	uint32				completionMarker;
	uint32				mismatchOffset;
	uint32				observed;
	uint64				elapsedUs;
};


inline RenderAbiHeader
MakeRenderAbiHeader(uint16 size)
{
	RenderAbiHeader header = {
		kRenderProtocolMagic, kRenderProtocolVersion, size
	};
	return header;
}


inline bool
IsValidRenderAbiHeader(const RenderAbiHeader& header, uint16 expectedSize)
{
	return header.magic == kRenderProtocolMagic
		&& header.version == kRenderProtocolVersion
		&& header.size == expectedSize;
}


inline bool
IsRenderReady(const RenderDeviceInfo& info)
{
	return IsValidRenderAbiHeader(info.header, sizeof(info))
		&& info.status == 0
		&& (info.capabilities & kRenderRequiredCapabilities)
			== kRenderRequiredCapabilities
		&& (info.submissionEngines & kRenderEngineRcs) != 0;
}

} // namespace valleyview

#endif
