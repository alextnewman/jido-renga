// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot
#ifndef INTEL_VALLEYVIEW_RENDER_PROTOCOL_H
#define INTEL_VALLEYVIEW_RENDER_PROTOCOL_H

#include <common/intel_valleyview/Protocol.h>
#include <common/intel_valleyview/RcsCore.h>


namespace valleyview {

constexpr uint32 kRenderProtocolMagic = 0x564c5652;
constexpr uint16 kRenderProtocolVersion = 4;

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


constexpr uint32 kRcsDiagnosticArm = 0x52435330;

enum RcsDiagnosticStage : uint32 {
	kRcsStageNone = 0,
	kRcsStageMemoryAllocated,
	kRcsStageGgttBound,
	kRcsStageSnapshot,
	kRcsStageForcewakeAcquired,
	kRcsStageTlbFlushed,
	kRcsStageRingStarted,
	kRcsStageCommandsCompleted,
	kRcsStageOutputVerified,
	kRcsStageRestored
};

enum RcsDiagnosticFlag : uint32 {
	kRcsMemoryAllocated = 1u << 0,
	kRcsGgttBound = 1u << 1,
	kRcsSnapshotCaptured = 1u << 2,
	kRcsRingAvailable = 1u << 3,
	kRcsForcewakeAcquired = 1u << 4,
	kRcsTlbFlushed = 1u << 5,
	kRcsRingStarted = 1u << 6,
	kRcsBatchMarkerVerified = 1u << 7,
	kRcsCompletionVerified = 1u << 8,
	kRcsTimestampVerified = 1u << 9,
	kRcsRingRestored = 1u << 10,
	kRcsGgttRestored = 1u << 11,
	kRcsDisplayUnchanged = 1u << 12,
	kRcsBcsUnchanged = 1u << 13,
	kRcsResetPerformed = 1u << 14,
	kRcsFaultCaptured = 1u << 15,
	kRcsShaderMemoryAllocated = 1u << 16,
	kRcsShaderGgttBound = 1u << 17,
	kRcsShaderCommandsBuilt = 1u << 18,
	kRcsShaderCommandsCompleted = 1u << 19,
	kRcsShaderOutputVerified = 1u << 20,
	kRcsShaderGuardVerified = 1u << 21,
	kRcsShaderCacheRestored = 1u << 22,
	kRcsShaderGgttRestored = 1u << 23
};

enum RcsShaderDiagnosticStage : uint32 {
	kRcsShaderStageNone = 0,
	kRcsShaderStageMemoryAllocated,
	kRcsShaderStageGgttBound,
	kRcsShaderStageCommandsBuilt,
	kRcsShaderStageRingStarted,
	kRcsShaderStageCommandsCompleted,
	kRcsShaderStageOutputVerified,
	kRcsShaderStageRestored
};

struct RcsDiagnostic {
	RenderAbiHeader			header;
	uint32					command;
	int32					status;
	RcsDiagnosticStage		stage;
	uint32					flags;
	uint32					ringOffset;
	uint32					statusOffset;
	uint32					batchOffset;
	uint32					resultOffset;
	uint32					ringTailBytes;
	uint32					batchBytes;
	uint32					batchMarker;
	uint32					completionMarker;
	uint32					observedBatchMarker;
	uint32					observedCompletionMarker;
	uint32					timestampBefore;
	uint32					observedTimestamp;
	uint32					timestampAfter;
	uint32					reserved0;
	uint64					elapsedUs;
	uint64					testCount;
	uint64					failureCount;
	uint64					resetCount;
	uint64					displaySignatureBefore;
	uint64					displaySignatureAfter;
	int32					resetStatus;
	int32					ringRestoreStatus;
	int32					ggttRestoreStatus;
	int32					forcewakeReleaseStatus;
	int32					wakeRestoreStatus;
	RcsShaderDiagnosticStage shaderStage;
	int32					shaderStatus;
	int32					shaderGgttRestoreStatus;
	uint32					shaderOffset;
	uint32					shaderPages;
	uint32					shaderCommandBytes;
	uint32					shaderKernelOffset;
	uint32					shaderSurfaceStateOffset;
	uint32					shaderBindingTableOffset;
	uint32					shaderDescriptorOffset;
	uint32					shaderSurfaceOffset;
	uint32					shaderSurfaceBytes;
	uint32					shaderGuardOffset;
	uint32					shaderGuardBytes;
	uint32					shaderCompletionMarker;
	uint32					shaderZeroDwords;
	uint32					shaderSentinelDwords;
	uint32					shaderUnexpectedDwords;
	uint32					shaderFirstChangedOffset;
	uint32					shaderLastChangedOffset;
	uint32					shaderFirstUnexpectedOffset;
	uint32					shaderFirstUnexpectedValue;
	uint32					shaderGuardMismatchOffset;
	uint32					shaderGuardObserved;
	uint64					shaderChecksumBefore;
	uint64					shaderChecksumAfter;
	uint32					cacheMode0Before;
	uint32					cacheMode0After;
	uint32					cacheMode1Before;
	uint32					cacheMode1After;
	uint32					pteBefore[kRcsTestPageCount];
	uint32					pteBound[kRcsTestPageCount];
	uint32					pteAfter[kRcsTestPageCount];
	uint32					shaderPteBefore[kRcsShaderTotalPages];
	uint32					shaderPteBound[kRcsShaderTotalPages];
	uint32					shaderPteAfter[kRcsShaderTotalPages];
	GpuRegisterSnapshot		globalBefore;
	GpuRegisterSnapshot		globalFault;
	GpuRegisterSnapshot		globalAfter;
	RcsRegisterSnapshot		before;
	RcsRegisterSnapshot		active;
	RcsRegisterSnapshot		fault;
	RcsRegisterSnapshot		after;
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
