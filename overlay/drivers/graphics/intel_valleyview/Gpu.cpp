// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot

#include "Driver.h"

#include <common/intel_valleyview/GpuCore.h>

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
CaptureGpuDiagnostics(ValleyViewDevice& device,
	valleyview::GpuDiagnostics& diagnostics)
{
	memset(&diagnostics, 0, sizeof(diagnostics));
	diagnostics.header = valleyview::MakeAbiHeader(sizeof(diagnostics));
	diagnostics.status = B_NO_INIT;

	mutex_lock(&device.lock);
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
	if (count != valleyview::kBcsSelfTestCommandCount)
		return B_BAD_DATA;
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
	return status;
}


status_t
QuiesceBcsRuntime(ValleyViewDevice& device)
{
	device.bcsReady = false;
	if (device.registers == NULL)
		return B_NO_INIT;

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
	mutex_unlock(&device.lock);
	return status;
}
