// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot
#ifndef INTEL_VALLEYVIEW_FIRMWARE_STATE_H
#define INTEL_VALLEYVIEW_FIRMWARE_STATE_H

#include <common/intel_valleyview/Protocol.h>

#include <stddef.h>


namespace valleyview {

constexpr uint32 kVlvDisplayBase = 0x180000;

constexpr uint32 kDpllA = kVlvDisplayBase + 0x6014;
constexpr uint32 kHTotalA = kVlvDisplayBase + 0x60000;
constexpr uint32 kHBlankA = kVlvDisplayBase + 0x60004;
constexpr uint32 kHSyncA = kVlvDisplayBase + 0x60008;
constexpr uint32 kVTotalA = kVlvDisplayBase + 0x6000c;
constexpr uint32 kVBlankA = kVlvDisplayBase + 0x60010;
constexpr uint32 kVSyncA = kVlvDisplayBase + 0x60014;
constexpr uint32 kPipeSourceA = kVlvDisplayBase + 0x6001c;
constexpr uint32 kPipeConfigA = kVlvDisplayBase + 0x70008;
constexpr uint32 kPlaneAddressVlvA = kVlvDisplayBase + 0x7017c;
constexpr uint32 kPlaneControlA = kVlvDisplayBase + 0x70180;
constexpr uint32 kPlaneLinearOffsetA = kVlvDisplayBase + 0x70184;
constexpr uint32 kPlaneStrideA = kVlvDisplayBase + 0x70188;
constexpr uint32 kPlaneSurfaceA = kVlvDisplayBase + 0x7019c;
constexpr uint32 kPlaneTileOffsetA = kVlvDisplayBase + 0x701a4;
constexpr uint32 kPlaneSurfaceLiveA = kVlvDisplayBase + 0x701ac;
constexpr uint32 kPanelFitterControl = kVlvDisplayBase + 0x61230;
constexpr uint32 kPanelFitterProgrammedRatios = kVlvDisplayBase + 0x61234;
constexpr uint32 kPanelFitterAutoRatios = kVlvDisplayBase + 0x61238;
constexpr uint32 kDpC = kVlvDisplayBase + 0x64200;
constexpr uint32 kPpsStatusA = kVlvDisplayBase + 0x61200;
constexpr uint32 kPpsControlA = kVlvDisplayBase + 0x61204;
constexpr uint32 kPpsOnDelaysA = kVlvDisplayBase + 0x61208;
constexpr uint32 kPpsOffDelaysA = kVlvDisplayBase + 0x6120c;
constexpr uint32 kPpsDivisorA = kVlvDisplayBase + 0x61210;
constexpr uint32 kPwmControl2A = kVlvDisplayBase + 0x61250;
constexpr uint32 kPwmControlA = kVlvDisplayBase + 0x61254;
constexpr uint32 kCursorControlA = kVlvDisplayBase + 0x70080;
constexpr uint32 kCursorBaseA = kVlvDisplayBase + 0x70084;
constexpr uint32 kCursorPositionA = kVlvDisplayBase + 0x70088;
constexpr uint32 kCursorPaletteA = kVlvDisplayBase + 0x70090;
constexpr uint32 kCursorSurfaceLiveA = kVlvDisplayBase + 0x700ac;
constexpr uint32 kFwBlcSelfVlv = kVlvDisplayBase + 0x6500;

constexpr uint32 kLastSnapshotRegister = kPlaneSurfaceLiveA;

constexpr uint32 kDpllVcoEnable = 1u << 31;
constexpr uint32 kDpllLock = 1u << 15;
constexpr uint32 kPipeEnable = 1u << 31;
constexpr uint32 kPlaneEnable = 1u << 31;
constexpr uint32 kDpPortEnable = 1u << 31;
constexpr uint32 kPpsOn = 1u << 31;
constexpr uint32 kPpsReady = 1u << 30;
constexpr uint32 kPwmEnable = 1u << 31;
constexpr uint32 kPanelFitterEnable = 1u << 31;
constexpr uint32 kFwCxsrEnable = 1u << 15;
constexpr uint32 kCursorModeMask = 0x27;
constexpr uint32 kCursorMode64TwoColor = 0x04;
constexpr uint32 kCursorMode64Argb = 0x27;
constexpr uint32 kCursorPositionNegative = 1u << 15;
constexpr uint32 kCursorPositionMask = 0x7fff;
constexpr uint32 kPlaneFormatMask = 0xfu << 26;
constexpr uint32 kPlaneFormatBgrx888 = 6u << 26;
constexpr uint32 kPlaneTiled = 1u << 10;

// The Gen7 ValleyView GTTMMADR BAR is 4 MiB: the low 2 MiB is the register
// block and the high 2 MiB is the global GTT. Each 4-byte PTE maps one 4 KiB
// page; the physical page frame lives in bits 31:12 (valid for the <4 GiB
// stolen memory this board uses), with bit 0 marking a present entry.
constexpr uint32 kGttOffsetInBar = 0x200000;
constexpr uint32 kGen7PteSize = 4;
constexpr uint32 kGen7PtePresent = 1u << 0;
constexpr uint32 kGen7PtePhysMask = 0xfffff000u;
constexpr uint32 kPageMask = 0xfffu;
constexpr uint32 kPageSize = kPageMask + 1;

constexpr size_t kOpRegionSize = 8 * 1024;
constexpr uint32 kOpRegionAsls = 0xfc;
constexpr uint32 kOpRegionVbtOffset = 0x400;
constexpr uint32 kOpRegionMboxesOffset = 88;
constexpr uint32 kOpRegionAsleOffset = 0x300;
constexpr uint32 kAsleRvdaOffset = 186;
constexpr uint32 kAsleRvdsOffset = 194;
constexpr uint32 kMailboxAsle = 1u << 2;
constexpr uint32 kMaxVbtSize = 1024 * 1024;
constexpr uint32 kVbtHeaderSize = 48;
constexpr uint32 kBdbHeaderSize = 22;
constexpr uint64 kGgttSignatureSeed = 1469598103934665603ull;
constexpr uint64 kGgttSignaturePrime = 1099511628211ull;


inline uint16
DecodeLowSize(uint32 value)
{
	return static_cast<uint16>((value & 0xffff) + 1);
}


inline uint16
DecodeHighSize(uint32 value)
{
	return static_cast<uint16>((value >> 16) + 1);
}


inline bool
HasSignature(const uint8* bytes, size_t size, const char* signature,
	size_t signatureSize)
{
	if (bytes == NULL || signature == NULL || size < signatureSize)
		return false;

	for (size_t index = 0; index < signatureSize; index++) {
		if (bytes[index] != static_cast<uint8>(signature[index]))
			return false;
	}
	return true;
}


inline bool
FindVbt(const uint8* bytes, size_t size, uint32& offset)
{
	if (bytes == NULL)
		return false;

	for (size_t index = 0; index + 4 <= size; index += 4) {
		if (bytes[index] == '$' && bytes[index + 1] == 'V'
			&& bytes[index + 2] == 'B' && bytes[index + 3] == 'T') {
			offset = static_cast<uint32>(index);
			return true;
		}
	}
	return false;
}


inline uint16
ReadVbtLe16(const uint8* bytes)
{
	return static_cast<uint16>(bytes[0])
		| static_cast<uint16>(bytes[1]) << 8;
}


inline uint32
ReadVbtLe32(const uint8* bytes)
{
	return static_cast<uint32>(bytes[0])
		| static_cast<uint32>(bytes[1]) << 8
		| static_cast<uint32>(bytes[2]) << 16
		| static_cast<uint32>(bytes[3]) << 24;
}


inline bool
ValidateVbt(const uint8* bytes, size_t mappedSize, uint32& declaredSize)
{
	if (bytes == NULL || mappedSize < kVbtHeaderSize
		|| !HasSignature(bytes, mappedSize, "$VBT", 4)) {
		return false;
	}

	const uint16 headerSize = ReadVbtLe16(bytes + 22);
	const uint16 vbtSize = ReadVbtLe16(bytes + 24);
	const uint32 bdbOffset = ReadVbtLe32(bytes + 28);
	if (headerSize < kVbtHeaderSize || headerSize > vbtSize
		|| vbtSize > mappedSize || bdbOffset > vbtSize
		|| vbtSize - bdbOffset < kBdbHeaderSize) {
		return false;
	}

	const uint8* bdb = bytes + bdbOffset;
	if (!HasSignature(bdb, vbtSize - bdbOffset, "BIOS_DATA_BLOCK ", 16))
		return false;

	const uint16 bdbHeaderSize = ReadVbtLe16(bdb + 18);
	const uint16 bdbSize = ReadVbtLe16(bdb + 20);
	if (bdbHeaderSize < kBdbHeaderSize || bdbHeaderSize > bdbSize
		|| bdbSize > vbtSize - bdbOffset) {
		return false;
	}

	declaredSize = vbtSize;
	return true;
}


inline uint64
DecodePtePhysical(uint32 pte)
{
	if ((pte & kGen7PtePresent) == 0)
		return 0;
	return static_cast<uint64>(pte & kGen7PtePhysMask);
}


inline uint64
AppendGgttPteSignature(uint64 signature, uint32 pte)
{
	for (uint32 byte = 0; byte < sizeof(pte); byte++) {
		signature ^= (pte >> (byte * 8)) & 0xff;
		signature *= kGgttSignaturePrime;
	}
	return signature;
}


inline bool
RangeFits(uint64 offset, uint64 length, uint64 size)
{
	return length != 0 && offset <= size && length <= size - offset;
}


inline void
DecodeFirmwareSnapshot(FirmwareSnapshot& snapshot)
{
	snapshot.flags &= ~(kSnapshotDpllEnabled | kSnapshotDpllLocked
		| kSnapshotPipeEnabled | kSnapshotPlaneEnabled
		| kSnapshotPortEnabled | kSnapshotPpsOn | kSnapshotPpsReady
		| kSnapshotPwmEnabled | kSnapshotCursorEnabled
		| kSnapshotPanelFitterEnabled | kSnapshotBootFramebuffer
		| kSnapshotAdoptionCompatible | kSnapshotScanoutMatchesBoot
		| kSnapshotGttRangePresent);

	snapshot.hDisplay = DecodeLowSize(snapshot.hTotal);
	snapshot.hTotalPixels = DecodeHighSize(snapshot.hTotal);
	snapshot.vDisplay = DecodeLowSize(snapshot.vTotal);
	snapshot.vTotalLines = DecodeHighSize(snapshot.vTotal);
	snapshot.sourceWidth = DecodeHighSize(snapshot.pipeSource);
	snapshot.sourceHeight = DecodeLowSize(snapshot.pipeSource);
	snapshot.pwmPeriod
		= static_cast<uint16>(((snapshot.pwmControl >> 17) & 0x7fff) * 2);
	snapshot.pwmDuty = static_cast<uint16>(snapshot.pwmControl);
	snapshot.ppsPort = static_cast<uint8>(snapshot.ppsOnDelays >> 30);
	snapshot.dpPipe = static_cast<uint8>((snapshot.dpC >> 30) & 1);

	if ((snapshot.dpllA & kDpllVcoEnable) != 0)
		snapshot.flags |= kSnapshotDpllEnabled;
	if ((snapshot.dpllA & kDpllLock) != 0)
		snapshot.flags |= kSnapshotDpllLocked;
	if ((snapshot.pipeConfig & kPipeEnable) != 0)
		snapshot.flags |= kSnapshotPipeEnabled;
	if ((snapshot.planeControl & kPlaneEnable) != 0)
		snapshot.flags |= kSnapshotPlaneEnabled;
	if ((snapshot.dpC & kDpPortEnable) != 0)
		snapshot.flags |= kSnapshotPortEnabled;
	if ((snapshot.ppsStatus & kPpsOn) != 0)
		snapshot.flags |= kSnapshotPpsOn;
	if ((snapshot.ppsStatus & kPpsReady) != 0)
		snapshot.flags |= kSnapshotPpsReady;
	if ((snapshot.pwmControl2 & kPwmEnable) != 0)
		snapshot.flags |= kSnapshotPwmEnabled;
	if ((snapshot.cursorControl & kCursorModeMask) != 0)
		snapshot.flags |= kSnapshotCursorEnabled;
	if ((snapshot.panelFitterControl & kPanelFitterEnable) != 0)
		snapshot.flags |= kSnapshotPanelFitterEnabled;

	const bool framebufferSizeValid = snapshot.bootHeight != 0
		&& snapshot.bootBytesPerRow != 0
		&& snapshot.bootFramebufferSize
			>= static_cast<uint64>(snapshot.bootBytesPerRow)
				* snapshot.bootHeight;
	if (snapshot.bootFramebufferStatus == B_OK
		&& snapshot.bootFramebufferPhysical != 0 && framebufferSizeValid) {
		snapshot.flags |= kSnapshotBootFramebuffer;
	}

	// FRAME_BUFFER_BOOT_INFO names the CPU-visible GMADR aperture, while the
	// GTT PTE names its backing stolen-memory page. Prove ownership in the
	// aperture domain: the boot address must exactly equal GMADR plus the live
	// DSPSURF and DSPLINOFF values, and the complete buffer must fit in BAR2.
	const uint64 scanoutOffset
		= static_cast<uint64>(snapshot.planeGgttOffset)
			+ snapshot.planeLinearOffset;
	if (snapshot.gmadrBase != 0
		&& scanoutOffset <= UINT64_MAX - snapshot.gmadrBase) {
		snapshot.scanoutAperture = snapshot.gmadrBase + scanoutOffset;
	} else
		snapshot.scanoutAperture = 0;

	snapshot.scanoutPhysical = DecodePtePhysical(snapshot.gttPte);
	if (snapshot.scanoutAperture != 0
		&& snapshot.scanoutAperture == snapshot.bootFramebufferPhysical
		&& RangeFits(scanoutOffset, snapshot.bootFramebufferSize,
			snapshot.gmadrSize)) {
		snapshot.flags |= kSnapshotScanoutMatchesBoot;
	}

	if (snapshot.gttRequiredPages != 0
		&& snapshot.gttPresentPages == snapshot.gttRequiredPages)
		snapshot.flags |= kSnapshotGttRangePresent;

	const uint32 requiredState = kSnapshotMmioMapped
		| kSnapshotDpllEnabled | kSnapshotDpllLocked
		| kSnapshotPipeEnabled | kSnapshotPlaneEnabled
		| kSnapshotPortEnabled | kSnapshotPpsOn | kSnapshotPpsReady
		| kSnapshotBootFramebuffer | kSnapshotScanoutMatchesBoot
		| kSnapshotGttRangePresent;
	if ((snapshot.flags & requiredState) == requiredState
		&& snapshot.bootWidth == snapshot.sourceWidth
		&& snapshot.bootHeight == snapshot.sourceHeight
		&& snapshot.bootDepth == 32
		&& snapshot.bootBytesPerRow == snapshot.planeStride
		&& (snapshot.planeControl & kPlaneFormatMask)
			== kPlaneFormatBgrx888
		&& (snapshot.planeControl & kPlaneTiled) == 0) {
		snapshot.flags |= kSnapshotAdoptionCompatible;
	}
}

} // namespace valleyview

#endif
