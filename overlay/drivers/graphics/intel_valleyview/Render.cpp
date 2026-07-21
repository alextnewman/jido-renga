// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot

#include "Driver.h"

#include <common/intel_valleyview/RenderMemoryCore.h>

#include <KernelExport.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <team.h>
#include <vm/vm.h>


namespace {

void
LockRenderDevice(ValleyViewDevice& device)
{
	mutex_lock(&device.lock);
	mutex_lock(&device.renderLock);
}


void
UnlockRenderDevice(ValleyViewDevice& device)
{
	mutex_unlock(&device.renderLock);
	mutex_unlock(&device.lock);
}


ValleyViewRenderBuffer*
FindRenderBuffer(ValleyViewClient& client, uint32 handle)
{
	for (ValleyViewRenderBuffer* buffer = client.buffers; buffer != NULL;
			buffer = buffer->next) {
		if (buffer->handle == handle)
			return buffer;
	}
	return NULL;
}


uint32
AllocateRenderHandle(ValleyViewClient& client)
{
	for (uint32 attempt = 0;
			attempt <= valleyview::kRenderMaxClientBuffers; attempt++) {
		client.nextHandle++;
		if (client.nextHandle == 0)
			client.nextHandle++;
		if (FindRenderBuffer(client, client.nextHandle) == NULL)
			return client.nextHandle;
	}
	return 0;
}


status_t
BuildPhysicalPageList(ValleyViewRenderBuffer& buffer)
{
	physical_entry* entries = static_cast<physical_entry*>(
		malloc(buffer.pageCount * sizeof(physical_entry)));
	if (entries == NULL)
		return B_NO_MEMORY;

	uint32 entryCount = buffer.pageCount;
	status_t status = get_memory_map_etc(B_SYSTEM_TEAM, buffer.address,
		buffer.size, entries, &entryCount);
	if (status != B_OK) {
		free(entries);
		return status;
	}

	uint32 page = 0;
	uint64 covered = 0;
	for (uint32 index = 0; index < entryCount; index++) {
		const physical_entry& entry = entries[index];
		if (entry.size == 0
			|| (entry.address & valleyview::kPageMask) != 0
			|| (entry.size & valleyview::kPageMask) != 0
			|| entry.address >= 0x100000000ull
			|| entry.size > 0x100000000ull - entry.address
			|| entry.size > buffer.size - covered) {
			status = B_BAD_DATA;
			break;
		}

		for (uint64 offset = 0; offset < entry.size;
				offset += valleyview::kPageSize) {
			if (page >= buffer.pageCount) {
				status = B_BAD_DATA;
				break;
			}
			buffer.physicalPages[page++]
				= entry.address + offset;
		}
		if (status != B_OK)
			break;
		covered += entry.size;
	}
	free(entries);

	return status == B_OK && covered == buffer.size
			&& page == buffer.pageCount
		? B_OK : B_BAD_DATA;
}


status_t
AllocateRenderMemory(ValleyViewRenderBuffer& buffer)
{
	char name[B_OS_NAME_LENGTH];
	if (buffer.handle == 0)
		strlcpy(name, "intel_valleyview RCS diagnostic", sizeof(name));
	else {
		snprintf(name, sizeof(name), "intel_valleyview render BO %" B_PRIu32,
			buffer.handle);
	}
	buffer.address = NULL;
	buffer.area = create_area(name, &buffer.address, B_ANY_KERNEL_ADDRESS,
		buffer.size, B_32_BIT_FULL_LOCK,
		B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA);
	if (buffer.area < B_OK)
		return buffer.area;

	buffer.physicalPages = static_cast<uint64*>(
		malloc(buffer.pageCount * sizeof(uint64)));
	buffer.savedPtes = static_cast<uint32*>(
		malloc(buffer.pageCount * sizeof(uint32)));
	if (buffer.physicalPages == NULL || buffer.savedPtes == NULL)
		return B_NO_MEMORY;

	status_t status = BuildPhysicalPageList(buffer);
	if (status != B_OK)
		return status;

	memset(buffer.address, 0, buffer.size);
	memory_write_barrier();
	return B_OK;
}


status_t
ReleaseRenderMapping(ValleyViewRenderBuffer& buffer)
{
	if (buffer.mappingArea < B_OK)
		return B_OK;

	status_t status = vm_change_clones_to_null_areas(buffer.area);
	if (status != B_OK)
		return status;

	status = vm_delete_area(buffer.mappingTeam, buffer.mappingArea, true);
	if (status == B_BAD_VALUE || status == B_BAD_TEAM_ID)
		status = B_OK;
	if (status == B_OK) {
		buffer.mappingArea = -1;
		buffer.mappingTeam = -1;
	}
	return status;
}


status_t
ReleaseRenderBuffer(ValleyViewDevice& device,
	ValleyViewRenderBuffer* buffer)
{
	status_t status = ReleaseRenderMapping(*buffer);
	if (status != B_OK)
		buffer->quarantined = true;

	if (!buffer->quarantined) {
		status = UnbindRenderBufferGgtt(device, *buffer);
		if (status == B_OK) {
			status = delete_area(buffer->area);
			if (status != B_OK)
				buffer->quarantined = true;
		}
	}

	if (buffer->quarantined) {
		if (status == B_OK)
			status = B_NOT_ALLOWED;
		device.renderMemoryQuarantined = true;
		dprintf("intel_valleyview: quarantined render BO %" B_PRIu32
			" area %" B_PRId32 " after unsafe teardown (%" B_PRId32 ")\n",
			buffer->handle, buffer->area, status);
	}

	free(buffer->savedPtes);
	free(buffer->physicalPages);
	free(buffer);
	return status;
}


status_t
CreateInternalRenderBuffer(ValleyViewDevice& device, uint64 size,
	ValleyViewRenderBuffer*& buffer, valleyview::RcsDiagnostic& diagnostics)
{
	buffer = static_cast<ValleyViewRenderBuffer*>(
		calloc(1, sizeof(ValleyViewRenderBuffer)));
	if (buffer == NULL)
		return B_NO_MEMORY;

	buffer->area = -1;
	buffer->mappingArea = -1;
	buffer->mappingTeam = -1;
	buffer->size = size;
	buffer->pageCount = static_cast<uint32>(size / valleyview::kPageSize);
	buffer->flags = valleyview::kRenderBufferCpuCached;
	buffer->ggttOffset = valleyview::kInvalidRenderGgttOffset;
	buffer->domain = valleyview::kRenderDomainCpu;

	status_t status = AllocateRenderMemory(*buffer);
	if (status == B_OK) {
		diagnostics.flags |= valleyview::kRcsMemoryAllocated;
		diagnostics.stage = valleyview::kRcsStageMemoryAllocated;
		status = BindRenderBufferGgtt(device, *buffer);
	}
	if (status == B_OK) {
		diagnostics.flags |= valleyview::kRcsGgttBound;
		diagnostics.stage = valleyview::kRcsStageGgttBound;
	}
	if (status != B_OK) {
		if (buffer->ggttOffset != valleyview::kInvalidRenderGgttOffset)
			UnbindRenderBufferGgtt(device, *buffer);
		if (buffer->area >= B_OK && !buffer->quarantined)
			delete_area(buffer->area);
		free(buffer->savedPtes);
		free(buffer->physicalPages);
		free(buffer);
		buffer = NULL;
	}
	return status;
}


status_t
DestroyInternalRenderBuffer(ValleyViewDevice& device,
	ValleyViewRenderBuffer*& buffer, status_t& ggttStatus,
	uint32* observedPtes)
{
	if (buffer == NULL) {
		ggttStatus = B_OK;
		return B_OK;
	}

	ggttStatus = buffer->quarantined
		? B_NOT_ALLOWED
		: UnbindRenderBufferGgtt(device, *buffer, observedPtes);
	status_t status = ReleaseRenderBuffer(device, buffer);
	buffer = NULL;
	return ggttStatus != B_OK ? ggttStatus : status;
}


bool
FindRenderTestMismatch(const uint32* words, uint32 count, uint32 seed,
	bool destinationPattern, uint32& mismatchOffset, uint32& observed)
{
	for (uint32 index = 0; index < count; index++) {
		const uint32 expected = destinationPattern
			? valleyview::RenderMemoryTestDestinationWord(index, seed)
			: valleyview::RenderMemoryTestWord(index, seed);
		if (words[index] != expected) {
			mismatchOffset = index * sizeof(uint32);
			observed = words[index];
			return true;
		}
	}
	return false;
}

} // namespace


status_t
CreateRenderBuffer(ValleyViewClient& client,
	valleyview::RenderBufferCreate& request)
{
	uint64 size;
	if (!valleyview::NormalizeRenderBufferSize(request.requestedSize, size)
		|| request.flags != valleyview::kRenderBufferCpuCached) {
		return B_BAD_VALUE;
	}

	ValleyViewDevice& device = *client.device;
	LockRenderDevice(device);
	if (!device.nativeActive || device.registers == NULL || device.gpuFaulted
		|| device.p0MemoryQuarantined || device.renderMemoryQuarantined) {
		UnlockRenderDevice(device);
		return B_NO_INIT;
	}
	if (client.bufferCount >= valleyview::kRenderMaxClientBuffers
		|| size > valleyview::kRenderMaxClientBytes - client.allocatedBytes) {
		UnlockRenderDevice(device);
		return B_NO_MEMORY;
	}

	ValleyViewRenderBuffer* buffer = static_cast<ValleyViewRenderBuffer*>(
		calloc(1, sizeof(ValleyViewRenderBuffer)));
	if (buffer == NULL) {
		UnlockRenderDevice(device);
		return B_NO_MEMORY;
	}
	buffer->area = -1;
	buffer->mappingArea = -1;
	buffer->mappingTeam = -1;
	buffer->handle = AllocateRenderHandle(client);
	buffer->size = size;
	buffer->pageCount = static_cast<uint32>(size / valleyview::kPageSize);
	buffer->flags = request.flags;
	buffer->ggttOffset = valleyview::kInvalidRenderGgttOffset;
	buffer->domain = valleyview::kRenderDomainCpu;
	if (buffer->handle == 0) {
		free(buffer);
		UnlockRenderDevice(device);
		return B_NO_MEMORY;
	}

	status_t status = AllocateRenderMemory(*buffer);
	if (status == B_OK)
		status = BindRenderBufferGgtt(device, *buffer);
	if (status != B_OK) {
		if (buffer->ggttOffset != valleyview::kInvalidRenderGgttOffset)
			UnbindRenderBufferGgtt(device, *buffer);
		if (buffer->area >= B_OK && !buffer->quarantined)
			delete_area(buffer->area);
		free(buffer->savedPtes);
		free(buffer->physicalPages);
		free(buffer);
		UnlockRenderDevice(device);
		return status;
	}

	buffer->next = client.buffers;
	client.buffers = buffer;
	client.bufferCount++;
	client.allocatedBytes += size;
	request.handle = buffer->handle;
	request.size = buffer->size;
	request.gpuOffset = buffer->ggttOffset;
	UnlockRenderDevice(device);
	return B_OK;
}


status_t
MapRenderBuffer(ValleyViewClient& client, valleyview::RenderBufferMap& request)
{
	ValleyViewDevice& device = *client.device;
	LockRenderDevice(device);
	ValleyViewRenderBuffer* buffer = FindRenderBuffer(client, request.handle);
	if (buffer == NULL || buffer->quarantined) {
		UnlockRenderDevice(device);
		return B_BAD_VALUE;
	}
	if (buffer->mappingArea >= B_OK) {
		UnlockRenderDevice(device);
		return B_BUSY;
	}
	if (!device.nativeActive || device.gpuFaulted
		|| device.p0MemoryQuarantined || device.renderMemoryQuarantined) {
		UnlockRenderDevice(device);
		return B_NO_INIT;
	}

	void* address = NULL;
	const team_id team = team_get_current_team_id();
	area_id area = vm_clone_area(team,
		"intel_valleyview render mapping", &address, B_ANY_ADDRESS,
		B_READ_AREA | B_WRITE_AREA | B_KERNEL_AREA, 0, buffer->area, true);
	if (area < B_OK) {
		UnlockRenderDevice(device);
		return area;
	}
	buffer->mappingArea = area;
	buffer->mappingTeam = team;
	request.flags = buffer->flags;
	request.area = area;
	request.address = reinterpret_cast<uint64>(address);
	request.size = buffer->size;
	UnlockRenderDevice(device);
	return B_OK;
}


status_t
DiscardRenderBufferMapping(ValleyViewClient& client, uint32 handle,
	area_id area)
{
	ValleyViewDevice& device = *client.device;
	LockRenderDevice(device);
	ValleyViewRenderBuffer* buffer = FindRenderBuffer(client, handle);
	if (buffer == NULL) {
		UnlockRenderDevice(device);
		return B_OK;
	}
	if (buffer->mappingArea != area) {
		UnlockRenderDevice(device);
		return B_BAD_VALUE;
	}

	status_t status = ReleaseRenderMapping(*buffer);
	if (status != B_OK) {
		buffer->quarantined = true;
		device.renderMemoryQuarantined = true;
	}
	UnlockRenderDevice(device);
	return status;
}


status_t
CloseRenderBuffer(ValleyViewClient& client, uint32 handle)
{
	ValleyViewDevice& device = *client.device;
	LockRenderDevice(device);
	ValleyViewRenderBuffer** link = &client.buffers;
	while (*link != NULL && (*link)->handle != handle)
		link = &(*link)->next;
	if (*link == NULL) {
		UnlockRenderDevice(device);
		return B_BAD_VALUE;
	}

	ValleyViewRenderBuffer* buffer = *link;
	*link = buffer->next;
	client.bufferCount--;
	client.allocatedBytes -= buffer->size;
	status_t status = ReleaseRenderBuffer(device, buffer);
	UnlockRenderDevice(device);
	return status;
}


status_t
SetRenderBufferDomain(ValleyViewClient& client,
	valleyview::RenderBufferSetDomain& request)
{
	ValleyViewDevice& device = *client.device;
	LockRenderDevice(device);
	ValleyViewRenderBuffer* buffer = FindRenderBuffer(client, request.handle);
	if (buffer == NULL || buffer->quarantined
		|| !valleyview::CanTransitionRenderBufferDomain(buffer->domain,
			request.domain, buffer->flags)) {
		UnlockRenderDevice(device);
		return B_BAD_VALUE;
	}
	if (!device.nativeActive || device.gpuFaulted
		|| device.p0MemoryQuarantined || device.renderMemoryQuarantined) {
		UnlockRenderDevice(device);
		return B_NO_INIT;
	}

	request.previousDomain = buffer->domain;
	if (buffer->domain != request.domain) {
		if (request.domain == valleyview::kRenderDomainBcs)
			memory_write_barrier();
		else
			memory_read_barrier();
		buffer->domain = request.domain;
	}
	UnlockRenderDevice(device);
	return B_OK;
}


status_t
RunRenderMemoryTest(ValleyViewClient& client,
	valleyview::RenderMemoryTest& test)
{
	const uint32 sourceHandle = test.sourceHandle;
	const uint32 destinationHandle = test.destinationHandle;
	const uint32 seed = test.seed;
	test.stage = valleyview::kRenderMemoryTestNone;
	test.status = B_NO_INIT;
	test.completionMarker = 0;
	test.mismatchOffset = UINT32_MAX;
	test.observed = 0;
	test.elapsedUs = 0;

	if (sourceHandle == 0 || destinationHandle == 0
		|| sourceHandle == destinationHandle) {
		test.status = B_BAD_VALUE;
		return test.status;
	}

	ValleyViewDevice& device = *client.device;
	LockRenderDevice(device);
	const bigtime_t started = system_time();
	ValleyViewRenderBuffer* source = FindRenderBuffer(client, sourceHandle);
	ValleyViewRenderBuffer* destination = FindRenderBuffer(client,
		destinationHandle);
	status_t status = B_OK;
	if (!device.nativeActive || device.gpuFaulted
		|| device.p0MemoryQuarantined || device.renderMemoryQuarantined) {
		status = B_NO_INIT;
	} else if (source == NULL || destination == NULL || source->quarantined
		|| destination->quarantined
		|| source->size < valleyview::kRenderMemoryTestBytes
		|| destination->size < valleyview::kRenderMemoryTestBytes
		|| source->domain != valleyview::kRenderDomainCpu
		|| destination->domain != valleyview::kRenderDomainCpu) {
		status = B_BAD_VALUE;
	}

	if (status == B_OK
		&& FindRenderTestMismatch(static_cast<const uint32*>(source->address),
			valleyview::kRenderMemoryTestWords, seed, false,
			test.mismatchOffset, test.observed)) {
		status = B_BAD_DATA;
	}
	if (status == B_OK
		&& FindRenderTestMismatch(
			static_cast<const uint32*>(destination->address),
			valleyview::kRenderMemoryTestWords, seed, true,
			test.mismatchOffset, test.observed)) {
		status = B_BAD_DATA;
	}
	if (status == B_OK) {
		test.stage = valleyview::kRenderMemoryTestInputsVerified;
		source->domain = valleyview::kRenderDomainBcs;
		destination->domain = valleyview::kRenderDomainBcs;
		memory_write_barrier();
		status = SubmitRenderBcsCopy(device, source->ggttOffset,
			destination->ggttOffset, test.completionMarker);
		if (status == B_OK)
			test.stage = valleyview::kRenderMemoryTestCommandsCompleted;
		memory_read_barrier();
		if (device.gpuFaulted) {
			source->quarantined = true;
			destination->quarantined = true;
			device.renderMemoryQuarantined = true;
			dprintf("intel_valleyview: quarantined render memory after "
				"unsafe BCS retirement\n");
		} else {
			source->domain = valleyview::kRenderDomainCpu;
			destination->domain = valleyview::kRenderDomainCpu;
		}
	}
	if (status == B_OK
		&& FindRenderTestMismatch(
			static_cast<const uint32*>(destination->address),
			valleyview::kRenderMemoryTestWords, seed, false,
			test.mismatchOffset, test.observed)) {
		status = B_BAD_DATA;
	}
	if (status == B_OK)
		test.stage = valleyview::kRenderMemoryTestOutputVerified;

	device.renderMemoryTests++;
	if (status != B_OK)
		device.renderMemoryFailures++;
	const bigtime_t elapsed = system_time() - started;
	test.elapsedUs = elapsed > 0 ? static_cast<uint64>(elapsed) : 0;
	test.status = status;
	UnlockRenderDevice(device);
	return status;
}


status_t
RunRcsDiagnostic(ValleyViewClient& client,
	valleyview::RcsDiagnostic& diagnostics)
{
	const uint32 command = diagnostics.command;
	memset(&diagnostics, 0, sizeof(diagnostics));
	diagnostics.header = valleyview::MakeRenderAbiHeader(sizeof(diagnostics));
	diagnostics.command = command;
	diagnostics.status = B_NO_INIT;
	diagnostics.resetStatus = B_NO_INIT;
	diagnostics.ringRestoreStatus = B_NO_INIT;
	diagnostics.ggttRestoreStatus = B_NO_INIT;
	diagnostics.forcewakeReleaseStatus = B_NO_INIT;
	diagnostics.wakeRestoreStatus = B_NO_INIT;
	if (command != valleyview::kRcsDiagnosticArm) {
		diagnostics.status = B_BAD_VALUE;
		return diagnostics.status;
	}

	ValleyViewDevice& device = *client.device;
	mutex_lock(&device.lock);
	mutex_lock(&device.renderLock);
	if (!device.nativeActive || device.registers == NULL || device.gpuFaulted
		|| device.p0MemoryQuarantined || device.renderMemoryQuarantined) {
		mutex_unlock(&device.renderLock);
		mutex_unlock(&device.lock);
		return diagnostics.status;
	}

	const bigtime_t started = system_time();
	ValleyViewRenderBuffer* buffer = NULL;
	status_t status = CreateInternalRenderBuffer(device,
		valleyview::kRcsTestBytes, buffer, diagnostics);
	if (status == B_OK) {
		diagnostics.ringOffset = buffer->ggttOffset
			+ valleyview::kRcsRingPage * valleyview::kPageSize;
		diagnostics.statusOffset = buffer->ggttOffset
			+ valleyview::kRcsStatusPage * valleyview::kPageSize;
		diagnostics.batchOffset = buffer->ggttOffset
			+ valleyview::kRcsBatchPage * valleyview::kPageSize;
		diagnostics.resultOffset = buffer->ggttOffset
			+ valleyview::kRcsResultPage * valleyview::kPageSize;
		for (uint32 page = 0; page < valleyview::kRcsTestPageCount;
				page++) {
			diagnostics.pteBefore[page] = buffer->savedPtes[page];
			if (!valleyview::EncodeBytPte(buffer->physicalPages[page], true,
					true, diagnostics.pteBound[page])) {
				status = B_BAD_DATA;
				break;
			}
		}
	}
	if (status == B_OK) {
		mutex_unlock(&device.renderLock);
		mutex_lock(&device.presentLock);
		status = ExecuteRcsDiagnostic(device, *buffer, diagnostics);
		mutex_unlock(&device.presentLock);
		mutex_lock(&device.renderLock);
	}

	status_t cleanupStatus = DestroyInternalRenderBuffer(device, buffer,
		diagnostics.ggttRestoreStatus, diagnostics.pteAfter);
	if ((diagnostics.flags & valleyview::kRcsGgttBound) != 0
		&& diagnostics.ggttRestoreStatus == B_OK) {
		diagnostics.flags |= valleyview::kRcsGgttRestored;
		if (diagnostics.stage >= valleyview::kRcsStageOutputVerified)
			diagnostics.stage = valleyview::kRcsStageRestored;
	}
	if (cleanupStatus != B_OK)
		status = cleanupStatus;

	device.rcsTests++;
	if (status == B_OK) {
		device.rcsReady = true;
		device.rcsStatus = B_OK;
	} else {
		device.rcsReady = false;
		device.rcsFailures++;
		device.rcsStatus = status;
	}
	const bigtime_t elapsed = system_time() - started;
	diagnostics.elapsedUs = elapsed > 0 ? static_cast<uint64>(elapsed) : 0;
	diagnostics.testCount = device.rcsTests;
	diagnostics.failureCount = device.rcsFailures;
	diagnostics.resetCount = device.rcsResets;
	diagnostics.status = status;
	mutex_unlock(&device.renderLock);
	mutex_unlock(&device.lock);
	return status;
}


void
DestroyRenderClient(ValleyViewClient& client)
{
	ValleyViewDevice& device = *client.device;
	LockRenderDevice(device);
	while (client.buffers != NULL) {
		ValleyViewRenderBuffer* buffer = client.buffers;
		client.buffers = buffer->next;
		client.bufferCount--;
		client.allocatedBytes -= buffer->size;
		status_t status = ReleaseRenderBuffer(device, buffer);
		if (status != B_OK) {
			dprintf("intel_valleyview: render client cleanup failed: %"
				B_PRId32 "\n", status);
		}
	}
	UnlockRenderDevice(device);
}
