// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot
#ifndef INTEL_VALLEYVIEW_PROTOCOL_H
#define INTEL_VALLEYVIEW_PROTOCOL_H

#include <SupportDefs.h>


namespace valleyview {

constexpr uint16 kIntelVendorId = 0x8086;
constexpr uint16 kWinkyDeviceId = 0x0f31;

constexpr uint32 kProtocolMagic = 0x564c5657;
constexpr uint16 kProtocolVersion = 6;

constexpr bool kDefaultEnabled = false;
constexpr bool kDefaultAllowModeset = false;
constexpr bool kDevicePublicationReady = true;

enum {
	kGetDeviceName = 10000,
	kGetDriverStatus,
	kGetDeviceIdentity,
	kGetFirmwareSnapshot,
	kGetSharedInfo,
	kCloneFramebuffer,
	kPublishGraphics,
	kGetGpuDiagnostics,
	kRunGpuSelfTest
};

enum DisplayState : uint32 {
	kDisplayUnavailable = 0,
	kFirmwareAdopted,
	kActive,
	kSoftBlankedAdopted,
	kSoftBlankedNative,
	kPanelOff,
	kFaulted
};

enum Capability : uint32 {
	kCapabilityFirmwareAdoption = 1u << 0,
	kCapabilityModeset = 1u << 1,
	kCapabilityBacklight = 1u << 2,
	kCapabilityCursor = 1u << 3,
	kCapabilityDpms = 1u << 4,
	kCapabilityVblank = 1u << 5,
	kCapabilityGpuDiagnostics = 1u << 6,
	kCapabilityBcsSelfTest = 1u << 7
};

enum FirmwareSnapshotFlag : uint32 {
	kSnapshotMmioMapped = 1u << 0,
	kSnapshotOpRegionValid = 1u << 1,
	kSnapshotVbtPresent = 1u << 2,
	kSnapshotDpllEnabled = 1u << 3,
	kSnapshotDpllLocked = 1u << 4,
	kSnapshotPipeEnabled = 1u << 5,
	kSnapshotPlaneEnabled = 1u << 6,
	kSnapshotPortEnabled = 1u << 7,
	kSnapshotPpsOn = 1u << 8,
	kSnapshotPpsReady = 1u << 9,
	kSnapshotPwmEnabled = 1u << 10,
	kSnapshotCursorEnabled = 1u << 11,
	kSnapshotPanelFitterEnabled = 1u << 12,
	kSnapshotBootFramebuffer = 1u << 13,
	kSnapshotAdoptionCompatible = 1u << 14,
	kSnapshotScanoutMatchesBoot = 1u << 15,
	kSnapshotGttRangePresent = 1u << 16
};

enum VbtSource : uint8 {
	kVbtNone = 0,
	kVbtInline,
	kVbtRvda
};

struct AbiHeader {
	uint32	magic;
	uint16	version;
	uint16	size;
};

struct DriverStatus {
	AbiHeader	header;
	uint32		capabilities;
	DisplayState	displayState;
	uint8		enabled;
	uint8		allowModeset;
	uint8		reserved[6];
};

struct DeviceIdentity {
	AbiHeader	header;
	uint16		vendorId;
	uint16		deviceId;
	uint16		subsystemVendorId;
	uint16		subsystemId;
	uint8		revision;
	uint8		bus;
	uint8		device;
	uint8		function;
	uint8		reserved[4];
};

struct FirmwareSnapshot {
	AbiHeader	header;
	uint32		generation;
	uint32		flags;
	int32		mmioStatus;
	int32		opRegionStatus;
	int32		bootFramebufferStatus;
	int32		adoptionStatus;
	uint64		mmioPhysical;
	uint64		mmioSize;
	uint64		gmadrBase;
	uint64		gmadrSize;
	uint32		asls;
	uint32		opRegionMboxes;
	uint16		opRegionSizeKiB;
	uint8		opRegionMajor;
	uint8		opRegionMinor;
	uint64		vbtAddress;
	uint32		vbtSize;
	VbtSource	vbtSource;
	uint8		opRegionRevision;
	uint8		opRegionReserved[2];

	uint32		dpllA;
	uint32		hTotal;
	uint32		hBlank;
	uint32		hSync;
	uint32		vTotal;
	uint32		vBlank;
	uint32		vSync;
	uint32		pipeSource;
	uint32		pipeConfig;
	uint32		planeControl;
	uint32		planeAddressVlv;
	uint32		planeLinearOffset;
	uint32		planeStride;
	uint32		planeSurface;
	uint32		planeSurfaceLive;
	uint32		planeTileOffset;
	uint32		planeGgttOffset;
	uint32		gttPte;
	uint64		scanoutPhysical;
	uint64		scanoutAperture;
	uint32		gttRequiredPages;
	uint32		gttPresentPages;
	uint32		panelFitterControl;
	uint32		panelFitterProgrammedRatios;
	uint32		panelFitterAutoRatios;
	uint32		dpC;
	uint32		ppsStatus;
	uint32		ppsControl;
	uint32		ppsOnDelays;
	uint32		ppsOffDelays;
	uint32		ppsDivisor;
	uint32		pwmControl2;
	uint32		pwmControl;
	uint32		cursorControl;
	uint32		cursorBase;
	uint32		cursorPosition;
	uint64		bootFramebufferPhysical;
	uint64		bootFramebufferSize;
	int32		bootFramebufferArea;
	uint32		bootWidth;
	uint32		bootHeight;
	uint32		bootDepth;
	uint32		bootBytesPerRow;

	uint16		hDisplay;
	uint16		hTotalPixels;
	uint16		vDisplay;
	uint16		vTotalLines;
	uint16		sourceWidth;
	uint16		sourceHeight;
	uint16		pwmPeriod;
	uint16		pwmDuty;
	uint8		ppsPort;
	uint8		dpPipe;
	uint8		reserved[6];
};


constexpr uint32 kGpuSelfTestArm = 0x42435330;

enum GpuSelfTestStage : uint32 {
	kGpuStageNone = 0,
	kGpuStageSnapshot,
	kGpuStageMemoryAllocated,
	kGpuStageGgttInstalled,
	kGpuStageForcewakeAcquired,
	kGpuStageRingStarted,
	kGpuStageCommandsCompleted,
	kGpuStageBuffersVerified,
	kGpuStageRestored
};

enum GpuDiagnosticFlag : uint32 {
	kGpuSnapshotCaptured = 1u << 0,
	kGpuScratchRangeUniform = 1u << 1,
	kGpuMemoryAllocated = 1u << 2,
	kGpuGgttInstalled = 1u << 3,
	kGpuGtWakeAllowed = 1u << 4,
	kGpuRenderForcewake = 1u << 5,
	kGpuMediaForcewake = 1u << 6,
	kGpuRingAvailable = 1u << 7,
	kGpuRingStarted = 1u << 8,
	kGpuCommandsCompleted = 1u << 9,
	kGpuFillVerified = 1u << 10,
	kGpuCopyVerified = 1u << 11,
	kGpuDisplayUnchanged = 1u << 12,
	kGpuGgttRestored = 1u << 13,
	kGpuRingRestored = 1u << 14,
	kGpuBcsReset = 1u << 15
};

struct GpuRegisterSnapshot {
	uint32	gtlcWakeControl;
	uint32	gtlcPowerStatus;
	uint32	forcewakeRender;
	uint32	forcewakeAckRender;
	uint32	forcewakeMedia;
	uint32	forcewakeAckMedia;
	uint32	gtFifoControl;
	uint32	gtFifoDebug;
	uint32	gtThreadStatus;
	uint32	renderC0Count;
	uint32	mediaC0Count;
	uint32	gdrst;
	uint32	bcsTail;
	uint32	bcsHead;
	uint32	bcsStart;
	uint32	bcsControl;
	uint32	bcsHws;
	uint32	bcsMiMode;
	uint32	bcsMode;
	uint32	bcsActhd;
	uint32	bcsIpehr;
	uint32	bcsIpeir;
	uint32	bcsInstdone;
};

struct GpuDiagnostics {
	AbiHeader				header;
	uint32					command;
	uint32					generation;
	uint32					flags;
	int32					status;
	GpuSelfTestStage		stage;
	uint32					ggttOffset;
	uint32					ringTailBytes;
	uint64					testPhysical;
	uint64					displaySignatureBefore;
	uint64					displaySignatureAfter;
	uint32					pteBefore[4];
	uint32					pteTest[4];
	uint32					pteAfter[4];
	uint32					expectedPattern;
	uint32					completionMarker;
	uint32					sourceMismatchOffset;
	uint32					sourceObserved;
	uint32					destinationMismatchOffset;
	uint32					destinationObserved;
	uint32					elapsedUs;
	uint32					reserved;
	GpuRegisterSnapshot	before;
	GpuRegisterSnapshot	active;
	GpuRegisterSnapshot	after;
};


inline AbiHeader
MakeAbiHeader(uint16 size)
{
	AbiHeader header = {kProtocolMagic, kProtocolVersion, size};
	return header;
}


inline bool
IsValidAbiHeader(const AbiHeader& header, uint16 expectedSize)
{
	return header.magic == kProtocolMagic
		&& header.version == kProtocolVersion
		&& header.size == expectedSize;
}


inline bool
IsSupportedDevice(uint16 vendorId, uint16 deviceId)
{
	return vendorId == kIntelVendorId && deviceId == kWinkyDeviceId;
}

} // namespace valleyview

#endif
