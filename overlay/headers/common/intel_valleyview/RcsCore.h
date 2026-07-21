// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot
#ifndef INTEL_VALLEYVIEW_RCS_CORE_H
#define INTEL_VALLEYVIEW_RCS_CORE_H

#include <common/intel_valleyview/GpuCore.h>

#include <stddef.h>


namespace valleyview {

constexpr uint32 kRcsRingBase = 0x02000;
constexpr uint32 kRcsRingTail = kRcsRingBase + 0x30;
constexpr uint32 kRcsRingHead = kRcsRingBase + 0x34;
constexpr uint32 kRcsRingStart = kRcsRingBase + 0x38;
constexpr uint32 kRcsRingControl = kRcsRingBase + 0x3c;
constexpr uint32 kRcsRingIpeir = kRcsRingBase + 0x64;
constexpr uint32 kRcsRingIpehr = kRcsRingBase + 0x68;
constexpr uint32 kRcsRingInstdone = kRcsRingBase + 0x6c;
constexpr uint32 kRcsRingActhd = kRcsRingBase + 0x74;
constexpr uint32 kRcsRingMiMode = kRcsRingBase + 0x9c;
constexpr uint32 kRcsRingInstpm = kRcsRingBase + 0xc0;
constexpr uint32 kRcsRingBbstate = kRcsRingBase + 0x110;
constexpr uint32 kRcsRingBbaddr = kRcsRingBase + 0x140;
constexpr uint32 kRcsRingCcid = kRcsRingBase + 0x180;
constexpr uint32 kRcsRingPpDirDclv = kRcsRingBase + 0x220;
constexpr uint32 kRcsRingPpDirBase = kRcsRingBase + 0x228;
constexpr uint32 kRcsRingContextControl = kRcsRingBase + 0x244;
constexpr uint32 kRcsRingMode = kRcsRingBase + 0x29c;
constexpr uint32 kRcsRingTimestamp = kRcsRingBase + 0x358;
constexpr uint32 kRcsRingContextStatus = kRcsRingBase + 0x3a0;
constexpr uint32 kRcsRingHws = 0x04080;
constexpr uint32 kRcsRingFault = 0x04094;

constexpr uint32 kGen6ResetRender = 1u << 1;
constexpr uint32 kRcsCcidEnable = 1u << 0;
constexpr uint32 kRcsModeIdle = 1u << 9;
constexpr uint32 kRcsInstpmTlbInvalidate = 1u << 9;
constexpr uint32 kRcsInstpmSyncFlush = 1u << 5;
constexpr uint32 kRcsFaultAddressMask = 0xfffff000;
constexpr uint32 kRcsFaultGgtt = 1u << 11;
constexpr uint32 kRcsFaultSourceMask = 0x000007f8;
constexpr uint32 kRcsFaultSourceShift = 3;
constexpr uint32 kRcsFaultTypeMask = 0x00000006;
constexpr uint32 kRcsFaultTypeShift = 1;
constexpr uint32 kRcsFaultValid = 1u << 0;

constexpr uint32 kRcsTestPageCount = 4;
constexpr uint32 kRcsTestBytes = kRcsTestPageCount * kPageSize;
constexpr uint32 kRcsRingPage = 0;
constexpr uint32 kRcsStatusPage = 1;
constexpr uint32 kRcsBatchPage = 2;
constexpr uint32 kRcsResultPage = 3;
constexpr uint32 kRcsBatchMarkerOffset = 0;
constexpr uint32 kRcsTimestampOffset = 4;
constexpr uint32 kRcsCompletionOffset = 8;
constexpr uint32 kRcsBatchMarker = 0x52435342;
constexpr uint32 kRcsCompletionMarker = 0x52435343;
constexpr uint32 kRcsResultSentinel = 0xa55a3cc3;

constexpr uint32 kMiNoop = 0;
constexpr uint32 kMiBatchBufferEnd = 0x05000000;
constexpr uint32 kMiBatchBufferStart = 0x18800000;
constexpr uint32 kMiBatchNonSecureI965 = 1u << 8;
constexpr uint32 kMiStoreDwordImmGen4 = 0x10000002;
constexpr uint32 kMiStoreRegisterMem = 0x12000001;
constexpr uint32 kMiUseGgtt = 1u << 22;
constexpr uint32 kGen7PipeControl = 0x7a000002;
constexpr uint32 kPipeControlDepthCacheFlush = 1u << 0;
constexpr uint32 kPipeControlDcFlush = 1u << 5;
constexpr uint32 kPipeControlFlushEnable = 1u << 7;
constexpr uint32 kPipeControlRenderTargetFlush = 1u << 12;
constexpr uint32 kPipeControlQwordWrite = 1u << 14;
constexpr uint32 kPipeControlCsStall = 1u << 20;
constexpr uint32 kPipeControlGlobalGttIvb = 1u << 24;
constexpr uint32 kRcsCompletionFlags
	= kPipeControlDepthCacheFlush
		| kPipeControlDcFlush
		| kPipeControlFlushEnable
		| kPipeControlRenderTargetFlush
		| kPipeControlQwordWrite
		| kPipeControlCsStall
		| kPipeControlGlobalGttIvb;

constexpr size_t kRcsBatchCommandCount = 6;
constexpr size_t kRcsRingCommandCount = 10;


struct RcsRegisterSnapshot {
	uint32	tail;
	uint32	head;
	uint32	start;
	uint32	control;
	uint32	hws;
	uint32	miMode;
	uint32	mode;
	uint32	instpm;
	uint32	acthd;
	uint32	ipehr;
	uint32	ipeir;
	uint32	instdone;
	uint32	bbstate;
	uint32	bbaddr;
	uint32	ccid;
	uint32	faultRegister;
	uint32	contextControl;
	uint32	contextStatus;
	uint32	ppDirDclv;
	uint32	ppDirBase;
	uint32	timestamp;
};


inline bool
IsRcsRingAvailable(const RcsRegisterSnapshot& snapshot)
{
	return (snapshot.control & kRingValid) == 0
		&& (snapshot.miMode & kRingStop) == 0
		&& (snapshot.miMode & kRcsModeIdle) != 0
		&& (snapshot.mode & kRingPpgttEnable) == 0
		&& (snapshot.ccid & kRcsCcidEnable) == 0
		&& (snapshot.head & kRingAddressMask)
			== (snapshot.tail & kRingAddressMask);
}


inline bool
IsRcsRingRestored(const RcsRegisterSnapshot& expected,
	const RcsRegisterSnapshot& observed)
{
	return observed.start == expected.start
		&& observed.hws == expected.hws
		&& (observed.head & kRingAddressMask)
			== (expected.head & kRingAddressMask)
		&& (observed.tail & kRingAddressMask)
			== (expected.tail & kRingAddressMask)
		&& observed.control == expected.control
		&& observed.miMode == expected.miMode
		&& observed.mode == expected.mode
		&& observed.ccid == expected.ccid
		&& observed.contextControl == expected.contextControl
		&& observed.ppDirDclv == expected.ppDirDclv
		&& observed.ppDirBase == expected.ppDirBase;
}


inline bool
RcsFaultIsValid(uint32 fault)
{
	return (fault & kRcsFaultValid) != 0;
}


inline uint32
RcsFaultAddress(uint32 fault)
{
	return fault & kRcsFaultAddressMask;
}


inline uint32
RcsFaultSource(uint32 fault)
{
	return (fault & kRcsFaultSourceMask) >> kRcsFaultSourceShift;
}


inline uint32
RcsFaultType(uint32 fault)
{
	return (fault & kRcsFaultTypeMask) >> kRcsFaultTypeShift;
}


inline bool
RcsTimestampInWindow(uint32 before, uint32 observed, uint32 after)
{
	return observed - before <= after - before;
}


inline size_t
BuildRcsDiagnosticBatch(uint32* commands, size_t capacity,
	uint32 resultOffset, uint32 marker)
{
	if (commands == NULL || capacity < kRcsBatchCommandCount
		|| (resultOffset & (sizeof(uint32) - 1)) != 0
		|| resultOffset > UINT32_MAX - kRcsCompletionOffset) {
		return 0;
	}

	commands[0] = kMiStoreDwordImmGen4 | kMiUseGgtt;
	commands[1] = 0;
	commands[2] = resultOffset + kRcsBatchMarkerOffset;
	commands[3] = marker;
	commands[4] = kMiBatchBufferEnd;
	commands[5] = kMiNoop;
	return kRcsBatchCommandCount;
}


inline size_t
BuildRcsDiagnosticRing(uint32* commands, size_t capacity,
	uint32 batchOffset, uint32 resultOffset, uint32 completionMarker)
{
	if (commands == NULL || capacity < kRcsRingCommandCount
		|| (batchOffset & kPageMask) != 0
		|| (resultOffset & kPageMask) != 0
		|| resultOffset > UINT32_MAX - kRcsCompletionOffset) {
		return 0;
	}

	commands[0] = kMiBatchBufferStart | kMiBatchNonSecureI965;
	commands[1] = batchOffset;
	commands[2] = kMiStoreRegisterMem | kMiUseGgtt;
	commands[3] = kRcsRingTimestamp;
	commands[4] = resultOffset + kRcsTimestampOffset;
	commands[5] = kGen7PipeControl;
	commands[6] = kRcsCompletionFlags;
	commands[7] = resultOffset + kRcsCompletionOffset;
	commands[8] = completionMarker;
	commands[9] = kMiNoop;
	return kRcsRingCommandCount;
}

} // namespace valleyview

#endif
