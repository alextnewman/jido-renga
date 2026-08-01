// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot

#include "Driver.h"

#include <common/intel_valleyview/GpuCore.h>
#include <common/intel_valleyview/PpgttCore.h>
#include <common/intel_valleyview/RcsCore.h>
#include <common/intel_valleyview/RenderMemoryCore.h>

#include <KernelExport.h>

#include <string.h>
#include <vm/vm.h>


namespace {

constexpr bigtime_t kForcewakeTimeoutUs = 50000;
constexpr bigtime_t kGtThreadTimeoutUs = 5000;
constexpr bigtime_t kGtFifoTimeoutUs = 10000;
constexpr bigtime_t kRingTimeoutUs = 100000;
constexpr bigtime_t kResetTimeoutUs = 2000;
constexpr uint32 kSourceSentinel = 0x11111111;
constexpr uint32 kDestinationSentinel = 0x22222222;


bool
EncodeRenderGgttEntry(const ValleyViewRenderBuffer& buffer, uint32 page,
	uint32& entry)
{
	if (page >= buffer.pageCount)
		return false;
	if (buffer.ggttEncoding == kValleyViewRenderGgttPpgttDirectory)
		return valleyview::EncodePpgttPde(buffer.physicalPages[page], entry);
	return buffer.ggttEncoding == kValleyViewRenderGgttData
		&& valleyview::EncodeBytPte(buffer.physicalPages[page], true, true,
			entry);
}


uint32
ReadMmio(const volatile uint8* registers, uint32 offset)
{
	return *(const volatile uint32*)(registers + offset);
}


void
WriteMmio(volatile uint8* registers, uint32 offset, uint32 value)
{
	*(volatile uint32*)(registers + offset) = value;
}


status_t
WaitForMask(const volatile uint8* registers, uint32 offset, uint32 mask,
	uint32 expected, bigtime_t timeout)
{
	const bigtime_t deadline = system_time() + timeout;
	do {
		if ((ReadMmio(registers, offset) & mask) == expected)
			return B_OK;
		snooze(10);
	} while (system_time() < deadline);
	return B_TIMED_OUT;
}


status_t
WaitForGtFifo(const volatile uint8* registers)
{
	const bigtime_t deadline = system_time() + kGtFifoTimeoutUs;
	do {
		const uint32 entries = ReadMmio(registers, valleyview::kGtFifoControl)
			& valleyview::kGtFifoFreeEntriesMask;
		if (entries > valleyview::kGtFifoReservedEntries)
			return B_OK;
		snooze(10);
	} while (system_time() < deadline);
	return B_TIMED_OUT;
}


status_t
WriteGt(volatile uint8* registers, uint32 offset, uint32 value)
{
	status_t status = WaitForGtFifo(registers);
	if (status != B_OK)
		return status;
	WriteMmio(registers, offset, value);
	return B_OK;
}


void
ReadGpuRegisters(const volatile uint8* registers,
	valleyview::GpuRegisterSnapshot& snapshot)
{
	snapshot.gtlcWakeControl
		= ReadMmio(registers, valleyview::kGtlcWakeControl);
	snapshot.gtlcPowerStatus
		= ReadMmio(registers, valleyview::kGtlcPowerStatus);
	snapshot.forcewakeRender
		= ReadMmio(registers, valleyview::kForcewakeRender);
	snapshot.forcewakeAckRender
		= ReadMmio(registers, valleyview::kForcewakeAckRender);
	snapshot.forcewakeMedia
		= ReadMmio(registers, valleyview::kForcewakeMedia);
	snapshot.forcewakeAckMedia
		= ReadMmio(registers, valleyview::kForcewakeAckMedia);
	snapshot.gtFifoControl
		= ReadMmio(registers, valleyview::kGtFifoControl);
	snapshot.gtFifoDebug
		= ReadMmio(registers, valleyview::kGtFifoDebug);
	snapshot.gtThreadStatus
		= ReadMmio(registers, valleyview::kGtThreadStatus);
	snapshot.renderC0Count
		= ReadMmio(registers, valleyview::kRenderC0Count);
	snapshot.mediaC0Count
		= ReadMmio(registers, valleyview::kMediaC0Count);
	snapshot.gdrst = ReadMmio(registers, valleyview::kGen6Gdrst);
	snapshot.bcsTail = ReadMmio(registers, valleyview::kRingTail);
	snapshot.bcsHead = ReadMmio(registers, valleyview::kRingHead);
	snapshot.bcsStart = ReadMmio(registers, valleyview::kRingStart);
	snapshot.bcsControl = ReadMmio(registers, valleyview::kRingControl);
	snapshot.bcsHws = ReadMmio(registers, valleyview::kRingHws);
	snapshot.bcsMiMode = ReadMmio(registers, valleyview::kRingMiMode);
	snapshot.bcsMode = ReadMmio(registers, valleyview::kRingMode);
	snapshot.bcsActhd = ReadMmio(registers, valleyview::kRingActhd);
	snapshot.bcsIpehr = ReadMmio(registers, valleyview::kRingIpehr);
	snapshot.bcsIpeir = ReadMmio(registers, valleyview::kRingIpeir);
	snapshot.bcsInstdone = ReadMmio(registers, valleyview::kRingInstdone);
}


void
ReadRcsRegisters(const volatile uint8* registers,
	valleyview::RcsRegisterSnapshot& snapshot)
{
	snapshot.tail = ReadMmio(registers, valleyview::kRcsRingTail);
	snapshot.head = ReadMmio(registers, valleyview::kRcsRingHead);
	snapshot.start = ReadMmio(registers, valleyview::kRcsRingStart);
	snapshot.control = ReadMmio(registers, valleyview::kRcsRingControl);
	snapshot.hws = ReadMmio(registers, valleyview::kRcsRingHws);
	snapshot.miMode = ReadMmio(registers, valleyview::kRcsRingMiMode);
	snapshot.mode = ReadMmio(registers, valleyview::kRcsRingMode);
	snapshot.instpm = ReadMmio(registers, valleyview::kRcsRingInstpm);
	snapshot.acthd = ReadMmio(registers, valleyview::kRcsRingActhd);
	snapshot.ipehr = ReadMmio(registers, valleyview::kRcsRingIpehr);
	snapshot.ipeir = ReadMmio(registers, valleyview::kRcsRingIpeir);
	snapshot.instdone = ReadMmio(registers, valleyview::kRcsRingInstdone);
	snapshot.bbstate = ReadMmio(registers, valleyview::kRcsRingBbstate);
	snapshot.bbaddr = ReadMmio(registers, valleyview::kRcsRingBbaddr);
	snapshot.ccid = ReadMmio(registers, valleyview::kRcsRingCcid);
	snapshot.faultRegister = ReadMmio(registers,
		valleyview::kRcsRingFault);
	snapshot.contextControl = ReadMmio(registers,
		valleyview::kRcsRingContextControl);
	snapshot.contextStatus = ReadMmio(registers,
		valleyview::kRcsRingContextStatus);
	snapshot.ppDirDclv = ReadMmio(registers,
		valleyview::kRcsRingPpDirDclv);
	snapshot.ppDirBase = ReadMmio(registers,
		valleyview::kRcsRingPpDirBase);
	snapshot.timestamp = ReadMmio(registers, valleyview::kRcsRingTimestamp);
}


bool
BcsStateUnchanged(const valleyview::GpuRegisterSnapshot& before,
	const valleyview::GpuRegisterSnapshot& after)
{
	return before.bcsTail == after.bcsTail
		&& before.bcsHead == after.bcsHead
		&& before.bcsStart == after.bcsStart
		&& before.bcsControl == after.bcsControl
		&& before.bcsHws == after.bcsHws
		&& before.bcsMiMode == after.bcsMiMode
		&& before.bcsMode == after.bcsMode;
}


uint64
DisplaySignature(const volatile uint8* registers)
{
	const uint32 values[] = {
		ReadMmio(registers, valleyview::kDpllA),
		ReadMmio(registers, valleyview::kPipeConfigA),
		ReadMmio(registers, valleyview::kPlaneControlA),
		ReadMmio(registers, valleyview::kPlaneLinearOffsetA),
		ReadMmio(registers, valleyview::kPlaneStrideA),
		ReadMmio(registers, valleyview::kPlaneSurfaceA),
		ReadMmio(registers, valleyview::kDpC),
		ReadMmio(registers, valleyview::kPpsStatusA),
		ReadMmio(registers, valleyview::kPpsControlA),
		ReadMmio(registers, valleyview::kPwmControl2A),
		ReadMmio(registers, valleyview::kPwmControlA)
	};

	uint64 signature = 1469598103934665603ull;
	for (size_t index = 0; index < sizeof(values) / sizeof(values[0]);
			index++) {
		for (size_t byte = 0; byte < sizeof(values[index]); byte++) {
			signature ^= (values[index] >> (byte * 8)) & 0xff;
			signature *= 1099511628211ull;
		}
	}
	return signature;
}


bool
PtesUniform(const uint32* ptes)
{
	for (uint32 index = 1; index < valleyview::kGpuTestPageCount; index++) {
		if (ptes[index] != ptes[0])
			return false;
	}
	return true;
}


void
ReadTestPtes(const volatile uint8* registers, uint32 ggttOffset,
	uint32* ptes)
{
	const uint32 firstIndex = ggttOffset / valleyview::kPageSize;
	for (uint32 index = 0; index < valleyview::kGpuTestPageCount; index++) {
		ptes[index] = ReadMmio(registers, valleyview::kGttOffsetInBar
			+ (firstIndex + index) * valleyview::kGen7PteSize);
	}
}


void
WriteTestPtes(volatile uint8* registers, uint32 ggttOffset,
	const uint32* ptes)
{
	const uint32 firstIndex = ggttOffset / valleyview::kPageSize;
	for (uint32 index = 0; index < valleyview::kGpuTestPageCount; index++) {
		WriteMmio(registers, valleyview::kGttOffsetInBar
			+ (firstIndex + index) * valleyview::kGen7PteSize, ptes[index]);
	}
	memory_write_barrier();
	WriteMmio(registers, valleyview::kGfxFlushControl,
		valleyview::kGfxFlushEnable);
	ReadMmio(registers, valleyview::kGfxFlushControl);
}


void
FlushGgtt(volatile uint8* registers)
{
	memory_write_barrier();
	WriteMmio(registers, valleyview::kGfxFlushControl,
		valleyview::kGfxFlushEnable);
	ReadMmio(registers, valleyview::kGfxFlushControl);
}


status_t
EnableGtWake(volatile uint8* registers,
	const valleyview::GpuRegisterSnapshot& before, bool& changed)
{
	changed = false;
	if ((before.gtlcWakeControl & valleyview::kGtlcAllowWakeRequest) != 0)
		return B_OK;

	WriteMmio(registers, valleyview::kGtlcWakeControl,
		before.gtlcWakeControl | valleyview::kGtlcAllowWakeRequest);
	changed = true;
	ReadMmio(registers, valleyview::kGtlcWakeControl);
	status_t status = WaitForMask(registers, valleyview::kGtlcPowerStatus,
		valleyview::kGtlcAllowWakeAck, valleyview::kGtlcAllowWakeAck,
		kForcewakeTimeoutUs);
	return status;
}


status_t
RestoreGtWake(volatile uint8* registers,
	const valleyview::GpuRegisterSnapshot& before, bool changed)
{
	if (!changed)
		return B_OK;

	WriteMmio(registers, valleyview::kGtlcWakeControl,
		before.gtlcWakeControl);
	ReadMmio(registers, valleyview::kGtlcWakeControl);
	const uint32 expected
		= (before.gtlcPowerStatus & valleyview::kGtlcAllowWakeAck) != 0
		? valleyview::kGtlcAllowWakeAck : 0;
	return WaitForMask(registers, valleyview::kGtlcPowerStatus,
		valleyview::kGtlcAllowWakeAck, expected, kForcewakeTimeoutUs);
}


status_t
AcquireForcewake(volatile uint8* registers,
	valleyview::GpuDiagnostics& diagnostics)
{
	if ((ReadMmio(registers, valleyview::kForcewakeAckRender)
			& valleyview::kForcewakeKernel) != 0
		|| (ReadMmio(registers, valleyview::kForcewakeAckMedia)
			& valleyview::kForcewakeKernel) != 0) {
		return B_BUSY;
	}

	WriteMmio(registers, valleyview::kForcewakeRender,
		(valleyview::kForcewakeKernel << 16)
			| valleyview::kForcewakeKernel);
	diagnostics.flags |= valleyview::kGpuRenderForcewake;
	status_t status = WaitForMask(registers,
		valleyview::kForcewakeAckRender, valleyview::kForcewakeKernel,
		valleyview::kForcewakeKernel, kForcewakeTimeoutUs);
	if (status != B_OK)
		return status;
	WriteMmio(registers, valleyview::kForcewakeMedia,
		(valleyview::kForcewakeKernel << 16)
			| valleyview::kForcewakeKernel);
	diagnostics.flags |= valleyview::kGpuMediaForcewake;
	status = WaitForMask(registers, valleyview::kForcewakeAckMedia,
		valleyview::kForcewakeKernel, valleyview::kForcewakeKernel,
		kForcewakeTimeoutUs);
	if (status != B_OK)
		return status;
	return WaitForMask(registers, valleyview::kGtThreadStatus,
		valleyview::kGtThreadStatusMask, 0, kGtThreadTimeoutUs);
}


status_t
ReleaseForcewake(volatile uint8* registers,
	const valleyview::GpuDiagnostics& diagnostics)
{
	status_t result = B_OK;
	if ((diagnostics.flags & valleyview::kGpuMediaForcewake) != 0) {
		WriteMmio(registers, valleyview::kForcewakeMedia,
			valleyview::kForcewakeKernel << 16);
		result = WaitForMask(registers, valleyview::kForcewakeAckMedia,
			valleyview::kForcewakeKernel, 0, kForcewakeTimeoutUs);
	}
	if ((diagnostics.flags & valleyview::kGpuRenderForcewake) != 0) {
		WriteMmio(registers, valleyview::kForcewakeRender,
			valleyview::kForcewakeKernel << 16);
		status_t status = WaitForMask(registers,
			valleyview::kForcewakeAckRender, valleyview::kForcewakeKernel,
			0, kForcewakeTimeoutUs);
		if (result == B_OK)
			result = status;
	}
	return result;
}


status_t
CreateTestMemory(area_id& area, uint32*& memory, phys_addr_t& physical)
{
	virtual_address_restrictions virtualRestrictions = {};
	virtualRestrictions.address_specification = B_ANY_KERNEL_ADDRESS;

	physical_address_restrictions physicalRestrictions = {};
	physicalRestrictions.high_address = 0x100000000ull;
	physicalRestrictions.alignment = valleyview::kPageSize;

	void* address = NULL;
	area = create_area_etc(B_SYSTEM_TEAM, "intel_valleyview GPU self-test",
		valleyview::kGpuTestBytes, B_CONTIGUOUS,
		B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA, 0, 0,
		&virtualRestrictions, &physicalRestrictions, &address);
	if (area < B_OK)
		return area;

	physical_entry entry;
	if (address == NULL
		|| get_memory_map(address, valleyview::kGpuTestBytes, &entry, 1) != B_OK
		|| entry.size < valleyview::kGpuTestBytes
		|| entry.address + valleyview::kGpuTestBytes > 0x100000000ull
		|| (entry.address & valleyview::kPageMask) != 0) {
		delete_area(area);
		area = -1;
		return B_BAD_DATA;
	}

	status_t status = vm_set_area_memory_type(area, entry.address,
		B_WRITE_COMBINING_MEMORY);
	if (status != B_OK) {
		delete_area(area);
		area = -1;
		return status;
	}

	memory = static_cast<uint32*>(address);
	physical = entry.address;
	return B_OK;
}


status_t
BuildTestMemory(uint32* memory, uint32 ggttOffset,
	valleyview::GpuDiagnostics& diagnostics)
{
	uint32* ring = memory + valleyview::kGpuRingPage
		* valleyview::kPageSize / sizeof(uint32);
	uint32* source = memory + valleyview::kGpuSourcePage
		* valleyview::kPageSize / sizeof(uint32);
	uint32* destination = memory + valleyview::kGpuDestinationPage
		* valleyview::kPageSize / sizeof(uint32);
	uint32* statusPage = memory + valleyview::kGpuStatusPage
		* valleyview::kPageSize / sizeof(uint32);

	memset(ring, 0, valleyview::kPageSize);
	for (uint32 index = 0; index < valleyview::kPageSize / sizeof(uint32);
			index++) {
		source[index] = kSourceSentinel;
		destination[index] = kDestinationSentinel;
		statusPage[index] = 0;
	}

	const uint32 sourceOffset
		= ggttOffset + valleyview::kGpuSourcePage * valleyview::kPageSize;
	const uint32 destinationOffset = ggttOffset
		+ valleyview::kGpuDestinationPage * valleyview::kPageSize;
	const uint32 statusOffset
		= ggttOffset + valleyview::kGpuStatusPage * valleyview::kPageSize;
	const size_t count = valleyview::BuildBcsSelfTestCommands(ring,
		valleyview::kPageSize / sizeof(uint32), sourceOffset,
		destinationOffset, statusOffset, valleyview::kGpuTestPattern,
		valleyview::kGpuCompletionMarker);
	if (count != valleyview::kBcsSelfTestCommandCount)
		return B_BAD_DATA;

	diagnostics.expectedPattern = valleyview::kGpuTestPattern;
	diagnostics.completionMarker = valleyview::kGpuCompletionMarker;
	diagnostics.ringTailBytes = static_cast<uint32>(count * sizeof(uint32));
	memory_write_barrier();
	return B_OK;
}


status_t
InstallTestPtes(volatile uint8* registers, phys_addr_t physical,
	valleyview::GpuDiagnostics& diagnostics)
{
	for (uint32 index = 0; index < valleyview::kGpuTestPageCount; index++) {
		if (!valleyview::EncodeBytPte(
				physical + index * valleyview::kPageSize, true, false,
				diagnostics.pteTest[index])) {
			return B_BAD_DATA;
		}
	}

	WriteTestPtes(registers, diagnostics.ggttOffset, diagnostics.pteTest);
	uint32 readback[valleyview::kGpuTestPageCount];
	ReadTestPtes(registers, diagnostics.ggttOffset, readback);
	for (uint32 index = 0; index < valleyview::kGpuTestPageCount; index++) {
		if (readback[index] != diagnostics.pteTest[index])
			return B_IO_ERROR;
	}
	return B_OK;
}


status_t
StartBcsRing(volatile uint8* registers, uint32 ringOffset,
	uint32 statusOffset, uint32 tailBytes)
{
	status_t status = WriteGt(registers, valleyview::kRingControl, 0);
	if (status != B_OK)
		return status;
	ReadMmio(registers, valleyview::kRingControl);

	status = WriteGt(registers, valleyview::kRingHead, 0);
	if (status == B_OK)
		status = WriteGt(registers, valleyview::kRingTail, 0);
	if (status == B_OK)
		status = WriteGt(registers, valleyview::kRingStart,
			ringOffset);
	if (status == B_OK)
		status = WriteGt(registers, valleyview::kRingHws,
			statusOffset);
	if (status != B_OK)
		return status;
	ReadMmio(registers, valleyview::kRingHead);

	status = WriteGt(registers, valleyview::kRingControl,
		valleyview::kRingValid);
	if (status != B_OK)
		return status;
	if ((ReadMmio(registers, valleyview::kRingControl)
			& valleyview::kRingValid) == 0) {
		return B_IO_ERROR;
	}

	status = WriteGt(registers, valleyview::kRingTail,
		tailBytes);
	ReadMmio(registers, valleyview::kRingTail);
	return status;
}


status_t
WaitForBcsCompletion(const uint32* memory, uint32 statusPageIndex,
	uint32 completionMarker)
{
	const volatile uint32* statusPage = memory
		+ statusPageIndex * valleyview::kPageSize / sizeof(uint32);
	const uint32 markerIndex
		= valleyview::kGpuCompletionOffset / sizeof(uint32);
	const bigtime_t deadline = system_time() + kRingTimeoutUs;
	do {
		memory_read_barrier();
		if (statusPage[markerIndex] == completionMarker)
			return B_OK;
		snooze(10);
	} while (system_time() < deadline);

	return B_TIMED_OUT;
}


status_t
WaitForBcsIdle(const volatile uint8* registers)
{
	const bigtime_t deadline = system_time() + kRingTimeoutUs;
	do {
		const uint32 head = ReadMmio(registers, valleyview::kRingHead)
			& valleyview::kRingAddressMask;
		const uint32 tail = ReadMmio(registers, valleyview::kRingTail)
			& valleyview::kRingAddressMask;
		if (head == tail)
			return B_OK;
		snooze(10);
	} while (system_time() < deadline);
	return B_TIMED_OUT;
}


status_t
ResetBcs(volatile uint8* registers)
{
	for (uint32 pass = 0; pass < 2; pass++) {
		WriteMmio(registers, valleyview::kGen6Gdrst,
			valleyview::kGen6ResetBlt);
		status_t status = WaitForMask(registers, valleyview::kGen6Gdrst,
			valleyview::kGen6ResetBlt, 0, kResetTimeoutUs);
		if (status != B_OK)
			return status;
	}
	snooze(50);
	return B_OK;
}


status_t
RestoreBcsRing(volatile uint8* registers,
	const valleyview::GpuRegisterSnapshot& original, bool reset)
{
	status_t status = B_OK;
	if (!reset) {
		status = WaitForBcsIdle(registers);
		if (status == B_OK)
			status = WriteGt(registers, valleyview::kRingControl, 0);
		if (status == B_OK)
			ReadMmio(registers, valleyview::kRingControl);
	}
	if (status != B_OK || reset) {
		status_t resetStatus = ResetBcs(registers);
		if (resetStatus != B_OK)
			return resetStatus;
	}

	status = WriteGt(registers, valleyview::kRingControl, 0);
	if (status == B_OK)
		status = WriteGt(registers, valleyview::kRingStart,
			original.bcsStart);
	if (status == B_OK)
		status = WriteGt(registers, valleyview::kRingHws, original.bcsHws);
	if (status == B_OK)
		status = WriteGt(registers, valleyview::kRingHead, original.bcsHead);
	if (status == B_OK)
		status = WriteGt(registers, valleyview::kRingTail, original.bcsTail);
	if (status == B_OK)
		status = WriteGt(registers, valleyview::kRingControl,
			original.bcsControl);
	if (status != B_OK)
		return status;
	ReadMmio(registers, valleyview::kRingControl);

	if (ReadMmio(registers, valleyview::kRingStart) != original.bcsStart
		|| ReadMmio(registers, valleyview::kRingHws) != original.bcsHws
		|| (ReadMmio(registers, valleyview::kRingHead)
				& valleyview::kRingAddressMask)
			!= (original.bcsHead & valleyview::kRingAddressMask)
		|| (ReadMmio(registers, valleyview::kRingTail)
				& valleyview::kRingAddressMask)
			!= (original.bcsTail & valleyview::kRingAddressMask)
		|| ReadMmio(registers, valleyview::kRingControl)
			!= original.bcsControl
		|| ReadMmio(registers, valleyview::kRingMiMode)
			!= original.bcsMiMode
		|| ReadMmio(registers, valleyview::kRingMode)
			!= original.bcsMode) {
		return B_IO_ERROR;
	}
	return B_OK;
}


status_t
PrepareRcsMemory(ValleyViewRenderBuffer& buffer,
	ValleyViewRenderBuffer& shaderBuffer,
	valleyview::RcsDiagnostic& diagnostics)
{
	if (buffer.address == NULL
		|| buffer.size < valleyview::kRcsTestBytes
		|| buffer.ggttOffset == valleyview::kInvalidRenderGgttOffset
		|| shaderBuffer.address == NULL
		|| shaderBuffer.size < valleyview::kRcsShaderTotalBytes
		|| shaderBuffer.ggttOffset
			== valleyview::kInvalidRenderGgttOffset) {
		return B_BAD_VALUE;
	}

	uint32* memory = static_cast<uint32*>(buffer.address);
	memset(memory, 0, valleyview::kRcsTestBytes);
	uint32* ring = memory + valleyview::kRcsRingPage
		* valleyview::kPageSize / sizeof(uint32);
	uint32* batch = memory + valleyview::kRcsBatchPage
		* valleyview::kPageSize / sizeof(uint32);
	uint32* result = memory + valleyview::kRcsResultPage
		* valleyview::kPageSize / sizeof(uint32);
	for (uint32 index = 0;
			index < valleyview::kPageSize / sizeof(uint32); index++) {
		result[index] = valleyview::kRcsResultSentinel;
	}

	const size_t batchCount = valleyview::BuildRcsDiagnosticBatch(batch,
		valleyview::kPageSize / sizeof(uint32), diagnostics.resultOffset,
		valleyview::kRcsBatchMarker);
	valleyview::RcsShaderLayout layout = {};
	if (!valleyview::BuildRcsShaderBatch(shaderBuffer.address,
			static_cast<uint32>(shaderBuffer.size), shaderBuffer.ggttOffset,
			layout)) {
		return B_BAD_DATA;
	}
	uint32* shaderSurface = reinterpret_cast<uint32*>(
		static_cast<uint8*>(shaderBuffer.address) + layout.surfaceOffset);
	uint32* shaderGuard = reinterpret_cast<uint32*>(
		static_cast<uint8*>(shaderBuffer.address) + layout.guardOffset);
	for (uint32 index = 0;
			index < layout.surfaceBytes / sizeof(uint32); index++) {
		shaderSurface[index] = valleyview::kRcsShaderSentinel;
	}
	for (uint32 index = 0;
			index < layout.guardBytes / sizeof(uint32); index++) {
		shaderGuard[index] = valleyview::kRcsShaderSentinel;
	}
	const size_t ringCount = valleyview::BuildRcsCombinedDiagnosticRing(ring,
		valleyview::kPageSize / sizeof(uint32), diagnostics.batchOffset,
		shaderBuffer.ggttOffset, diagnostics.resultOffset,
		valleyview::kRcsShaderCompletionMarker);
	if (batchCount != valleyview::kRcsBatchCommandCount
		|| ringCount != valleyview::kRcsCombinedRingCommandCount) {
		return B_BAD_DATA;
	}

	diagnostics.batchMarker = valleyview::kRcsBatchMarker;
	diagnostics.completionMarker
		= valleyview::kRcsShaderCompletionMarker;
	diagnostics.shaderCompletionMarker
		= valleyview::kRcsShaderCompletionMarker;
	diagnostics.batchBytes
		= static_cast<uint32>(batchCount * sizeof(uint32));
	diagnostics.ringTailBytes
		= static_cast<uint32>(ringCount * sizeof(uint32));
	diagnostics.shaderCommandBytes = layout.commandBytes;
	diagnostics.shaderKernelOffset = layout.kernelOffset;
	diagnostics.shaderSurfaceStateOffset = layout.surfaceStateOffset;
	diagnostics.shaderBindingTableOffset = layout.bindingTableOffset;
	diagnostics.shaderDescriptorOffset = layout.interfaceDescriptorOffset;
	diagnostics.shaderSurfaceOffset = layout.surfaceOffset;
	diagnostics.shaderSurfaceBytes = layout.surfaceBytes;
	diagnostics.shaderGuardOffset = layout.guardOffset;
	diagnostics.shaderGuardBytes = layout.guardBytes;
	diagnostics.shaderChecksumBefore = valleyview::RcsShaderChecksum(
		shaderSurface, layout.surfaceBytes / sizeof(uint32));
	diagnostics.flags |= valleyview::kRcsShaderCommandsBuilt;
	diagnostics.shaderStage = valleyview::kRcsShaderStageCommandsBuilt;
	memory_write_barrier();
	return B_OK;
}


status_t
FlushRcsTlb(volatile uint8* registers)
{
	const uint32 bits = valleyview::kRcsInstpmTlbInvalidate
		| valleyview::kRcsInstpmSyncFlush;
	status_t status = WriteGt(registers, valleyview::kRcsRingInstpm,
		(bits << 16) | bits);
	if (status != B_OK)
		return status;
	ReadMmio(registers, valleyview::kRcsRingInstpm);
	return WaitForMask(registers, valleyview::kRcsRingInstpm,
		valleyview::kRcsInstpmSyncFlush, 0, kResetTimeoutUs);
}


status_t
PrepareRcsRing(volatile uint8* registers, uint32 ringOffset,
	uint32 statusOffset)
{
	status_t status = WriteGt(registers, valleyview::kRcsRingControl, 0);
	if (status != B_OK)
		return status;
	ReadMmio(registers, valleyview::kRcsRingControl);

	status = WriteGt(registers, valleyview::kRcsRingHead, 0);
	if (status == B_OK)
		status = WriteGt(registers, valleyview::kRcsRingTail, 0);
	if (status == B_OK)
		status = WriteGt(registers, valleyview::kRcsRingStart, ringOffset);
	if (status == B_OK)
		status = WriteGt(registers, valleyview::kRcsRingHws, statusOffset);
	if (status != B_OK)
		return status;
	ReadMmio(registers, valleyview::kRcsRingHws);
	return B_OK;
}


status_t
EnableRcsRing(volatile uint8* registers, uint32 tailBytes)
{
	status_t status = WriteGt(registers, valleyview::kRcsRingControl,
		valleyview::kRingValid);
	if (status != B_OK)
		return status;
	if ((ReadMmio(registers, valleyview::kRcsRingControl)
			& valleyview::kRingValid) == 0) {
		return B_IO_ERROR;
	}

	status = WriteGt(registers, valleyview::kRcsRingTail, tailBytes);
	ReadMmio(registers, valleyview::kRcsRingTail);
	return status;
}


status_t
WaitForRcsCompletion(const uint32* result, uint32 marker)
{
	const volatile uint32* value = result
		+ valleyview::kRcsCompletionOffset / sizeof(uint32);
	const bigtime_t deadline = system_time() + kRingTimeoutUs;
	do {
		memory_read_barrier();
		if (*value == marker)
			return B_OK;
		snooze(10);
	} while (system_time() < deadline);
	return B_TIMED_OUT;
}


status_t
WaitForRcsIdle(const volatile uint8* registers)
{
	const bigtime_t deadline = system_time() + kRingTimeoutUs;
	do {
		const uint32 head = ReadMmio(registers, valleyview::kRcsRingHead)
			& valleyview::kRingAddressMask;
		const uint32 tail = ReadMmio(registers, valleyview::kRcsRingTail)
			& valleyview::kRingAddressMask;
		if (head == tail)
			return B_OK;
		snooze(10);
	} while (system_time() < deadline);
	return B_TIMED_OUT;
}


status_t
ResetRcs(volatile uint8* registers)
{
	for (uint32 pass = 0; pass < 2; pass++) {
		WriteMmio(registers, valleyview::kGen6Gdrst,
			valleyview::kGen6ResetRender);
		status_t status = WaitForMask(registers, valleyview::kGen6Gdrst,
			valleyview::kGen6ResetRender, 0, kResetTimeoutUs);
		if (status != B_OK)
			return status;
	}
	snooze(50);
	return B_OK;
}


status_t
RestoreRcsRing(volatile uint8* registers,
	const valleyview::RcsRegisterSnapshot& original, bool reset,
	status_t& resetStatus, bool& ringSafe,
	valleyview::RcsDiagnostic& diagnostics)
{
	ringSafe = false;
	status_t status = B_OK;
	status_t cleanupFailure = B_OK;
	if (!reset) {
		status = WaitForRcsIdle(registers);
		if (status == B_OK) {
			status = WriteGt(registers, valleyview::kRcsRingControl, 0);
		}
		if (status == B_OK)
			ReadMmio(registers, valleyview::kRcsRingControl);
		else {
			cleanupFailure = status;
			if ((diagnostics.flags & valleyview::kRcsFaultCaptured) == 0) {
				ReadGpuRegisters(registers, diagnostics.globalFault);
				ReadRcsRegisters(registers, diagnostics.fault);
				diagnostics.flags |= valleyview::kRcsFaultCaptured;
			}
		}
	}
	if (status != B_OK || reset) {
		resetStatus = ResetRcs(registers);
		if (resetStatus != B_OK)
			return resetStatus;
	}

	status = WriteGt(registers, valleyview::kRcsRingControl, 0);
	if (status == B_OK) {
		status = WriteGt(registers, valleyview::kRcsRingStart,
			original.start);
	}
	if (status == B_OK)
		status = WriteGt(registers, valleyview::kRcsRingHws, original.hws);
	if (status == B_OK)
		status = WriteGt(registers, valleyview::kRcsRingHead, original.head);
	if (status == B_OK)
		status = WriteGt(registers, valleyview::kRcsRingTail, original.tail);
	if (status == B_OK)
		status = FlushRcsTlb(registers);
	if (status == B_OK) {
		status = WriteGt(registers, valleyview::kRcsRingControl,
			original.control);
	}
	if (status != B_OK) {
		if ((diagnostics.flags & valleyview::kRcsFaultCaptured) == 0) {
			ReadGpuRegisters(registers, diagnostics.globalFault);
			ReadRcsRegisters(registers, diagnostics.fault);
			diagnostics.flags |= valleyview::kRcsFaultCaptured;
		}
		return status;
	}
	ReadMmio(registers, valleyview::kRcsRingControl);

	valleyview::RcsRegisterSnapshot observed = {};
	ReadRcsRegisters(registers, observed);
	ringSafe = valleyview::IsRcsRingRestored(original, observed);
	if (!ringSafe) {
		if ((diagnostics.flags & valleyview::kRcsFaultCaptured) == 0) {
			ReadGpuRegisters(registers, diagnostics.globalFault);
			diagnostics.fault = observed;
			diagnostics.flags |= valleyview::kRcsFaultCaptured;
		}
		return B_IO_ERROR;
	}
	return cleanupFailure;
}


status_t
RestoreRcsShaderCache(volatile uint8* registers,
	valleyview::RcsDiagnostic& diagnostics)
{
	status_t status = WriteGt(registers, valleyview::kGen7CacheMode0,
		0xffff0000u | (diagnostics.cacheMode0Before & 0xffff));
	status_t mode1Status = WriteGt(registers, valleyview::kGen7CacheMode1,
		0xffff0000u | (diagnostics.cacheMode1Before & 0xffff));
	if (status == B_OK)
		status = mode1Status;

	diagnostics.cacheMode0After = ReadMmio(registers,
		valleyview::kGen7CacheMode0);
	diagnostics.cacheMode1After = ReadMmio(registers,
		valleyview::kGen7CacheMode1);
	if ((diagnostics.cacheMode0After & 0xffff)
			!= (diagnostics.cacheMode0Before & 0xffff)
		|| (diagnostics.cacheMode1After & 0xffff)
			!= (diagnostics.cacheMode1Before & 0xffff)) {
		status = B_IO_ERROR;
	}
	return status;
}


void
CaptureRcsSubmissionFault(volatile uint8* registers,
	valleyview::RenderSubmit& submit)
{
	if ((submit.diagnosticFlags & valleyview::kRenderSubmitFaultCaptured) != 0)
		return;
	ReadGpuRegisters(registers, submit.globalFault);
	ReadRcsRegisters(registers, submit.fault);
	submit.diagnosticFlags |= valleyview::kRenderSubmitFaultCaptured;
}


status_t
ClearRcsFault(volatile uint8* registers)
{
	const uint32 fault = ReadMmio(registers, valleyview::kRcsRingFault);
	if (!valleyview::RcsFaultIsValid(fault))
		return B_OK;
	status_t status = WriteGt(registers, valleyview::kRcsRingFault,
		fault & ~valleyview::kRcsFaultValid);
	if (status != B_OK)
		return status;
	ReadMmio(registers, valleyview::kRcsRingFault);
	return valleyview::RcsFaultIsValid(
		ReadMmio(registers, valleyview::kRcsRingFault))
		? B_IO_ERROR : B_OK;
}


status_t
ProgramRcsPpgttControl(volatile uint8* registers,
	valleyview::RenderSubmit& submit, bool& touched)
{
	touched = false;
	submit.ppgttControlBefore[0]
		= ReadMmio(registers, valleyview::kRcsGacEcoBits);
	submit.ppgttControlBefore[1]
		= ReadMmio(registers, valleyview::kRcsGamEcoCheck);
	const uint32 gac = submit.ppgttControlBefore[0]
		| valleyview::kRcsGacPpgttCache64;
	const uint32 gam = (submit.ppgttControlBefore[1]
		| valleyview::kRcsGamPpgttLlc) & ~valleyview::kRcsGamPpgttGfdt;

	touched = true;
	status_t status = WriteGt(registers, valleyview::kRcsGacEcoBits, gac);
	if (status == B_OK)
		status = WriteGt(registers, valleyview::kRcsGamEcoCheck, gam);
	if (status != B_OK)
		return status;
	ReadMmio(registers, valleyview::kRcsGamEcoCheck);
	return B_OK;
}


status_t
RestoreRcsPpgttControl(volatile uint8* registers,
	valleyview::RenderSubmit& submit)
{
	status_t status = WriteGt(registers, valleyview::kRcsGacEcoBits,
		submit.ppgttControlBefore[0]);
	if (status == B_OK) {
		status = WriteGt(registers, valleyview::kRcsGamEcoCheck,
			submit.ppgttControlBefore[1]);
	}
	submit.ppgttControlAfter[0]
		= ReadMmio(registers, valleyview::kRcsGacEcoBits);
	submit.ppgttControlAfter[1]
		= ReadMmio(registers, valleyview::kRcsGamEcoCheck);
	if (submit.ppgttControlAfter[0] != submit.ppgttControlBefore[0]
		|| submit.ppgttControlAfter[1] != submit.ppgttControlBefore[1]) {
		status = B_IO_ERROR;
	}
	return status;
}


status_t
ProgramRcsPpgtt(volatile uint8* registers, uint32 ppDirBase)
{
	status_t status = WriteGt(registers, valleyview::kRcsRingPpDirDclv,
		UINT32_MAX);
	if (status == B_OK)
		status = WriteGt(registers, valleyview::kRcsRingPpDirBase, ppDirBase);
	if (status == B_OK) {
		status = WriteGt(registers, valleyview::kRcsRingMode,
			(valleyview::kRingPpgttEnable << 16)
				| valleyview::kRingPpgttEnable);
	}
	if (status != B_OK)
		return status;
	ReadMmio(registers, valleyview::kRcsRingMode);

	status = FlushRcsTlb(registers);
	if (status != B_OK)
		return status;
	return ReadMmio(registers, valleyview::kRcsRingPpDirDclv) == UINT32_MAX
			&& ReadMmio(registers, valleyview::kRcsRingPpDirBase) == ppDirBase
			&& (ReadMmio(registers, valleyview::kRcsRingMode)
				& valleyview::kRingPpgttEnable) != 0
		? B_OK : B_IO_ERROR;
}


status_t
RestoreRcsSubmissionRing(volatile uint8* registers,
	const valleyview::RcsRegisterSnapshot& original, bool reset,
	bool& ringSafe, valleyview::RenderSubmit& submit)
{
	ringSafe = false;
	status_t status = B_OK;
	status_t cleanupFailure = B_OK;
	if (!reset) {
		status = WaitForRcsIdle(registers);
		if (status == B_OK)
			status = WriteGt(registers, valleyview::kRcsRingControl, 0);
		if (status == B_OK)
			ReadMmio(registers, valleyview::kRcsRingControl);
		else {
			cleanupFailure = status;
			CaptureRcsSubmissionFault(registers, submit);
		}
	}
	if (status != B_OK || reset) {
		submit.resetStatus = ResetRcs(registers);
		if (submit.resetStatus != B_OK)
			return submit.resetStatus;
	}

	status = WriteGt(registers, valleyview::kRcsRingControl, 0);
	if (status == B_OK) {
		status = WriteGt(registers, valleyview::kRcsRingMode,
			(valleyview::kRingPpgttEnable << 16)
				| (original.mode & valleyview::kRingPpgttEnable));
	}
	if (status == B_OK) {
		status = WriteGt(registers, valleyview::kRcsRingPpDirDclv,
			original.ppDirDclv);
	}
	if (status == B_OK) {
		status = WriteGt(registers, valleyview::kRcsRingPpDirBase,
			original.ppDirBase);
	}
	if (status == B_OK)
		status = WriteGt(registers, valleyview::kRcsRingStart, original.start);
	if (status == B_OK)
		status = WriteGt(registers, valleyview::kRcsRingHws, original.hws);
	if (status == B_OK)
		status = WriteGt(registers, valleyview::kRcsRingHead, original.head);
	if (status == B_OK)
		status = WriteGt(registers, valleyview::kRcsRingTail, original.tail);
	if (status == B_OK)
		status = FlushRcsTlb(registers);
	if (status == B_OK) {
		status = WriteGt(registers, valleyview::kRcsRingControl,
			original.control);
	}
	if (status != B_OK) {
		CaptureRcsSubmissionFault(registers, submit);
		return status;
	}
	ReadMmio(registers, valleyview::kRcsRingControl);

	valleyview::RcsRegisterSnapshot observed = {};
	ReadRcsRegisters(registers, observed);
	ringSafe = valleyview::IsRcsRingRestored(original, observed);
	if (!ringSafe) {
		CaptureRcsSubmissionFault(registers, submit);
		return B_IO_ERROR;
	}
	return cleanupFailure;
}


status_t
RestoreRcsSubmissionCache(volatile uint8* registers,
	valleyview::RenderSubmit& submit)
{
	status_t status = WriteGt(registers, valleyview::kRcsL3SqcReg1,
		submit.l3Before[0]);
	if (status == B_OK) {
		status = WriteGt(registers, valleyview::kRcsL3Control2,
			submit.l3Before[1]);
	}
	if (status == B_OK) {
		status = WriteGt(registers, valleyview::kRcsL3Control3,
			submit.l3Before[2]);
	}
	if (status == B_OK) {
		status = WriteGt(registers, valleyview::kRcsRingInstpm,
			0xffff0000u | (submit.active.instpm & 0xffff));
	}

	submit.l3After[0] = ReadMmio(registers, valleyview::kRcsL3SqcReg1);
	submit.l3After[1] = ReadMmio(registers, valleyview::kRcsL3Control2);
	submit.l3After[2] = ReadMmio(registers, valleyview::kRcsL3Control3);
	if (submit.l3After[0] != submit.l3Before[0]
		|| submit.l3After[1] != submit.l3Before[1]
		|| submit.l3After[2] != submit.l3Before[2]
		|| (ReadMmio(registers, valleyview::kRcsRingInstpm) & 0xffff)
			!= (submit.active.instpm & 0xffff)) {
		status = B_IO_ERROR;
	}
	return status;
}


bool
VerifyPage(const uint32* page, uint32 sentinel, uint32& mismatchOffset,
	uint32& observed)
{
	if (valleyview::FindWordMismatch(page, valleyview::kGpuTestPixels,
			valleyview::kGpuTestPattern, mismatchOffset, observed)) {
		return false;
	}

	uint32 tailOffset = 0;
	const size_t pageWords = valleyview::kPageSize / sizeof(uint32);
	if (valleyview::FindWordMismatch(page + valleyview::kGpuTestPixels,
			pageWords - valleyview::kGpuTestPixels, sentinel, tailOffset,
			observed)) {
		mismatchOffset = valleyview::kGpuTestPixels * sizeof(uint32)
			+ tailOffset;
		return false;
	}
	return true;
}


status_t
MapGpuRegisters(const ValleyViewDevice& device, uint32 protection,
	volatile uint8*& registers, area_id& area)
{
	if (device.snapshot.mmioStatus != B_OK
		|| device.snapshot.mmioPhysical == 0
		|| device.snapshot.mmioSize < valleyview::kGttOffsetInBar) {
		return B_NO_INIT;
	}

	registers = NULL;
	area = map_physical_memory("intel_valleyview GPU diagnostics",
		device.snapshot.mmioPhysical, device.snapshot.mmioSize,
		B_ANY_KERNEL_BLOCK_ADDRESS | B_UNCACHED_MEMORY, protection,
		(void**)&registers);
	return area < B_OK ? area : B_OK;
}


status_t
PrepareDiagnostics(const ValleyViewDevice& device,
	const volatile uint8* registers, valleyview::GpuDiagnostics& diagnostics,
	bool requireUniformPtes)
{
	ReadGpuRegisters(registers, diagnostics.before);
	diagnostics.displaySignatureBefore = DisplaySignature(registers);
	diagnostics.flags |= valleyview::kGpuSnapshotCaptured;
	diagnostics.stage = valleyview::kGpuStageSnapshot;

	const uint64 liveOffset = static_cast<uint64>(
		device.snapshot.planeGgttOffset) + device.snapshot.planeLinearOffset;
	if (!valleyview::SelectGpuTestGgttOffset(device.snapshot.gmadrSize,
			liveOffset, device.snapshot.bootFramebufferSize,
			diagnostics.ggttOffset)) {
		return B_BAD_DATA;
	}

	const uint64 lastPte = valleyview::kGttOffsetInBar
		+ (static_cast<uint64>(diagnostics.ggttOffset)
				/ valleyview::kPageSize + valleyview::kGpuTestPageCount)
			* valleyview::kGen7PteSize;
	if (lastPte > device.snapshot.mmioSize)
		return B_BAD_DATA;

	ReadTestPtes(registers, diagnostics.ggttOffset, diagnostics.pteBefore);
	if (!PtesUniform(diagnostics.pteBefore)) {
		if (!requireUniformPtes)
			return B_OK;
		return B_BUSY;
	}
	diagnostics.flags |= valleyview::kGpuScratchRangeUniform;
	return B_OK;
}


bool
BcsAvailable(const valleyview::GpuRegisterSnapshot& snapshot)
{
	return (snapshot.bcsControl & valleyview::kRingValid) == 0
		&& (snapshot.bcsMiMode & valleyview::kRingStop) == 0
		&& (snapshot.bcsMode & valleyview::kRingPpgttEnable) == 0
		&& (snapshot.bcsHead & valleyview::kRingAddressMask)
			== (snapshot.bcsTail & valleyview::kRingAddressMask);
}


status_t
SubmitBcsCommandsLocked(ValleyViewDevice& device, uint32 tailBytes,
	uint32 completionMarker)
{
	if (!device.nativeActive || device.registers == NULL
		|| device.p0Private == NULL || device.gpuFaulted) {
		return B_NO_INIT;
	}

	volatile uint8* registers = device.registers;
	valleyview::GpuDiagnostics diagnostics = {};
	ReadGpuRegisters(registers, diagnostics.before);

	bool gtWakeChanged = false;
	bool forcewakeAttempted = false;
	bool ringStarted = false;
	bool resetBcs = false;
	status_t cleanupStatus = B_OK;
	status_t status = EnableGtWake(registers, diagnostics.before,
		gtWakeChanged);
	if (status != B_OK)
		goto cleanup;

	forcewakeAttempted = true;
	status = AcquireForcewake(registers, diagnostics);
	if (status != B_OK)
		goto cleanup;
	ReadGpuRegisters(registers, diagnostics.active);
	if (!BcsAvailable(diagnostics.active)) {
		status = B_BUSY;
		goto cleanup;
	}

	{
		uint32* statusPage = static_cast<uint32*>(device.p0Private)
			+ (device.p0Layout.status - device.p0Layout.cursor)
				/ sizeof(uint32);
		statusPage[valleyview::kGpuCompletionOffset / sizeof(uint32)] = 0;
		memory_write_barrier();
	}

	ringStarted = true;
	status = StartBcsRing(registers, device.p0Layout.ring,
		device.p0Layout.status, tailBytes);
	if (status != B_OK)
		goto cleanup;
	device.bcsSubmissions++;

	status = WaitForBcsCompletion(static_cast<const uint32*>(
			device.p0Private),
		(device.p0Layout.status - device.p0Layout.cursor)
			/ valleyview::kPageSize,
		completionMarker);
	if (status != B_OK)
		resetBcs = true;

cleanup:
	if (ringStarted) {
		cleanupStatus = RestoreBcsRing(registers, diagnostics.active,
			resetBcs);
		if (status == B_OK)
			status = cleanupStatus;
		if (cleanupStatus != B_OK)
			device.gpuFaulted = true;
	}
	if (forcewakeAttempted) {
		cleanupStatus = ReleaseForcewake(registers, diagnostics);
		if (status == B_OK)
			status = cleanupStatus;
	}
	cleanupStatus = RestoreGtWake(registers, diagnostics.before,
		gtWakeChanged);
	if (status == B_OK)
		status = cleanupStatus;

	if (status != B_OK) {
		device.bcsFailures++;
		device.bcsReady = false;
		device.bcsStatus = status;
		dprintf("intel_valleyview: BCS submission failed: %" B_PRId32
			" reset=%s restore=%" B_PRId32 "\n", status,
			resetBcs ? "yes" : "no", cleanupStatus);
	}
	return status;
}


void
CpuFillLocked(ValleyViewDevice& device,
	const valleyview::BcsFillRequest& request)
{
	uint8* framebuffer = static_cast<uint8*>(device.framebuffer);
	for (uint32 index = 0; index < request.count; index++) {
		const valleyview::BcsFillRect& rect = request.rects[index];
		for (uint32 y = rect.top; y <= rect.bottom; y++) {
			uint32* row = reinterpret_cast<uint32*>(
				framebuffer + y * valleyview::kP0BytesPerRow);
			for (uint32 x = rect.left; x <= rect.right; x++)
				row[x] = request.color;
		}
	}
	memory_write_barrier();
}


void
CpuBlitLocked(ValleyViewDevice& device,
	const valleyview::BcsBlitRequest& request)
{
	uint8* framebuffer = static_cast<uint8*>(device.framebuffer);
	for (uint32 index = 0; index < request.count; index++) {
		const valleyview::BcsBlitRect& rect = request.rects[index];
		const size_t bytes
			= (static_cast<size_t>(rect.width) + 1) * sizeof(uint32);
		if (rect.destinationTop > rect.sourceTop) {
			for (int32 row = rect.height; row >= 0; row--) {
				memmove(framebuffer
						+ (rect.destinationTop + row)
							* valleyview::kP0BytesPerRow
						+ rect.destinationLeft * sizeof(uint32),
					framebuffer
						+ (rect.sourceTop + row)
							* valleyview::kP0BytesPerRow
						+ rect.sourceLeft * sizeof(uint32),
					bytes);
			}
		} else {
			for (uint32 row = 0; row <= rect.height; row++) {
				memmove(framebuffer
						+ (rect.destinationTop + row)
							* valleyview::kP0BytesPerRow
						+ rect.destinationLeft * sizeof(uint32),
					framebuffer
						+ (rect.sourceTop + row)
							* valleyview::kP0BytesPerRow
						+ rect.sourceLeft * sizeof(uint32),
					bytes);
			}
		}
	}
	memory_write_barrier();
}


} // namespace


status_t
BindRenderBufferGgtt(ValleyViewDevice& device,
	ValleyViewRenderBuffer& buffer)
{
	if (buffer.pageCount == 0 || buffer.physicalPages == NULL
		|| buffer.savedPtes == NULL
		|| buffer.ggttOffset != valleyview::kInvalidRenderGgttOffset
		|| buffer.ggttAlignmentPages == 0
		|| (buffer.ggttAlignmentPages
			& (buffer.ggttAlignmentPages - 1)) != 0
		|| (buffer.ggttEncoding
				== kValleyViewRenderGgttPpgttDirectory
			&& buffer.pageCount
				!= valleyview::kPpgttDirectoryGgttPages)) {
		return B_BAD_VALUE;
	}

	mutex_lock(&device.bcsLock);
	if (!device.nativeActive || device.registers == NULL || device.gpuFaulted
		|| device.p0MemoryQuarantined || device.renderMemoryQuarantined
		|| device.p0SavedPtes == NULL) {
		mutex_unlock(&device.bcsLock);
		return B_NO_INIT;
	}

	const uint32 endPage = device.p0Layout.base / valleyview::kPageSize;
	const uint64 pteCapacity = device.snapshot.mmioSize
			> valleyview::kGttOffsetInBar
		? (device.snapshot.mmioSize - valleyview::kGttOffsetInBar)
			/ valleyview::kGen7PteSize
		: 0;
	valleyview::RenderGgttSearch search;
	if (endPage > pteCapacity
		|| !valleyview::InitializeRenderGgttSearch(search,
			valleyview::kRenderFirstGgttPage, endPage,
			buffer.pageCount, device.p0SavedPtes[0],
			buffer.ggttAlignmentPages)) {
		mutex_unlock(&device.bcsLock);
		return B_BAD_DATA;
	}

	for (uint32 page = search.firstPage; page < search.endPage; page++) {
		const uint32 pte = ReadMmio(device.registers,
			valleyview::kGttOffsetInBar
				+ page * valleyview::kGen7PteSize);
		if (valleyview::AdvanceRenderGgttSearch(search, page, pte))
			break;
	}
	if (!search.found) {
		mutex_unlock(&device.bcsLock);
		return B_NO_MEMORY;
	}

	const uint32 firstPage = search.offset / valleyview::kPageSize;
	for (uint32 page = 0; page < buffer.pageCount; page++) {
		const uint32 pte = ReadMmio(device.registers,
			valleyview::kGttOffsetInBar
				+ (firstPage + page) * valleyview::kGen7PteSize);
		if (pte != search.freePte) {
			mutex_unlock(&device.bcsLock);
			return B_BUSY;
		}
		buffer.savedPtes[page] = pte;
	}

	status_t status = B_OK;
	for (uint32 page = 0; page < buffer.pageCount; page++) {
		uint32 pte;
		if (!EncodeRenderGgttEntry(buffer, page, pte)) {
			status = B_BAD_DATA;
			break;
		}
		WriteMmio(device.registers, valleyview::kGttOffsetInBar
			+ (firstPage + page) * valleyview::kGen7PteSize, pte);
	}
	FlushGgtt(device.registers);

	if (status == B_OK) {
		for (uint32 page = 0; page < buffer.pageCount; page++) {
			uint32 expected = 0;
			if (!EncodeRenderGgttEntry(buffer, page, expected)) {
				status = B_BAD_DATA;
				break;
			}
			if (ReadMmio(device.registers, valleyview::kGttOffsetInBar
					+ (firstPage + page) * valleyview::kGen7PteSize)
					!= expected) {
				status = B_IO_ERROR;
				break;
			}
		}
	}

	if (status != B_OK) {
		for (uint32 page = 0; page < buffer.pageCount; page++) {
			WriteMmio(device.registers, valleyview::kGttOffsetInBar
				+ (firstPage + page) * valleyview::kGen7PteSize,
				buffer.savedPtes[page]);
		}
		FlushGgtt(device.registers);
		for (uint32 page = 0; page < buffer.pageCount; page++) {
			if (ReadMmio(device.registers, valleyview::kGttOffsetInBar
					+ (firstPage + page) * valleyview::kGen7PteSize)
					!= buffer.savedPtes[page]) {
				buffer.quarantined = true;
				device.renderMemoryQuarantined = true;
				break;
			}
		}
		mutex_unlock(&device.bcsLock);
		return status;
	}

	buffer.ggttOffset = search.offset;
	mutex_unlock(&device.bcsLock);
	return B_OK;
}


status_t
UnbindRenderBufferGgtt(ValleyViewDevice& device,
	ValleyViewRenderBuffer& buffer, uint32* observedPtes)
{
	if (buffer.ggttOffset == valleyview::kInvalidRenderGgttOffset)
		return buffer.quarantined ? B_IO_ERROR : B_OK;
	if (buffer.savedPtes == NULL || buffer.pageCount == 0)
		return B_BAD_VALUE;

	mutex_lock(&device.bcsLock);
	if (device.registers == NULL) {
		buffer.quarantined = true;
		device.renderMemoryQuarantined = true;
		mutex_unlock(&device.bcsLock);
		return B_NO_INIT;
	}

	const uint32 firstPage = buffer.ggttOffset / valleyview::kPageSize;
	for (uint32 page = 0; page < buffer.pageCount; page++) {
		WriteMmio(device.registers, valleyview::kGttOffsetInBar
			+ (firstPage + page) * valleyview::kGen7PteSize,
			buffer.savedPtes[page]);
	}
	FlushGgtt(device.registers);

	status_t status = B_OK;
	for (uint32 page = 0; page < buffer.pageCount; page++) {
		const uint32 observed = ReadMmio(device.registers,
			valleyview::kGttOffsetInBar
				+ (firstPage + page) * valleyview::kGen7PteSize);
		if (observedPtes != NULL)
			observedPtes[page] = observed;
		if (observed != buffer.savedPtes[page])
			status = B_IO_ERROR;
	}
	if (status == B_OK)
		buffer.ggttOffset = valleyview::kInvalidRenderGgttOffset;
	else {
		buffer.quarantined = true;
		device.renderMemoryQuarantined = true;
	}
	mutex_unlock(&device.bcsLock);
	return status;
}


status_t
SubmitRenderBcsCopy(ValleyViewDevice& device, uint32 sourceOffset,
	uint32 destinationOffset, uint32& completionMarker)
{
	if ((sourceOffset & valleyview::kPageMask) != 0
		|| (destinationOffset & valleyview::kPageMask) != 0
		|| sourceOffset == destinationOffset
		|| static_cast<uint64>(sourceOffset)
				+ valleyview::kRenderMemoryTestBytes > device.p0Layout.base
		|| static_cast<uint64>(destinationOffset)
				+ valleyview::kRenderMemoryTestBytes > device.p0Layout.base) {
		return B_BAD_VALUE;
	}

	mutex_lock(&device.bcsLock);
	if (!device.nativeActive || !device.bcsReady
		|| device.p0Private == NULL || device.gpuFaulted
		|| device.p0MemoryQuarantined || device.renderMemoryQuarantined) {
		mutex_unlock(&device.bcsLock);
		return B_NO_INIT;
	}

	uint32* ring = static_cast<uint32*>(device.p0Private)
		+ (device.p0Layout.ring - device.p0Layout.cursor) / sizeof(uint32);
	memset(ring, 0, valleyview::kPageSize);
	size_t count = 0;
	status_t status = valleyview::AppendBcsCopy(ring,
		valleyview::kPageSize / sizeof(uint32), count, sourceOffset,
		destinationOffset, valleyview::kRenderMemoryTestStride, 0, 0, 0, 0,
		valleyview::kRenderMemoryTestWidth - 1,
		valleyview::kRenderMemoryTestHeight - 1)
		? B_OK : B_BUFFER_OVERFLOW;
	completionMarker = 0xc3000000u
		| (++device.bcsSequence & 0x0fffffff);
	if (status == B_OK
		&& !valleyview::AppendBcsCompletion(ring,
			valleyview::kPageSize / sizeof(uint32), count,
			completionMarker)) {
		status = B_BUFFER_OVERFLOW;
	}
	if (status == B_OK) {
		memory_write_barrier();
		status = SubmitBcsCommandsLocked(device,
			static_cast<uint32>(count * sizeof(uint32)), completionMarker);
	}
	mutex_unlock(&device.bcsLock);
	return status;
}


status_t
ExecuteRcsSubmission(ValleyViewDevice& device,
	ValleyViewRenderBuffer& workspace, uint32 ppDirBase,
	valleyview::RenderSubmit& submit, bool resetAfterSubmission)
{
	uint32* result = static_cast<uint32*>(workspace.address)
		+ valleyview::kRcsSubmitResultPage
			* valleyview::kPageSize / sizeof(uint32);
	mutex_lock(&device.bcsLock);
	volatile uint8* registers = device.registers;
	valleyview::GpuDiagnostics forcewake = {};
	bool gtWakeChanged = false;
	bool forcewakeAttempted = false;
	bool cacheStateCaptured = false;
	bool ppgttControlTouched = false;
	bool activeCaptured = false;
	bool ringTouched = false;
	bool resetRcs = false;
	bool ringSafe = true;
	status_t cleanupStatus = B_OK;
	status_t status = B_OK;
	uint64 displaySignatureBefore = 0;

	ReadGpuRegisters(registers, submit.globalBefore);
	ReadRcsRegisters(registers, submit.before);
	displaySignatureBefore = DisplaySignature(registers);
	submit.diagnosticFlags |= valleyview::kRenderSubmitSnapshotCaptured;
	submit.stage = valleyview::kRenderSubmitStageSnapshot;
	if (!device.nativeActive || device.gpuFaulted
		|| device.p0MemoryQuarantined || device.renderMemoryQuarantined) {
		status = B_NO_INIT;
		goto cleanup;
	}

	status = EnableGtWake(registers, submit.globalBefore, gtWakeChanged);
	if (status != B_OK)
		goto cleanup;
	forcewakeAttempted = true;
	status = AcquireForcewake(registers, forcewake);
	if (status != B_OK)
		goto cleanup;
	submit.diagnosticFlags |= valleyview::kRenderSubmitForcewakeAcquired;
	status = ClearRcsFault(registers);
	if (status != B_OK)
		goto cleanup;

	submit.l3Before[0] = ReadMmio(registers, valleyview::kRcsL3SqcReg1);
	submit.l3Before[1] = ReadMmio(registers, valleyview::kRcsL3Control2);
	submit.l3Before[2] = ReadMmio(registers, valleyview::kRcsL3Control3);
	cacheStateCaptured = true;
	ReadRcsRegisters(registers, submit.active);
	activeCaptured = true;
	if (!valleyview::IsRcsRingAvailable(submit.active)) {
		status = B_BUSY;
		goto cleanup;
	}
	submit.diagnosticFlags |= valleyview::kRenderSubmitRingAvailable;

	status = ProgramRcsPpgttControl(registers, submit,
		ppgttControlTouched);
	if (status != B_OK)
		goto cleanup;
	submit.diagnosticFlags
		|= valleyview::kRenderSubmitPpgttControlProgrammed;

	ringTouched = true;
	status = PrepareRcsRing(registers,
		workspace.ggttOffset
			+ valleyview::kRcsSubmitRingPage * valleyview::kPageSize,
		workspace.ggttOffset
			+ valleyview::kRcsSubmitStatusPage * valleyview::kPageSize);
	if (status != B_OK)
		goto cleanup;

	status = ProgramRcsPpgtt(registers, ppDirBase);
	if (status != B_OK)
		goto cleanup;
	submit.diagnosticFlags |= valleyview::kRenderSubmitPpgttProgrammed
		| valleyview::kRenderSubmitTlbFlushed;
	submit.stage = valleyview::kRenderSubmitStagePpgttProgrammed;

	status = EnableRcsRing(registers, submit.ringTailBytes);
	if (status != B_OK)
		goto cleanup;
	submit.diagnosticFlags |= valleyview::kRenderSubmitRingStarted;
	submit.stage = valleyview::kRenderSubmitStageRingStarted;
	resetRcs = resetAfterSubmission;

	status = WaitForRcsCompletion(result, submit.completionMarker);
	if (status != B_OK)
		resetRcs = true;
	memory_read_barrier();
	submit.ppDirBaseObserved[0]
		= result[valleyview::kRcsPpgttLoadPostOffset / sizeof(uint32)];
	submit.ppDirBaseObserved[1]
		= result[valleyview::kRcsPpgttSecondLoadPostOffset / sizeof(uint32)];
	submit.ppgttBarrierObserved[0]
		= result[valleyview::kRcsPpgttFirstFlushOffset / sizeof(uint32)];
	submit.ppgttBarrierObserved[1]
		= result[valleyview::kRcsPpgttFirstInvalidateOffset / sizeof(uint32)];
	submit.ppgttBarrierObserved[2]
		= result[valleyview::kRcsPpgttSecondInvalidateOffset / sizeof(uint32)];
	submit.ppgttBarrierObserved[3]
		= result[valleyview::kRcsPpgttFinalFlushOffset / sizeof(uint32)];
	submit.observedCompletionMarker
		= result[valleyview::kRcsCompletionOffset / sizeof(uint32)];
	if (submit.observedCompletionMarker == submit.completionMarker) {
		submit.diagnosticFlags
			|= valleyview::kRenderSubmitCompletionVerified;
	}
	if (status == B_OK
		&& (submit.diagnosticFlags
			& valleyview::kRenderSubmitCompletionVerified) == 0) {
		status = B_BAD_DATA;
	}
	if (status == B_OK)
		submit.stage = valleyview::kRenderSubmitStageCompleted;

cleanup:
	if (status != B_OK && ringTouched)
		CaptureRcsSubmissionFault(registers, submit);
	if (ringTouched) {
		submit.ringRestoreStatus = RestoreRcsSubmissionRing(registers,
			submit.active, resetRcs, ringSafe, submit);
		if (ringSafe) {
			submit.diagnosticFlags |= valleyview::kRenderSubmitRingRestored;
		}
		if (submit.resetStatus != B_NO_INIT) {
			device.rcsResets++;
			if (submit.resetStatus == B_OK) {
				submit.diagnosticFlags
					|= valleyview::kRenderSubmitResetPerformed;
			}
		}
		if (submit.ringRestoreStatus != B_OK)
			status = submit.ringRestoreStatus;
	} else {
		submit.ringRestoreStatus = B_OK;
		submit.diagnosticFlags |= valleyview::kRenderSubmitRingRestored;
	}

	if (ringSafe && ringTouched && forcewakeAttempted && cacheStateCaptured) {
		submit.cacheRestoreStatus = RestoreRcsSubmissionCache(registers,
			submit);
		if (submit.cacheRestoreStatus == B_OK) {
			submit.diagnosticFlags |= valleyview::kRenderSubmitCacheRestored;
		} else {
			device.gpuFaulted = true;
			if (status == B_OK)
				status = submit.cacheRestoreStatus;
		}
	} else if (!ringTouched)
		submit.cacheRestoreStatus = B_OK;

	if (ppgttControlTouched) {
		submit.ppgttControlRestoreStatus
			= RestoreRcsPpgttControl(registers, submit);
		if (submit.ppgttControlRestoreStatus == B_OK) {
			submit.diagnosticFlags
				|= valleyview::kRenderSubmitPpgttControlRestored;
		} else {
			device.gpuFaulted = true;
			if (status == B_OK)
				status = submit.ppgttControlRestoreStatus;
		}
	} else
		submit.ppgttControlRestoreStatus = B_OK;

	ReadRcsRegisters(registers, submit.after);
	const valleyview::RcsRegisterSnapshot& expected
		= activeCaptured ? submit.active : submit.before;
	if (!valleyview::IsRcsRingRestored(expected, submit.after)) {
		ringSafe = false;
		submit.diagnosticFlags &= ~valleyview::kRenderSubmitRingRestored;
		submit.ringRestoreStatus = B_IO_ERROR;
		status = B_IO_ERROR;
	}
	if (forcewakeAttempted) {
		submit.forcewakeReleaseStatus
			= ReleaseForcewake(registers, forcewake);
		cleanupStatus = submit.forcewakeReleaseStatus;
		if (status == B_OK)
			status = cleanupStatus;
	} else
		submit.forcewakeReleaseStatus = B_OK;
	submit.wakeRestoreStatus = RestoreGtWake(registers,
		submit.globalBefore, gtWakeChanged);
	cleanupStatus = submit.wakeRestoreStatus;
	if (status == B_OK)
		status = cleanupStatus;

	ReadGpuRegisters(registers, submit.globalAfter);
	if (DisplaySignature(registers) == displaySignatureBefore) {
		submit.diagnosticFlags |= valleyview::kRenderSubmitDisplayUnchanged;
	} else
		status = B_IO_ERROR;
	if (BcsStateUnchanged(submit.globalBefore, submit.globalAfter)) {
		submit.diagnosticFlags |= valleyview::kRenderSubmitBcsUnchanged;
	} else if (status == B_OK)
		status = B_IO_ERROR;

	if (!ringSafe) {
		workspace.quarantined = true;
		device.renderMemoryQuarantined = true;
		device.gpuFaulted = true;
		dprintf("intel_valleyview: quarantined RCS submission memory after "
			"unsafe ring restoration\n");
	}
	mutex_unlock(&device.bcsLock);
	return status;
}


status_t
ExecuteRcsDiagnostic(ValleyViewDevice& device,
	ValleyViewRenderBuffer& buffer, ValleyViewRenderBuffer& shaderBuffer,
	valleyview::RcsDiagnostic& diagnostics)
{
	status_t status = PrepareRcsMemory(buffer, shaderBuffer, diagnostics);
	if (status != B_OK)
		return status;

	uint32* result = static_cast<uint32*>(buffer.address)
		+ valleyview::kRcsResultPage
			* valleyview::kPageSize / sizeof(uint32);
	const uint32* shaderSurface = reinterpret_cast<const uint32*>(
		static_cast<const uint8*>(shaderBuffer.address)
			+ diagnostics.shaderSurfaceOffset);
	const uint32* shaderGuard = reinterpret_cast<const uint32*>(
		static_cast<const uint8*>(shaderBuffer.address)
			+ diagnostics.shaderGuardOffset);
	valleyview::RcsShaderAnalysis shaderAnalysis = {};
	mutex_lock(&device.bcsLock);
	volatile uint8* registers = device.registers;
	valleyview::GpuDiagnostics forcewake = {};
	bool gtWakeChanged = false;
	bool forcewakeAttempted = false;
	bool cacheModesCaptured = false;
	bool activeCaptured = false;
	bool ringTouched = false;
	bool resetRcs = false;
	bool ringSafe = true;
	status_t cleanupStatus = B_OK;
	bool shaderValid = false;
	const uint32 requiredResults = valleyview::kRcsBatchMarkerVerified
		| valleyview::kRcsCompletionVerified
		| valleyview::kRcsTimestampVerified
		| valleyview::kRcsShaderOutputVerified
		| valleyview::kRcsShaderGuardVerified;

	ReadGpuRegisters(registers, diagnostics.globalBefore);
	ReadRcsRegisters(registers, diagnostics.before);
	diagnostics.displaySignatureBefore = DisplaySignature(registers);
	diagnostics.flags |= valleyview::kRcsSnapshotCaptured;
	diagnostics.stage = valleyview::kRcsStageSnapshot;
	if (!device.nativeActive || device.gpuFaulted
		|| device.p0MemoryQuarantined || device.renderMemoryQuarantined) {
		status = B_NO_INIT;
		goto cleanup;
	}

	status = EnableGtWake(registers, diagnostics.globalBefore, gtWakeChanged);
	if (status != B_OK)
		goto cleanup;
	forcewakeAttempted = true;
	status = AcquireForcewake(registers, forcewake);
	if (status != B_OK)
		goto cleanup;
	diagnostics.flags |= valleyview::kRcsForcewakeAcquired;
	diagnostics.stage = valleyview::kRcsStageForcewakeAcquired;
	diagnostics.cacheMode0Before = ReadMmio(registers,
		valleyview::kGen7CacheMode0);
	diagnostics.cacheMode1Before = ReadMmio(registers,
		valleyview::kGen7CacheMode1);
	cacheModesCaptured = true;

	ReadRcsRegisters(registers, diagnostics.active);
	activeCaptured = true;
	if (!valleyview::IsRcsRingAvailable(diagnostics.active)) {
		status = B_BUSY;
		goto cleanup;
	}
	diagnostics.flags |= valleyview::kRcsRingAvailable;
	diagnostics.timestampBefore = diagnostics.active.timestamp;

	ringTouched = true;
	status = PrepareRcsRing(registers, diagnostics.ringOffset,
		diagnostics.statusOffset);
	if (status != B_OK)
		goto cleanup;

	status = FlushRcsTlb(registers);
	if (status != B_OK)
		goto cleanup;
	diagnostics.flags |= valleyview::kRcsTlbFlushed;
	diagnostics.stage = valleyview::kRcsStageTlbFlushed;

	status = EnableRcsRing(registers, diagnostics.ringTailBytes);
	if (status != B_OK)
		goto cleanup;
	diagnostics.flags |= valleyview::kRcsRingStarted;
	diagnostics.stage = valleyview::kRcsStageRingStarted;
	diagnostics.shaderStage = valleyview::kRcsShaderStageRingStarted;
	// The shader reprograms media state and cache-mode workarounds. Reset RCS
	// after every attempt so no transient render state survives this diagnostic.
	resetRcs = true;

	status = WaitForRcsCompletion(result, diagnostics.completionMarker);
	diagnostics.timestampAfter
		= ReadMmio(registers, valleyview::kRcsRingTimestamp);
	memory_read_barrier();
	diagnostics.observedBatchMarker
		= result[valleyview::kRcsBatchMarkerOffset / sizeof(uint32)];
	diagnostics.observedTimestamp
		= result[valleyview::kRcsTimestampOffset / sizeof(uint32)];
	diagnostics.observedCompletionMarker
		= result[valleyview::kRcsCompletionOffset / sizeof(uint32)];
	if (diagnostics.observedBatchMarker == diagnostics.batchMarker)
		diagnostics.flags |= valleyview::kRcsBatchMarkerVerified;
	if (diagnostics.observedCompletionMarker == diagnostics.completionMarker)
		diagnostics.flags |= valleyview::kRcsCompletionVerified;
	if (valleyview::RcsTimestampInWindow(diagnostics.timestampBefore,
			diagnostics.observedTimestamp, diagnostics.timestampAfter)) {
		diagnostics.flags |= valleyview::kRcsTimestampVerified;
	}
	if (status != B_OK) {
		diagnostics.shaderStatus = status;
		resetRcs = true;
		goto cleanup;
	}
	diagnostics.stage = valleyview::kRcsStageCommandsCompleted;
	diagnostics.flags |= valleyview::kRcsShaderCommandsCompleted;
	diagnostics.shaderStage
		= valleyview::kRcsShaderStageCommandsCompleted;

	memory_read_barrier();
	shaderValid = valleyview::AnalyzeRcsShaderOutput(shaderSurface,
		diagnostics.shaderSurfaceBytes, shaderGuard,
		diagnostics.shaderGuardBytes, valleyview::kRcsShaderSentinel,
		shaderAnalysis);
	diagnostics.shaderZeroDwords = shaderAnalysis.zeroDwords;
	diagnostics.shaderSentinelDwords = shaderAnalysis.sentinelDwords;
	diagnostics.shaderUnexpectedDwords = shaderAnalysis.unexpectedDwords;
	diagnostics.shaderFirstChangedOffset
		= shaderAnalysis.firstChangedOffset;
	diagnostics.shaderLastChangedOffset = shaderAnalysis.lastChangedOffset;
	diagnostics.shaderFirstUnexpectedOffset
		= shaderAnalysis.firstUnexpectedOffset;
	diagnostics.shaderFirstUnexpectedValue
		= shaderAnalysis.firstUnexpectedValue;
	diagnostics.shaderGuardMismatchOffset
		= shaderAnalysis.guardMismatchOffset;
	diagnostics.shaderGuardObserved = shaderAnalysis.guardObserved;
	diagnostics.shaderChecksumAfter = shaderAnalysis.checksum;
	if (shaderAnalysis.zeroDwords
			== valleyview::kRcsShaderExpectedZeroDwords
		&& shaderAnalysis.sentinelDwords
			== valleyview::kRcsShaderExpectedSentinelDwords
		&& shaderAnalysis.unexpectedDwords == 0) {
		diagnostics.flags |= valleyview::kRcsShaderOutputVerified;
	}
	if (shaderAnalysis.guardMismatchOffset == UINT32_MAX)
		diagnostics.flags |= valleyview::kRcsShaderGuardVerified;
	diagnostics.shaderStatus = shaderValid ? B_OK : B_BAD_DATA;
	if (!shaderValid)
		status = diagnostics.shaderStatus;
	else {
		diagnostics.shaderStage
			= valleyview::kRcsShaderStageOutputVerified;
	}

	if ((diagnostics.flags & requiredResults) != requiredResults)
		status = B_BAD_DATA;
	if (status == B_OK)
		diagnostics.stage = valleyview::kRcsStageOutputVerified;

cleanup:
	if (status != B_OK && ringTouched) {
		ReadGpuRegisters(registers, diagnostics.globalFault);
		ReadRcsRegisters(registers, diagnostics.fault);
		diagnostics.flags |= valleyview::kRcsFaultCaptured;
	}
	if (ringTouched) {
		diagnostics.ringRestoreStatus = RestoreRcsRing(registers,
			diagnostics.active, resetRcs, diagnostics.resetStatus, ringSafe,
			diagnostics);
		if (ringSafe)
			diagnostics.flags |= valleyview::kRcsRingRestored;
		if (diagnostics.resetStatus != B_NO_INIT) {
			device.rcsResets++;
			if (diagnostics.resetStatus == B_OK)
				diagnostics.flags |= valleyview::kRcsResetPerformed;
		}
		if (diagnostics.ringRestoreStatus != B_OK)
			status = diagnostics.ringRestoreStatus;
	} else {
		diagnostics.ringRestoreStatus = B_OK;
		diagnostics.flags |= valleyview::kRcsRingRestored;
	}

	if (ringSafe && ringTouched && forcewakeAttempted && cacheModesCaptured) {
		status_t cacheStatus = RestoreRcsShaderCache(registers, diagnostics);
		if (cacheStatus == B_OK)
			diagnostics.flags |= valleyview::kRcsShaderCacheRestored;
		else {
			device.gpuFaulted = true;
			if (status == B_OK)
				status = cacheStatus;
			if (diagnostics.shaderStatus == B_OK)
				diagnostics.shaderStatus = cacheStatus;
		}
	}

	ReadRcsRegisters(registers, diagnostics.after);
	const valleyview::RcsRegisterSnapshot& expected
		= activeCaptured ? diagnostics.active : diagnostics.before;
	if (!valleyview::IsRcsRingRestored(expected,
			diagnostics.after)) {
		ringSafe = false;
		diagnostics.flags &= ~valleyview::kRcsRingRestored;
		diagnostics.ringRestoreStatus = B_IO_ERROR;
		status = B_IO_ERROR;
	}
	if (forcewakeAttempted) {
		diagnostics.forcewakeReleaseStatus
			= ReleaseForcewake(registers, forcewake);
		cleanupStatus = diagnostics.forcewakeReleaseStatus;
		if (status == B_OK)
			status = cleanupStatus;
	} else
		diagnostics.forcewakeReleaseStatus = B_OK;
	diagnostics.wakeRestoreStatus = RestoreGtWake(registers,
		diagnostics.globalBefore, gtWakeChanged);
	cleanupStatus = diagnostics.wakeRestoreStatus;
	if (status == B_OK)
		status = cleanupStatus;

	ReadGpuRegisters(registers, diagnostics.globalAfter);
	diagnostics.displaySignatureAfter = DisplaySignature(registers);
	if (diagnostics.displaySignatureAfter
			== diagnostics.displaySignatureBefore) {
		diagnostics.flags |= valleyview::kRcsDisplayUnchanged;
	} else
		status = B_IO_ERROR;
	if (BcsStateUnchanged(diagnostics.globalBefore,
			diagnostics.globalAfter)) {
		diagnostics.flags |= valleyview::kRcsBcsUnchanged;
	} else if (status == B_OK)
		status = B_IO_ERROR;

	if (!ringSafe) {
		buffer.quarantined = true;
		shaderBuffer.quarantined = true;
		device.renderMemoryQuarantined = true;
		device.gpuFaulted = true;
		dprintf("intel_valleyview: quarantined RCS diagnostic memory after "
			"unsafe ring restoration\n");
	}
	if (diagnostics.shaderStatus == B_NO_INIT)
		diagnostics.shaderStatus = status;
	mutex_unlock(&device.bcsLock);
	return status;
}


status_t
CaptureGpuDiagnostics(ValleyViewDevice& device,
	valleyview::GpuDiagnostics& diagnostics)
{
	memset(&diagnostics, 0, sizeof(diagnostics));
	diagnostics.header = valleyview::MakeAbiHeader(sizeof(diagnostics));
	diagnostics.status = B_NO_INIT;

	mutex_lock(&device.lock);
	mutex_lock(&device.bcsLock);
	diagnostics.generation = device.gpuTestGeneration;

	volatile uint8* registers = NULL;
	area_id registerArea = -1;
	bool gtWakeChanged = false;
	bool forcewakeAttempted = false;
	status_t cleanupStatus = B_OK;
	status_t status = MapGpuRegisters(device,
		B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA, registers, registerArea);
	if (status == B_OK)
		status = PrepareDiagnostics(device, registers, diagnostics, false);
	if (status == B_OK)
		status = EnableGtWake(registers, diagnostics.before, gtWakeChanged);
	if (status == B_OK) {
		diagnostics.flags |= valleyview::kGpuGtWakeAllowed;
		forcewakeAttempted = true;
		status = AcquireForcewake(registers, diagnostics);
	}
	if (status == B_OK)
		ReadGpuRegisters(registers, diagnostics.active);
	if (registers != NULL && forcewakeAttempted) {
		cleanupStatus = ReleaseForcewake(registers, diagnostics);
		if (status == B_OK)
			status = cleanupStatus;
	}
	if (registers != NULL) {
		cleanupStatus = RestoreGtWake(registers, diagnostics.before,
			gtWakeChanged);
		if (status == B_OK)
			status = cleanupStatus;
		ReadGpuRegisters(registers, diagnostics.after);
	}
	if (registerArea >= B_OK)
		delete_area(registerArea);

	diagnostics.status = status;
	mutex_unlock(&device.bcsLock);
	mutex_unlock(&device.lock);
	return status;
}


status_t
RunGpuSelfTest(ValleyViewDevice& device,
	valleyview::GpuDiagnostics& diagnostics)
{
	const uint32 command = diagnostics.command;
	memset(&diagnostics, 0, sizeof(diagnostics));
	diagnostics.header = valleyview::MakeAbiHeader(sizeof(diagnostics));
	diagnostics.command = command;
	diagnostics.status = B_NO_INIT;

	mutex_lock(&device.lock);
	diagnostics.generation = ++device.gpuTestGeneration;
	if (device.openCount != 1 || device.snapshot.adoptionStatus != B_OK
		|| device.gpuFaulted) {
		diagnostics.status = B_BUSY;
		mutex_unlock(&device.lock);
		return diagnostics.status;
	}
	mutex_lock(&device.bcsLock);

	const bigtime_t started = system_time();
	volatile uint8* registers = NULL;
	area_id registerArea = -1;
	area_id testArea = -1;
	uint32* memory = NULL;
	phys_addr_t physical = 0;
	bool gtWakeChanged = false;
	bool ptesInstalled = false;
	bool ringStarted = false;
	bool forcewakeAttempted = false;
	bool resetBcs = false;
	bool ringSafe = true;
	bool mappingsSafe = true;
	status_t cleanupStatus = B_OK;
	status_t status = MapGpuRegisters(device,
		B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA, registers, registerArea);
	if (status != B_OK)
		goto cleanup;

	status = PrepareDiagnostics(device, registers, diagnostics, true);
	if (status != B_OK)
		goto cleanup;

	status = CreateTestMemory(testArea, memory, physical);
	if (status != B_OK)
		goto cleanup;
	diagnostics.testPhysical = physical;
	diagnostics.flags |= valleyview::kGpuMemoryAllocated;
	diagnostics.stage = valleyview::kGpuStageMemoryAllocated;

	status = BuildTestMemory(memory, diagnostics.ggttOffset, diagnostics);
	if (status != B_OK)
		goto cleanup;

	ptesInstalled = true;
	mappingsSafe = false;
	status = InstallTestPtes(registers, physical, diagnostics);
	if (status != B_OK)
		goto cleanup;
	diagnostics.flags |= valleyview::kGpuGgttInstalled;
	diagnostics.stage = valleyview::kGpuStageGgttInstalled;

	status = EnableGtWake(registers, diagnostics.before, gtWakeChanged);
	if (status != B_OK)
		goto cleanup;
	diagnostics.flags |= valleyview::kGpuGtWakeAllowed;

	forcewakeAttempted = true;
	status = AcquireForcewake(registers, diagnostics);
	if (status != B_OK)
		goto cleanup;
	ReadGpuRegisters(registers, diagnostics.active);
	diagnostics.stage = valleyview::kGpuStageForcewakeAcquired;

	if ((diagnostics.active.bcsControl & valleyview::kRingValid) != 0
		|| (diagnostics.active.bcsMiMode & valleyview::kRingStop) != 0
		|| (diagnostics.active.bcsMode
			& valleyview::kRingPpgttEnable) != 0
		|| (diagnostics.active.bcsHead & valleyview::kRingAddressMask)
			!= (diagnostics.active.bcsTail & valleyview::kRingAddressMask)) {
		status = B_BUSY;
		goto cleanup;
	}
	diagnostics.flags |= valleyview::kGpuRingAvailable;

	ringStarted = true;
	ringSafe = false;
	status = StartBcsRing(registers, diagnostics.ggttOffset,
		diagnostics.ggttOffset
			+ valleyview::kGpuStatusPage * valleyview::kPageSize,
		diagnostics.ringTailBytes);
	if (status != B_OK)
		goto cleanup;
	diagnostics.flags |= valleyview::kGpuRingStarted;
	diagnostics.stage = valleyview::kGpuStageRingStarted;

	status = WaitForBcsCompletion(memory, valleyview::kGpuStatusPage,
		diagnostics.completionMarker);
	if (status != B_OK) {
		resetBcs = true;
		goto cleanup;
	}
	diagnostics.flags |= valleyview::kGpuCommandsCompleted;
	diagnostics.stage = valleyview::kGpuStageCommandsCompleted;

	memory_read_barrier();
	if (VerifyPage(memory + valleyview::kGpuSourcePage
			* valleyview::kPageSize / sizeof(uint32), kSourceSentinel,
			diagnostics.sourceMismatchOffset,
			diagnostics.sourceObserved)) {
		diagnostics.flags |= valleyview::kGpuFillVerified;
	} else {
		status = B_BAD_DATA;
		goto cleanup;
	}
	if (VerifyPage(memory + valleyview::kGpuDestinationPage
			* valleyview::kPageSize / sizeof(uint32), kDestinationSentinel,
			diagnostics.destinationMismatchOffset,
			diagnostics.destinationObserved)) {
		diagnostics.flags |= valleyview::kGpuCopyVerified;
	} else {
		status = B_BAD_DATA;
		goto cleanup;
	}
	diagnostics.stage = valleyview::kGpuStageBuffersVerified;

cleanup:
	if (registers != NULL && ringStarted) {
		cleanupStatus = RestoreBcsRing(registers, diagnostics.active,
			resetBcs);
		if (resetBcs && cleanupStatus == B_OK)
			diagnostics.flags |= valleyview::kGpuBcsReset;
		if (cleanupStatus == B_OK)
			diagnostics.flags |= valleyview::kGpuRingRestored;
		ringSafe = cleanupStatus == B_OK;
		if (status == B_OK)
			status = cleanupStatus;
	}

	if (registers != NULL && ptesInstalled && ringSafe) {
		WriteTestPtes(registers, diagnostics.ggttOffset,
			diagnostics.pteBefore);
		ReadTestPtes(registers, diagnostics.ggttOffset, diagnostics.pteAfter);
		bool restored = true;
		for (uint32 index = 0; index < valleyview::kGpuTestPageCount;
				index++) {
			if (diagnostics.pteAfter[index] != diagnostics.pteBefore[index])
				restored = false;
		}
		if (restored)
			diagnostics.flags |= valleyview::kGpuGgttRestored;
		if (restored)
			mappingsSafe = true;
		else if (status == B_OK)
			status = B_IO_ERROR;
	}

	if (registers != NULL) {
		diagnostics.displaySignatureAfter = DisplaySignature(registers);
		if (diagnostics.displaySignatureAfter
				== diagnostics.displaySignatureBefore) {
			diagnostics.flags |= valleyview::kGpuDisplayUnchanged;
		} else if (status == B_OK)
			status = B_IO_ERROR;
	}

	if (registers != NULL && forcewakeAttempted) {
		cleanupStatus = ReleaseForcewake(registers, diagnostics);
		if (status == B_OK)
			status = cleanupStatus;
	}
	if (registers != NULL) {
		cleanupStatus = RestoreGtWake(registers, diagnostics.before,
			gtWakeChanged);
		if (status == B_OK)
			status = cleanupStatus;
		ReadGpuRegisters(registers, diagnostics.after);
	}

	if ((!ringSafe || !mappingsSafe) && testArea >= B_OK) {
		device.gpuFaulted = true;
		device.gpuTestArea = testArea;
		testArea = -1;
	}
	if (testArea >= B_OK)
		delete_area(testArea);
	if (registerArea >= B_OK)
		delete_area(registerArea);

	if (status == B_OK
		&& (diagnostics.flags & (valleyview::kGpuFillVerified
				| valleyview::kGpuCopyVerified
				| valleyview::kGpuDisplayUnchanged
				| valleyview::kGpuGgttRestored
				| valleyview::kGpuRingRestored))
			== (valleyview::kGpuFillVerified | valleyview::kGpuCopyVerified
				| valleyview::kGpuDisplayUnchanged
				| valleyview::kGpuGgttRestored
				| valleyview::kGpuRingRestored)) {
		diagnostics.stage = valleyview::kGpuStageRestored;
	}

	const bigtime_t elapsed = system_time() - started;
	diagnostics.elapsedUs = elapsed > UINT32_MAX
		? UINT32_MAX : static_cast<uint32>(elapsed);
	diagnostics.status = status;
	mutex_unlock(&device.bcsLock);
	mutex_unlock(&device.lock);
	return status;
}


status_t
InitializeBcsRuntime(ValleyViewDevice& device)
{
	if (!device.nativeActive || device.p0Private == NULL
		|| device.gpuFaulted || device.p0MemoryQuarantined) {
		return B_NO_INIT;
	}
	mutex_lock(&device.bcsLock);
	if (!device.nativeActive || device.p0Private == NULL
		|| device.gpuFaulted || device.p0MemoryQuarantined) {
		mutex_unlock(&device.bcsLock);
		return B_NO_INIT;
	}

	uint32* memory = static_cast<uint32*>(device.p0Private);
	uint32* ring = memory
		+ (device.p0Layout.ring - device.p0Layout.cursor) / sizeof(uint32);
	uint32* source = memory
		+ (device.p0Layout.testSource - device.p0Layout.cursor)
			/ sizeof(uint32);
	uint32* destination = memory
		+ (device.p0Layout.testDestination - device.p0Layout.cursor)
			/ sizeof(uint32);
	uint32* statusPage = memory
		+ (device.p0Layout.status - device.p0Layout.cursor) / sizeof(uint32);

	memset(ring, 0, valleyview::kPageSize);
	for (uint32 index = 0; index < valleyview::kPageSize / sizeof(uint32);
			index++) {
		source[index] = kSourceSentinel;
		destination[index] = kDestinationSentinel;
		statusPage[index] = 0;
	}
	const size_t count = valleyview::BuildBcsSelfTestCommands(ring,
		valleyview::kPageSize / sizeof(uint32), device.p0Layout.testSource,
		device.p0Layout.testDestination, device.p0Layout.status,
		valleyview::kGpuTestPattern, valleyview::kGpuCompletionMarker);
	if (count != valleyview::kBcsSelfTestCommandCount) {
		mutex_unlock(&device.bcsLock);
		return B_BAD_DATA;
	}
	memory_write_barrier();

	status_t status = SubmitBcsCommandsLocked(device,
		static_cast<uint32>(count * sizeof(uint32)),
		valleyview::kGpuCompletionMarker);
	if (status == B_OK) {
		uint32 offset = 0;
		uint32 observed = 0;
		memory_read_barrier();
		if (!VerifyPage(source, kSourceSentinel, offset, observed)
			|| !VerifyPage(destination, kDestinationSentinel, offset,
				observed)) {
			status = B_BAD_DATA;
		}
	}

	device.bcsStatus = status;
	device.bcsReady = status == B_OK;
	mutex_unlock(&device.bcsLock);
	return status;
}


status_t
QuiesceBcsRuntime(ValleyViewDevice& device)
{
	mutex_lock(&device.bcsLock);
	device.bcsReady = false;
	if (device.registers == NULL) {
		mutex_unlock(&device.bcsLock);
		return B_NO_INIT;
	}

	volatile uint8* registers = device.registers;
	valleyview::GpuDiagnostics diagnostics = {};
	ReadGpuRegisters(registers, diagnostics.before);
	bool gtWakeChanged = false;
	bool forcewakeAttempted = false;
	status_t cleanupStatus = B_OK;
	status_t status = EnableGtWake(registers, diagnostics.before,
		gtWakeChanged);
	if (status == B_OK) {
		forcewakeAttempted = true;
		status = AcquireForcewake(registers, diagnostics);
	}
	if (status == B_OK)
		status = ResetBcs(registers);
	if (status == B_OK)
		status = WriteGt(registers, valleyview::kRingControl, 0);
	if (status == B_OK)
		status = WriteGt(registers, valleyview::kRingStart, 0);
	if (status == B_OK)
		status = WriteGt(registers, valleyview::kRingHws, 0);
	if (status == B_OK)
		status = WriteGt(registers, valleyview::kRingHead, 0);
	if (status == B_OK)
		status = WriteGt(registers, valleyview::kRingTail, 0);
	if (status == B_OK) {
		ReadMmio(registers, valleyview::kRingControl);
		ReadGpuRegisters(registers, diagnostics.active);
		if (!valleyview::IsBcsRingQuiesced(diagnostics.active))
			status = B_IO_ERROR;
	}

	if (forcewakeAttempted) {
		cleanupStatus = ReleaseForcewake(registers, diagnostics);
		if (status == B_OK)
			status = cleanupStatus;
	}
	cleanupStatus = RestoreGtWake(registers, diagnostics.before,
		gtWakeChanged);
	if (status == B_OK)
		status = cleanupStatus;
	device.bcsStatus = status;
	mutex_unlock(&device.bcsLock);
	return status;
}


status_t
SubmitBcsPresent(ValleyViewDevice& device, uint32 sourceOffset,
	uint32 destinationOffset)
{
	if (sourceOffset != device.p0Layout.framebuffer
		|| (destinationOffset != device.p0Layout.scanout[0]
			&& destinationOffset != device.p0Layout.scanout[1])) {
		return B_BAD_VALUE;
	}

	mutex_lock(&device.bcsLock);
	if (!device.nativeActive || !device.bcsReady
		|| device.p0Private == NULL || device.gpuFaulted
		|| device.p0MemoryQuarantined) {
		mutex_unlock(&device.bcsLock);
		return B_NO_INIT;
	}
	device.bcsPresentRequests++;

	uint32* ring = static_cast<uint32*>(device.p0Private)
		+ (device.p0Layout.ring - device.p0Layout.cursor) / sizeof(uint32);
	memset(ring, 0, valleyview::kPageSize);
	size_t count = 0;
	status_t status = valleyview::AppendBcsCopy(ring,
		valleyview::kPageSize / sizeof(uint32), count, sourceOffset,
		destinationOffset, valleyview::kP0BytesPerRow, 0, 0, 0, 0,
		valleyview::kP0Width - 1, valleyview::kP0Height - 1)
		? B_OK : B_BUFFER_OVERFLOW;

	const uint32 marker
		= 0xb3000000u | (++device.bcsSequence & 0x0fffffff);
	if (status == B_OK
		&& !valleyview::AppendBcsCompletion(ring,
			valleyview::kPageSize / sizeof(uint32), count, marker)) {
		status = B_BUFFER_OVERFLOW;
	}
	if (status == B_OK) {
		memory_write_barrier();
		status = SubmitBcsCommandsLocked(device,
			static_cast<uint32>(count * sizeof(uint32)), marker);
	}
	mutex_unlock(&device.bcsLock);
	return status;
}


status_t
SubmitBcsRenderCopy(ValleyViewDevice& device, uint32 sourceOffset,
	uint32 sourceStride, const valleyview::RenderDirectPresent& request)
{
	mutex_lock(&device.bcsLock);
	if (!device.nativeActive || !device.bcsReady
		|| device.p0Private == NULL || device.gpuFaulted
		|| device.p0MemoryQuarantined || device.renderMemoryQuarantined) {
		mutex_unlock(&device.bcsLock);
		return B_NO_INIT;
	}

	uint32* ring = static_cast<uint32*>(device.p0Private)
		+ (device.p0Layout.ring - device.p0Layout.cursor) / sizeof(uint32);
	memset(ring, 0, valleyview::kPageSize);
	size_t count = 0;
	status_t status = B_OK;
	for (uint32 index = 0; index < request.rectCount; index++) {
		const valleyview::RenderPresentRect& rect = request.rects[index];
		if (!valleyview::AppendBcsCopyPitches(ring,
				valleyview::kPageSize / sizeof(uint32), count,
				sourceOffset, device.p0Layout.framebuffer, sourceStride,
				valleyview::kP0BytesPerRow, rect.sourceLeft, rect.sourceTop,
				rect.destinationLeft, rect.destinationTop, rect.width,
				rect.height)) {
			status = B_BUFFER_OVERFLOW;
			break;
		}
	}

	const uint32 marker
		= 0xb5000000u | (++device.bcsSequence & 0x0fffffff);
	if (status == B_OK
		&& !valleyview::AppendBcsCompletion(ring,
			valleyview::kPageSize / sizeof(uint32), count, marker)) {
		status = B_BUFFER_OVERFLOW;
	}
	if (status == B_OK) {
		memory_write_barrier();
		status = SubmitBcsCommandsLocked(device,
			static_cast<uint32>(count * sizeof(uint32)), marker);
	}
	mutex_unlock(&device.bcsLock);
	return status;
}


status_t
SubmitBcsFill(ValleyViewDevice& device,
	const valleyview::BcsFillRequest& request)
{
	if (request.count == 0 || request.count > valleyview::kBcsMaxOperations)
		return B_BAD_VALUE;
	for (uint32 index = 0; index < request.count; index++) {
		const valleyview::BcsFillRect& rect = request.rects[index];
		if (rect.left > rect.right || rect.top > rect.bottom
			|| rect.right >= valleyview::kP0Width
			|| rect.bottom >= valleyview::kP0Height) {
			return B_BAD_VALUE;
		}
	}

	mutex_lock(&device.lock);
	if (!device.nativeActive || device.framebuffer == NULL
		|| device.p0Private == NULL || device.gpuFaulted) {
		mutex_unlock(&device.lock);
		return B_NO_INIT;
	}
	mutex_lock(&device.bcsLock);
	device.bcsFillRequests++;

	status_t status = B_NO_INIT;
	if (device.bcsReady) {
		uint32* ring = static_cast<uint32*>(device.p0Private)
			+ (device.p0Layout.ring - device.p0Layout.cursor)
				/ sizeof(uint32);
		memset(ring, 0, valleyview::kPageSize);
		size_t count = 0;
		status = B_OK;
		for (uint32 index = 0; index < request.count; index++) {
			const valleyview::BcsFillRect& rect = request.rects[index];
			if (!valleyview::AppendBcsFill(ring,
				valleyview::kPageSize / sizeof(uint32), count,
				device.p0Layout.framebuffer, valleyview::kP0BytesPerRow,
				request.color, rect.left, rect.top, rect.right,
				rect.bottom)) {
				status = B_BUFFER_OVERFLOW;
				break;
			}
		}

		const uint32 marker
			= 0xb1000000u | (++device.bcsSequence & 0x0fffffff);
		if (status == B_OK
			&& !valleyview::AppendBcsCompletion(ring,
				valleyview::kPageSize / sizeof(uint32), count, marker)) {
			status = B_BUFFER_OVERFLOW;
		}
		if (status == B_OK) {
			memory_write_barrier();
			status = SubmitBcsCommandsLocked(device,
				static_cast<uint32>(count * sizeof(uint32)), marker);
		}
	}
	if (status != B_OK && !device.gpuFaulted) {
		CpuFillLocked(device, request);
		device.cpuFillFallbacks++;
		status = B_OK;
	}
	mutex_unlock(&device.bcsLock);
	mutex_unlock(&device.lock);
	return status;
}


status_t
SubmitBcsBlit(ValleyViewDevice& device,
	const valleyview::BcsBlitRequest& request)
{
	if (request.count == 0 || request.count > valleyview::kBcsMaxOperations)
		return B_BAD_VALUE;
	for (uint32 index = 0; index < request.count; index++) {
		const valleyview::BcsBlitRect& rect = request.rects[index];
		const uint32 sourceRight = rect.sourceLeft + rect.width;
		const uint32 sourceBottom = rect.sourceTop + rect.height;
		const uint32 destinationRight
			= rect.destinationLeft + rect.width;
		const uint32 destinationBottom
			= rect.destinationTop + rect.height;
		if (sourceRight >= valleyview::kP0Width
			|| sourceBottom >= valleyview::kP0Height
			|| destinationRight >= valleyview::kP0Width
			|| destinationBottom >= valleyview::kP0Height) {
			return B_BAD_VALUE;
		}
	}

	mutex_lock(&device.lock);
	if (!device.nativeActive || device.framebuffer == NULL
		|| device.p0Private == NULL || device.gpuFaulted) {
		mutex_unlock(&device.lock);
		return B_NO_INIT;
	}
	mutex_lock(&device.bcsLock);
	device.bcsBlitRequests++;

	status_t status = B_NOT_SUPPORTED;
	if (device.bcsReady) {
		uint32* ring = static_cast<uint32*>(device.p0Private)
			+ (device.p0Layout.ring - device.p0Layout.cursor)
				/ sizeof(uint32);
		memset(ring, 0, valleyview::kPageSize);
		size_t count = 0;
		status = B_OK;
		for (uint32 index = 0; index < request.count; index++) {
			const valleyview::BcsBlitRect& rect = request.rects[index];
			const uint32 sourceRight = rect.sourceLeft + rect.width;
			const uint32 sourceBottom = rect.sourceTop + rect.height;
			const uint32 destinationRight
				= rect.destinationLeft + rect.width;
			const uint32 destinationBottom
				= rect.destinationTop + rect.height;
			const bool overlap = rect.sourceLeft <= destinationRight
				&& rect.destinationLeft <= sourceRight
				&& rect.sourceTop <= destinationBottom
				&& rect.destinationTop <= sourceBottom;
			if (overlap
				|| !valleyview::AppendBcsCopy(ring,
					valleyview::kPageSize / sizeof(uint32), count,
					device.p0Layout.framebuffer,
					device.p0Layout.framebuffer,
					valleyview::kP0BytesPerRow, rect.sourceLeft,
					rect.sourceTop, rect.destinationLeft,
					rect.destinationTop, rect.width, rect.height)) {
				status = overlap ? B_NOT_SUPPORTED : B_BUFFER_OVERFLOW;
				break;
			}
		}

		const uint32 marker
			= 0xb2000000u | (++device.bcsSequence & 0x0fffffff);
		if (status == B_OK
			&& !valleyview::AppendBcsCompletion(ring,
				valleyview::kPageSize / sizeof(uint32), count, marker)) {
			status = B_BUFFER_OVERFLOW;
		}
		if (status == B_OK) {
			memory_write_barrier();
			status = SubmitBcsCommandsLocked(device,
				static_cast<uint32>(count * sizeof(uint32)), marker);
		}
	}
	if (status != B_OK && !device.gpuFaulted) {
		CpuBlitLocked(device, request);
		device.cpuBlitFallbacks++;
		status = B_OK;
	}
	mutex_unlock(&device.bcsLock);
	mutex_unlock(&device.lock);
	return status;
}
