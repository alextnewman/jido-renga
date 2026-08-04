// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot

#include "Driver.h"

#include <common/intel_valleyview/PpgttCore.h>
#include <common/intel_valleyview/RenderCommandCore.h>
#include <common/intel_valleyview/RenderMemoryCore.h>
#include <common/intel_valleyview/RenderSubmitCore.h>

#include <KernelExport.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <team.h>
#include <vm/vm.h>


namespace {

constexpr uint32 kRenderMemoryLockFlags = B_DMA_IO | B_READ_DEVICE;

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
AllocateRenderMemory(ValleyViewRenderBuffer& buffer,
	const char* explicitName = NULL)
{
	char name[B_OS_NAME_LENGTH];
	if (explicitName != NULL)
		strlcpy(name, explicitName, sizeof(name));
	else if (buffer.handle == 0)
		strlcpy(name, "intel_valleyview RCS diagnostic", sizeof(name));
	else {
		snprintf(name, sizeof(name), "intel_valleyview render BO %" B_PRIu32,
			buffer.handle);
	}
	buffer.address = NULL;
	buffer.area = create_area(name, &buffer.address, B_ANY_KERNEL_ADDRESS,
		buffer.size, B_NO_LOCK,
		B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA);
	if (buffer.area < B_OK)
		return buffer.area;

	memset(buffer.address, 0, buffer.size);
	status_t status = lock_memory(buffer.address, buffer.size,
		kRenderMemoryLockFlags);
	if (status != B_OK) {
		delete_area(buffer.area);
		buffer.area = -1;
		buffer.address = NULL;
		return status;
	}

	buffer.physicalPages = static_cast<uint64*>(
		malloc(buffer.pageCount * sizeof(uint64)));
	buffer.savedPtes = static_cast<uint32*>(
		malloc(buffer.pageCount * sizeof(uint32)));
	if (buffer.physicalPages == NULL || buffer.savedPtes == NULL)
		status = B_NO_MEMORY;
	else
		status = BuildPhysicalPageList(buffer);
	if (status != B_OK) {
		free(buffer.savedPtes);
		free(buffer.physicalPages);
		buffer.savedPtes = NULL;
		buffer.physicalPages = NULL;
		unlock_memory(buffer.address, buffer.size, kRenderMemoryLockFlags);
		delete_area(buffer.area);
		buffer.area = -1;
		buffer.address = NULL;
		return status;
	}

	memory_write_barrier();
	buffer.resident = true;
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
ReleaseRenderBufferBacking(ValleyViewDevice& device,
	ValleyViewRenderBuffer* buffer)
{
	status_t status = ReleaseRenderMapping(*buffer);
	if (status != B_OK)
		buffer->quarantined = true;

	if (!buffer->quarantined) {
		status = UnbindRenderBufferGgtt(device, *buffer);
		if (status == B_OK && buffer->resident) {
			status = unlock_memory(buffer->address, buffer->size,
				kRenderMemoryLockFlags);
			if (status == B_OK)
				buffer->resident = false;
			else
				buffer->quarantined = true;
		}
		if (status == B_OK && buffer->area >= B_OK) {
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
FlushPpgttCacheRange(void* address, size_t byteCount)
{
	const size_t kCacheLineBytes = 64;
	if (address == NULL || byteCount == 0
		|| byteCount > valleyview::kPpgttDirectoryBytes) {
		return B_BAD_VALUE;
	}

	const addr_t base = reinterpret_cast<addr_t>(address);
	if (base > UINTPTR_MAX - byteCount)
		return B_BAD_VALUE;
	const addr_t first = base & ~(static_cast<addr_t>(kCacheLineBytes) - 1);
	const addr_t end = base + byteCount;
	for (addr_t line = first; line < end; line += kCacheLineBytes)
		__asm__ volatile("clflush (%0)" : : "r" (line) : "memory");
	__asm__ volatile("mfence" : : : "memory");
	return B_OK;
}


bool
HasPpgttResources(const ValleyViewPpgttState& state)
{
	return state.directoryBuffer != NULL && state.scratchBuffer != NULL
		&& state.bitmap != NULL;
}


uint32*
PpgttEntries(ValleyViewPpgttState& state)
{
	return HasPpgttResources(state)
		? static_cast<uint32*>(state.directoryBuffer->address) : NULL;
}


bool
GetPpgttScratchPte(ValleyViewPpgttState& state, uint32& scratchPte)
{
	return HasPpgttResources(state)
		&& state.scratchBuffer->pageCount == 1
		&& state.scratchBuffer->physicalPages != NULL
		&& valleyview::EncodePpgttDataPte(
			state.scratchBuffer->physicalPages[0], true, true, scratchPte);
}


void
QuarantinePpgtt(ValleyViewClient& client, ValleyViewRenderBuffer* buffer,
	const char* reason)
{
	ValleyViewPpgttState& state = client.ppgtt;
	state.ready = false;
	state.quarantined = true;
	if (state.directoryBuffer != NULL)
		state.directoryBuffer->quarantined = true;
	if (state.scratchBuffer != NULL)
		state.scratchBuffer->quarantined = true;
	if (state.submissionBuffer != NULL)
		state.submissionBuffer->quarantined = true;
	if (buffer != NULL)
		buffer->quarantined = true;
	client.device->renderMemoryQuarantined = true;
	dprintf("intel_valleyview: quarantined PPGTT memory after %s\n", reason);
}


status_t
MapRenderBufferPpgtt(ValleyViewClient& client,
	ValleyViewRenderBuffer& buffer)
{
	ValleyViewPpgttState& state = client.ppgtt;
	if (!HasPpgttResources(state) || state.quarantined
		|| buffer.ppgttOffset != valleyview::kInvalidRenderPpgttOffset
		|| buffer.physicalPages == NULL || buffer.pageCount == 0) {
		return B_BAD_VALUE;
	}

	uint32 firstPage;
	if (!valleyview::FindFreePpgttRun(state.bitmap,
			valleyview::kPpgttBitmapBytes, buffer.pageCount, 1, firstPage)) {
		return B_NO_MEMORY;
	}

	for (uint32 page = 0; page < buffer.pageCount; page++) {
		uint32 pte;
		if (!valleyview::EncodePpgttDataPte(buffer.physicalPages[page],
				true, true, pte)) {
			return B_BAD_DATA;
		}
	}

	uint32 scratchPte;
	if (!GetPpgttScratchPte(state, scratchPte))
		return B_BAD_DATA;
	uint32* const entries = PpgttEntries(state);
	for (uint32 page = 0; page < buffer.pageCount; page++) {
		if (entries[firstPage + page] != scratchPte) {
			QuarantinePpgtt(client, &buffer, "non-scratch free mapping");
			return B_BAD_DATA;
		}
	}
	for (uint32 page = 0; page < buffer.pageCount; page++) {
		if (!valleyview::EncodePpgttDataPte(buffer.physicalPages[page],
				true, true, entries[firstPage + page])) {
			return B_BAD_DATA;
		}
	}
	status_t status = FlushPpgttCacheRange(entries + firstPage,
		buffer.pageCount * sizeof(uint32));
	if (status == B_OK) {
		for (uint32 page = 0; page < buffer.pageCount; page++) {
			uint32 expected;
			if (!valleyview::EncodePpgttDataPte(buffer.physicalPages[page],
					true, true, expected)
				|| entries[firstPage + page] != expected) {
				status = B_IO_ERROR;
				break;
			}
		}
	}
	if (status != B_OK) {
		for (uint32 page = 0; page < buffer.pageCount; page++)
			entries[firstPage + page] = scratchPte;
		bool restored = FlushPpgttCacheRange(entries + firstPage,
			buffer.pageCount * sizeof(uint32)) == B_OK;
		for (uint32 page = 0; restored && page < buffer.pageCount; page++)
			restored = entries[firstPage + page] == scratchPte;
		if (!restored)
			QuarantinePpgtt(client, &buffer, "failed map rollback");
		return status;
	}

	for (uint32 page = 0; page < buffer.pageCount; page++) {
		if (!valleyview::SetPpgttBitmapBit(state.bitmap,
				valleyview::kPpgttBitmapBytes, firstPage + page)) {
			QuarantinePpgtt(client, &buffer, "invalid allocation bitmap");
			return B_BAD_DATA;
		}
	}
	buffer.ppgttOffset = firstPage * valleyview::kPpgttPageBytes;
	return B_OK;
}


status_t
ReplaceRenderBufferPpgttWithScratch(ValleyViewClient& client,
	ValleyViewRenderBuffer& buffer)
{
	ValleyViewPpgttState& state = client.ppgtt;
	const uint32 firstPage = buffer.ppgttOffset
		/ valleyview::kPpgttPageBytes;
	if (!buffer.resident || buffer.physicalPages == NULL
		|| !HasPpgttResources(state)
		|| !valleyview::ValidatePpgttVaRange(buffer.ppgttOffset, buffer.size)
		|| buffer.pageCount > valleyview::kPpgttPageCount - firstPage) {
		return B_BAD_VALUE;
	}

	uint32 scratchPte;
	if (!GetPpgttScratchPte(state, scratchPte))
		return B_BAD_DATA;
	uint32* const entries = PpgttEntries(state);
	for (uint32 page = 0; page < buffer.pageCount; page++) {
		bool allocated;
		uint32 expected;
		if (!valleyview::GetPpgttBitmapBit(state.bitmap,
				valleyview::kPpgttBitmapBytes, firstPage + page, allocated)
			|| !allocated
			|| !valleyview::EncodePpgttDataPte(buffer.physicalPages[page],
				true, true, expected)
			|| entries[firstPage + page] != expected) {
			QuarantinePpgtt(client, &buffer,
				"unexpected resident PTE during eviction");
			return B_IO_ERROR;
		}
	}

	for (uint32 page = 0; page < buffer.pageCount; page++)
		entries[firstPage + page] = scratchPte;
	status_t status = FlushPpgttCacheRange(entries + firstPage,
		buffer.pageCount * sizeof(uint32));
	for (uint32 page = 0; status == B_OK && page < buffer.pageCount; page++) {
		if (entries[firstPage + page] != scratchPte)
			status = B_IO_ERROR;
	}
	if (status != B_OK)
		QuarantinePpgtt(client, &buffer, "failed physical eviction mapping");
	return status;
}


status_t
RestoreRenderBufferPpgtt(ValleyViewClient& client,
	ValleyViewRenderBuffer& buffer)
{
	ValleyViewPpgttState& state = client.ppgtt;
	const uint32 firstPage = buffer.ppgttOffset
		/ valleyview::kPpgttPageBytes;
	if (buffer.physicalPages == NULL || !HasPpgttResources(state)
		|| !valleyview::ValidatePpgttVaRange(buffer.ppgttOffset, buffer.size)
		|| buffer.pageCount > valleyview::kPpgttPageCount - firstPage) {
		return B_BAD_VALUE;
	}

	uint32 scratchPte;
	if (!GetPpgttScratchPte(state, scratchPte))
		return B_BAD_DATA;
	uint32* const entries = PpgttEntries(state);
	for (uint32 page = 0; page < buffer.pageCount; page++) {
		bool allocated;
		uint32 ignored;
		if (!valleyview::GetPpgttBitmapBit(state.bitmap,
				valleyview::kPpgttBitmapBytes, firstPage + page, allocated)
			|| !allocated || entries[firstPage + page] != scratchPte
			|| !valleyview::EncodePpgttDataPte(buffer.physicalPages[page],
				true, true, ignored)) {
			QuarantinePpgtt(client, &buffer,
				"unexpected scratch PTE during reload");
			return B_IO_ERROR;
		}
	}

	for (uint32 page = 0; page < buffer.pageCount; page++) {
		valleyview::EncodePpgttDataPte(buffer.physicalPages[page],
			true, true, entries[firstPage + page]);
	}
	status_t status = FlushPpgttCacheRange(entries + firstPage,
		buffer.pageCount * sizeof(uint32));
	for (uint32 page = 0; status == B_OK && page < buffer.pageCount; page++) {
		uint32 expected;
		if (!valleyview::EncodePpgttDataPte(buffer.physicalPages[page],
				true, true, expected)
			|| entries[firstPage + page] != expected) {
			status = B_IO_ERROR;
		}
	}
	if (status == B_OK)
		return B_OK;

	for (uint32 page = 0; page < buffer.pageCount; page++)
		entries[firstPage + page] = scratchPte;
	bool restored = FlushPpgttCacheRange(entries + firstPage,
		buffer.pageCount * sizeof(uint32)) == B_OK;
	for (uint32 page = 0; restored && page < buffer.pageCount; page++)
		restored = entries[firstPage + page] == scratchPte;
	if (!restored)
		QuarantinePpgtt(client, &buffer, "failed physical reload rollback");
	return status;
}


status_t
ClearRenderBufferPpgtt(ValleyViewClient& client,
	ValleyViewRenderBuffer& buffer)
{
	if (buffer.ppgttOffset == valleyview::kInvalidRenderPpgttOffset)
		return B_OK;

	ValleyViewPpgttState& state = client.ppgtt;
	const uint32 firstPage = buffer.ppgttOffset
		/ valleyview::kPpgttPageBytes;
	if (!HasPpgttResources(state)
		|| !valleyview::ValidatePpgttVaRange(buffer.ppgttOffset, buffer.size)
		|| buffer.pageCount > valleyview::kPpgttPageCount - firstPage) {
		QuarantinePpgtt(client, &buffer, "invalid buffer mapping");
		return B_BAD_DATA;
	}

	uint32 scratchPte;
	if (!GetPpgttScratchPte(state, scratchPte)) {
		QuarantinePpgtt(client, &buffer, "invalid scratch page");
		return B_BAD_DATA;
	}
	uint32* const entries = PpgttEntries(state);
	for (uint32 page = 0; page < buffer.pageCount; page++) {
		bool allocated;
		if (!valleyview::GetPpgttBitmapBit(state.bitmap,
				valleyview::kPpgttBitmapBytes, firstPage + page, allocated)
			|| !allocated) {
			QuarantinePpgtt(client, &buffer, "unowned buffer mapping");
			return B_BAD_DATA;
		}
		uint32 expected = scratchPte;
		if (buffer.resident
			&& (buffer.physicalPages == NULL
				|| !valleyview::EncodePpgttDataPte(
					buffer.physicalPages[page], true, true, expected))) {
			QuarantinePpgtt(client, &buffer, "invalid resident backing");
			return B_BAD_DATA;
		}
		if (entries[firstPage + page] != expected) {
			QuarantinePpgtt(client, &buffer, "unexpected mapped PTE");
			return B_IO_ERROR;
		}
	}

	for (uint32 page = 0; page < buffer.pageCount; page++)
		entries[firstPage + page] = scratchPte;
	status_t status = FlushPpgttCacheRange(entries + firstPage,
		buffer.pageCount * sizeof(uint32));
	for (uint32 page = 0; status == B_OK && page < buffer.pageCount; page++) {
		if (entries[firstPage + page] != scratchPte)
			status = B_IO_ERROR;
	}
	if (status != B_OK) {
		QuarantinePpgtt(client, &buffer, "failed mapping restoration");
		return status;
	}

	for (uint32 page = 0; page < buffer.pageCount; page++) {
		if (!valleyview::ClearPpgttBitmapBit(state.bitmap,
				valleyview::kPpgttBitmapBytes, firstPage + page)) {
			QuarantinePpgtt(client, &buffer, "invalid free bitmap");
			return B_BAD_DATA;
		}
	}
	buffer.ppgttOffset = valleyview::kInvalidRenderPpgttOffset;
	return B_OK;
}


status_t
EvictRenderBufferBacking(ValleyViewClient& client,
	ValleyViewRenderBuffer& buffer)
{
	if (!valleyview::CanEvictRenderBuffer(buffer.resident,
			buffer.quarantined, buffer.queuedReferenceCount, buffer.domain,
			buffer.ggttOffset)
		|| buffer.ppgttOffset == valleyview::kInvalidRenderPpgttOffset) {
		return B_BUSY;
	}

	status_t status = ReplaceRenderBufferPpgttWithScratch(client, buffer);
	if (status != B_OK)
		return status;
	status = unlock_memory(buffer.address, buffer.size,
		kRenderMemoryLockFlags);
	if (status != B_OK) {
		QuarantinePpgtt(client, &buffer, "partial physical eviction");
		return status;
	}

	free(buffer.savedPtes);
	free(buffer.physicalPages);
	buffer.savedPtes = NULL;
	buffer.physicalPages = NULL;
	buffer.resident = false;
	client.residentBytes -= buffer.size;
	ValleyViewDevice& device = *client.device;
	device.renderPhysicalResidentBytes -= buffer.size;
	device.renderPhysicalEvictions++;
	return B_OK;
}


status_t
EnsureClientResidentBudget(ValleyViewClient& client, uint64 requiredBytes)
{
	if (requiredBytes > valleyview::kRenderResidentBudgetBytes)
		return B_NO_MEMORY;

	while (client.residentBytes
			> valleyview::kRenderResidentBudgetBytes - requiredBytes) {
		ValleyViewRenderBuffer* candidate = NULL;
		for (ValleyViewRenderBuffer* buffer = client.buffers;
				buffer != NULL; buffer = buffer->next) {
			if (!valleyview::CanEvictRenderBuffer(buffer->resident,
					buffer->quarantined, buffer->queuedReferenceCount,
					buffer->domain, buffer->ggttOffset)) {
				continue;
			}
			if (candidate == NULL
				|| buffer->residencySerial < candidate->residencySerial) {
				candidate = buffer;
			}
		}
		if (candidate == NULL)
			return B_NO_MEMORY;
		status_t status = EvictRenderBufferBacking(client, *candidate);
		if (status != B_OK)
			return status;
	}
	return B_OK;
}


status_t
MakeRenderBufferResident(ValleyViewClient& client,
	ValleyViewRenderBuffer& buffer)
{
	if (buffer.resident) {
		buffer.residencySerial = ++client.residencySerial;
		return B_OK;
	}
	if (buffer.quarantined
		|| buffer.ppgttOffset == valleyview::kInvalidRenderPpgttOffset) {
		return B_BAD_VALUE;
	}

	status_t status = EnsureClientResidentBudget(client, buffer.size);
	if (status != B_OK)
		return status;
	status = lock_memory(buffer.address, buffer.size,
		kRenderMemoryLockFlags);
	if (status != B_OK)
		return status;

	buffer.physicalPages = static_cast<uint64*>(
		malloc(buffer.pageCount * sizeof(uint64)));
	buffer.savedPtes = static_cast<uint32*>(
		malloc(buffer.pageCount * sizeof(uint32)));
	if (buffer.physicalPages == NULL || buffer.savedPtes == NULL)
		status = B_NO_MEMORY;
	else
		status = BuildPhysicalPageList(buffer);
	if (status == B_OK)
		status = RestoreRenderBufferPpgtt(client, buffer);
	if (status != B_OK) {
		if (buffer.quarantined) {
			buffer.resident = true;
			client.residentBytes += buffer.size;
			client.device->renderPhysicalResidentBytes += buffer.size;
			return status;
		}
		free(buffer.savedPtes);
		free(buffer.physicalPages);
		buffer.savedPtes = NULL;
		buffer.physicalPages = NULL;
		unlock_memory(buffer.address, buffer.size, kRenderMemoryLockFlags);
		return status;
	}

	buffer.resident = true;
	buffer.residencySerial = ++client.residencySerial;
	client.residentBytes += buffer.size;
	ValleyViewDevice& device = *client.device;
	device.renderPhysicalResidentBytes += buffer.size;
	if (device.renderPhysicalResidentBytes
			> device.renderPhysicalResidentMaxBytes) {
		device.renderPhysicalResidentMaxBytes
			= device.renderPhysicalResidentBytes;
	}
	device.renderPhysicalReloads++;
	return B_OK;
}


status_t
ReleaseClientRenderBuffer(ValleyViewClient& client,
	ValleyViewRenderBuffer* buffer)
{
	const bool resident = buffer->resident;
	const uint64 size = buffer->size;
	status_t status = B_OK;
	if (client.ppgtt.quarantined) {
		status = B_NOT_ALLOWED;
		buffer->quarantined = true;
	} else {
		status = ClearRenderBufferPpgtt(client, *buffer);
		if (status != B_OK)
			buffer->quarantined = true;
	}
	status_t backingStatus = ReleaseRenderBufferBacking(*client.device, buffer);
	if (resident && backingStatus == B_OK) {
		client.residentBytes -= size;
		client.device->renderPhysicalResidentBytes -= size;
	}
	return status != B_OK ? status : backingStatus;
}


status_t
AllocatePpgttBuffer(uint64 size, const char* name,
	ValleyViewRenderGgttEncoding encoding, uint32 alignmentPages,
	ValleyViewRenderBuffer*& buffer)
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
	buffer->ppgttOffset = valleyview::kInvalidRenderPpgttOffset;
	buffer->ggttAlignmentPages = alignmentPages;
	buffer->ggttEncoding = encoding;
	buffer->domain = valleyview::kRenderDomainCpu;

	status_t status = AllocateRenderMemory(*buffer, name);
	if (status != B_OK) {
		if (buffer->area >= B_OK)
			delete_area(buffer->area);
		free(buffer->savedPtes);
		free(buffer->physicalPages);
		free(buffer);
		buffer = NULL;
	}
	return status;
}


status_t
InitializePpgttTables(ValleyViewPpgttState& state)
{
	uint32 scratchPte;
	if (!GetPpgttScratchPte(state, scratchPte))
		return B_BAD_DATA;

	uint32* const entries = PpgttEntries(state);
	for (uint32 page = 0; page < valleyview::kPpgttPageCount; page++)
		entries[page] = scratchPte;
	status_t status = FlushPpgttCacheRange(entries,
		valleyview::kPpgttDirectoryBytes);
	for (uint32 page = 0;
			status == B_OK && page < valleyview::kPpgttPageCount; page++) {
		if (entries[page] != scratchPte)
			status = B_IO_ERROR;
	}
	return status;
}


void
InitializeRenderSubmitResult(valleyview::RenderSubmit& submit)
{
	const valleyview::RenderAbiHeader header = submit.header;
	const uint32 submitFlags = submit.submitFlags;
	const uint32 contextHandle = submit.contextHandle;
	const uint32 batchHandle = submit.batchHandle;
	const uint32 batchOffset = submit.batchOffset;
	const uint32 batchLength = submit.batchLength;
	const uint32 objectCount = submit.objectCount;
	uint32 objectHandles[valleyview::kRenderSubmitMaxObjects];
	memcpy(objectHandles, submit.objectHandles, sizeof(objectHandles));

	memset(&submit, 0, sizeof(submit));
	submit.header = header;
	submit.submitFlags = submitFlags;
	submit.contextHandle = contextHandle;
	submit.batchHandle = batchHandle;
	submit.batchOffset = batchOffset;
	submit.batchLength = batchLength;
	submit.objectCount = objectCount;
	memcpy(submit.objectHandles, objectHandles, sizeof(objectHandles));
	submit.status = B_NO_INIT;
	submit.parserFailingOffset
		= valleyview::kRenderCommandInvalidOffset;
	submit.parserFailingDword
		= valleyview::kRenderCommandInvalidDword;
	submit.resetStatus = B_NO_INIT;
	submit.persistentStatus = B_NO_INIT;
	submit.ringRestoreStatus = B_NO_INIT;
	submit.cacheRestoreStatus = B_NO_INIT;
	submit.ppgttControlRestoreStatus = B_NO_INIT;
	submit.forcewakeReleaseStatus = B_NO_INIT;
	submit.wakeRestoreStatus = B_NO_INIT;
}


status_t
PrepareRcsSubmissionWorkspace(ValleyViewRenderBuffer& workspace,
	const void* batchData, uint32 ppDirBase,
	valleyview::RenderSubmit& submit, bool preserveHardwareContext,
	bool switchContext = true, bool restoreInhibit = true)
{
	if (workspace.address == NULL
		|| workspace.size != valleyview::kRcsSubmitWorkspaceBytes
		|| batchData == NULL) {
		return B_BAD_VALUE;
	}

	uint8* const memory = static_cast<uint8*>(workspace.address);
	const size_t contextOffset
		= valleyview::kRcsSubmitContextPage * valleyview::kPageSize;
	// The inhibited switch still makes this the active hardware context image.
	// Keep its saved bytes intact until ownership is released or reset.
	memset(memory, 0, preserveHardwareContext
		? contextOffset : workspace.size);
	uint8* const batch = memory
		+ valleyview::kRcsSubmitBatchPage * valleyview::kPageSize;
	memcpy(batch, batchData, submit.batchLength);
	memory_write_barrier();
	submit.diagnosticFlags |= valleyview::kRenderSubmitBatchCopied;
	submit.stage = valleyview::kRenderSubmitStageBatchCopied;

	const valleyview::RenderCommandResult parser
		= valleyview::ParseRenderCommands(batch, submit.batchLength);
	submit.parserReason = parser.reason;
	submit.parserFailingOffset = parser.failingByteOffset;
	submit.parserFailingDword = parser.failingDword;
	submit.parsedCommandCount = parser.parsedCommandCount;
	submit.lriRegisterCount = parser.lriRegisterCount;
	submit.pipeControlCount = parser.pipeControlCount;
	submit.primitiveCount = parser.primitiveCount;
	if (parser.status != valleyview::kRenderCommandStatusAccepted)
		return B_NOT_ALLOWED;
	submit.diagnosticFlags |= valleyview::kRenderSubmitBatchAccepted;
	submit.stage = valleyview::kRenderSubmitStageBatchParsed;
	submit.ppDirBaseRequested = ppDirBase;
	submit.hardwareContextOffset = workspace.ggttOffset
		+ valleyview::kRcsSubmitContextPage * valleyview::kPageSize;

	uint32* const result = reinterpret_cast<uint32*>(memory
		+ valleyview::kRcsSubmitResultPage * valleyview::kPageSize);
	for (uint32 index = 0;
			index < valleyview::kPageSize / sizeof(uint32); index++) {
		result[index] = valleyview::kRcsResultSentinel;
	}
	uint32* const ring = reinterpret_cast<uint32*>(memory
		+ valleyview::kRcsSubmitRingPage * valleyview::kPageSize);
	const size_t ringCount = valleyview::BuildRcsSubmitRing(ring,
		valleyview::kPageSize / sizeof(uint32),
		workspace.ggttOffset
			+ valleyview::kRcsSubmitBatchPage * valleyview::kPageSize,
		workspace.ggttOffset
			+ valleyview::kRcsSubmitResultPage * valleyview::kPageSize,
		submit.hardwareContextOffset,
		ppDirBase,
		submit.completionMarker, switchContext, restoreInhibit);
	const size_t expectedRingCount = switchContext
		? valleyview::kRcsSubmitRingCommandCount
		: valleyview::kRcsSubmitRingNoSwitchCommandCount;
	if (ringCount != expectedRingCount)
		return B_BAD_DATA;
	submit.ringTailBytes
		= static_cast<uint32>(ringCount * sizeof(uint32));
	memory_write_barrier();
	return B_OK;
}


status_t
CreateInternalRenderBuffer(ValleyViewDevice& device, uint64 size,
	ValleyViewRenderBuffer*& buffer, valleyview::RcsDiagnostic& diagnostics,
	bool shader)
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
	buffer->ppgttOffset = valleyview::kInvalidRenderPpgttOffset;
	buffer->ggttAlignmentPages = 1;
	buffer->ggttEncoding = kValleyViewRenderGgttData;
	buffer->domain = valleyview::kRenderDomainCpu;

	status_t status = AllocateRenderMemory(*buffer);
	if (status == B_OK) {
		if (shader) {
			diagnostics.flags |= valleyview::kRcsShaderMemoryAllocated;
			diagnostics.shaderStage
				= valleyview::kRcsShaderStageMemoryAllocated;
		} else {
			diagnostics.flags |= valleyview::kRcsMemoryAllocated;
			diagnostics.stage = valleyview::kRcsStageMemoryAllocated;
		}
		status = BindRenderBufferGgtt(device, *buffer);
	}
	if (status == B_OK) {
		if (shader) {
			diagnostics.flags |= valleyview::kRcsShaderGgttBound;
			diagnostics.shaderStage = valleyview::kRcsShaderStageGgttBound;
		} else {
			diagnostics.flags |= valleyview::kRcsGgttBound;
			diagnostics.stage = valleyview::kRcsStageGgttBound;
		}
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
RecordInternalRenderBuffer(const ValleyViewRenderBuffer& buffer,
	uint32* savedPtes, uint32* boundPtes)
{
	for (uint32 page = 0; page < buffer.pageCount; page++) {
		savedPtes[page] = buffer.savedPtes[page];
		if (!valleyview::EncodeBytPte(buffer.physicalPages[page], true,
				true, boundPtes[page])) {
			return B_BAD_DATA;
		}
	}
	return B_OK;
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
	status_t status = ReleaseRenderBufferBacking(device, buffer);
	buffer = NULL;
	return ggttStatus != B_OK ? ggttStatus : status;
}


status_t
ReleasePpgttResources(ValleyViewClient& client)
{
	ValleyViewPpgttState& state = client.ppgtt;
	ValleyViewDevice& device = *client.device;
	state.ready = false;
	if (state.quarantined)
		return B_NOT_ALLOWED;
	if (state.directoryBuffer != NULL) {
		status_t status = UnbindRenderBufferGgtt(device,
			*state.directoryBuffer);
		if (status != B_OK) {
			QuarantinePpgtt(client, NULL, "unsafe directory GGTT restoration");
			return status;
		}
	}

	status_t status = B_OK;
	if (state.submissionBuffer != NULL) {
		ValleyViewRenderBuffer* buffer = state.submissionBuffer;
		state.submissionBuffer = NULL;
		status = ReleaseRenderBufferBacking(device, buffer);
	}
	if (state.directoryBuffer != NULL) {
		ValleyViewRenderBuffer* buffer = state.directoryBuffer;
		state.directoryBuffer = NULL;
		status_t directoryStatus = ReleaseRenderBufferBacking(device, buffer);
		if (status == B_OK)
			status = directoryStatus;
	}
	if (state.scratchBuffer != NULL) {
		ValleyViewRenderBuffer* buffer = state.scratchBuffer;
		state.scratchBuffer = NULL;
		status_t scratchStatus = ReleaseRenderBufferBacking(device, buffer);
		if (status == B_OK)
			status = scratchStatus;
	}
	free(state.bitmap);
	state.bitmap = NULL;
	state.ppDirBase = 0;
	state.quarantined = false;
	state.hardwareContextGeneration = 0;
	return status;
}


status_t
DestroyPpgttContextLocked(ValleyViewClient& client, uint32 handle)
{
	if (handle == 0 || client.contextHandle != handle)
		return B_BAD_VALUE;
	if (client.ppgtt.quarantined)
		return B_NOT_ALLOWED;

	client.ppgtt.ready = false;
	status_t status = B_OK;
	for (ValleyViewRenderBuffer* buffer = client.buffers; buffer != NULL;
			buffer = buffer->next) {
		status_t clearStatus = ClearRenderBufferPpgtt(client, *buffer);
		if (status == B_OK)
			status = clearStatus;
	}
	if (status != B_OK) {
		QuarantinePpgtt(client, NULL, "incomplete context teardown");
		return status;
	}

	status = ReleasePpgttResources(client);
	if (!HasPpgttResources(client.ppgtt))
		client.contextHandle = 0;
	return status;
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


ValleyViewRenderBuffer*
FindClientRenderBuffer(ValleyViewClient& client, uint32 handle)
{
	return FindRenderBuffer(client, handle);
}


status_t
EnsureRenderBufferResident(ValleyViewClient& client,
	ValleyViewRenderBuffer& buffer)
{
	return MakeRenderBufferResident(client, buffer);
}


void
ReleaseRenderQueueReferences(ValleyViewClient& client, const uint32* handles,
	uint32 count)
{
	if (handles == NULL)
		return;

	ValleyViewDevice& device = *client.device;
	LockRenderDevice(device);
	for (uint32 index = 0; index < count; index++) {
		ValleyViewRenderBuffer* buffer = FindRenderBuffer(client,
			handles[index]);
		if (buffer == NULL || buffer->queuedReferenceCount == 0)
			continue;
		buffer->queuedReferenceCount--;
		if (buffer->queuedReferenceCount != 0 || !buffer->closePending)
			continue;

		ValleyViewRenderBuffer** link = &client.buffers;
		while (*link != NULL && *link != buffer)
			link = &(*link)->next;
		if (*link == NULL)
			continue;
		*link = buffer->next;
		client.bufferCount--;
		client.allocatedBytes -= buffer->size;
		status_t status = ReleaseClientRenderBuffer(client, buffer);
		if (status != B_OK) {
			dprintf("intel_valleyview: deferred render BO close failed: %"
				B_PRId32 "\n", status);
		}
	}
	UnlockRenderDevice(device);
}


status_t
CreateRenderContext(ValleyViewClient& client,
	valleyview::RenderContextCreate& request)
{
	request.handle = 0;
	request.status = B_NO_INIT;
	request.addressBits = valleyview::kPpgttAddressBits;
	request.addressSpaceSize = valleyview::kPpgttVirtualAddressBytes;
	request.pageSize = valleyview::kPpgttPageBytes;
	request.ppDirBase = 0;
	if (request.flags != 0) {
		request.status = B_BAD_VALUE;
		return request.status;
	}

	ValleyViewDevice& device = *client.device;
	LockRenderDevice(device);
	status_t status = B_OK;
	if (!device.nativeActive || device.registers == NULL || device.gpuFaulted
		|| device.p0MemoryQuarantined || device.renderMemoryQuarantined) {
		status = B_NO_INIT;
	} else if (client.contextHandle != 0 || client.buffers != NULL) {
		status = B_BUSY;
	}
	if (status != B_OK) {
		request.status = status;
		UnlockRenderDevice(device);
		return status;
	}

	ValleyViewPpgttState& state = client.ppgtt;
	state.bitmap = static_cast<uint8*>(
		malloc(valleyview::kPpgttBitmapBytes));
	if (state.bitmap == NULL)
		status = B_NO_MEMORY;
	else {
		memset(state.bitmap, 0, valleyview::kPpgttBitmapBytes);
		if (!valleyview::SetPpgttBitmapBit(state.bitmap,
				valleyview::kPpgttBitmapBytes, 0)) {
			status = B_BAD_DATA;
		}
	}
	if (status == B_OK) {
		status = AllocatePpgttBuffer(valleyview::kPpgttPageBytes,
			"intel_valleyview PPGTT scratch", kValleyViewRenderGgttData, 1,
			state.scratchBuffer);
	}
	if (status == B_OK) {
		status = AllocatePpgttBuffer(valleyview::kPpgttDirectoryBytes,
			"intel_valleyview PPGTT directory",
			kValleyViewRenderGgttPpgttDirectory,
			valleyview::kPpgttDirectoryAlignment
				/ valleyview::kPpgttPageBytes,
			state.directoryBuffer);
	}
	if (status == B_OK)
		status = InitializePpgttTables(state);
	if (status == B_OK)
		status = BindRenderBufferGgtt(device, *state.directoryBuffer);
	if (status == B_OK) {
		status = AllocatePpgttBuffer(valleyview::kRcsSubmitWorkspaceBytes,
			"intel_valleyview persistent RCS context",
			kValleyViewRenderGgttData,
			valleyview::kPpgttDirectoryAlignment
				/ valleyview::kPpgttPageBytes,
			state.submissionBuffer);
	}
	if (status == B_OK)
		status = BindRenderBufferGgtt(device, *state.submissionBuffer);
	if (status == B_OK
		&& !valleyview::EncodePpgttDirectoryBase(
			state.directoryBuffer->ggttOffset, device.snapshot.gmadrSize,
			state.ppDirBase)) {
		status = B_BAD_DATA;
	}

	if (status == B_OK) {
		client.contextGeneration++;
		if (client.contextGeneration == 0)
			client.contextGeneration++;
		client.contextHandle = client.contextGeneration;
		state.ready = true;
		request.handle = client.contextHandle;
		request.ppDirBase = state.ppDirBase;
	} else {
		if (!state.quarantined) {
			status_t cleanupStatus = ReleasePpgttResources(client);
			if (cleanupStatus != B_OK)
				status = cleanupStatus;
		}
	}
	request.status = status;
	UnlockRenderDevice(device);
	return status;
}


status_t
DestroyRenderContext(ValleyViewClient& client,
	valleyview::RenderContextDestroy& request)
{
	request.status = B_NO_INIT;
	if (request.flags != 0 || request.handle == 0 || request.reserved != 0) {
		request.status = B_BAD_VALUE;
		return request.status;
	}

	ValleyViewDevice& device = *client.device;
	LockRenderDevice(device);
	mutex_lock(&device.renderQueueLock);
	const bool queueBusy = client.activeJob != NULL
		|| client.queueHead != NULL;
	mutex_unlock(&device.renderQueueLock);
	UnlockRenderDevice(device);
	if (queueBusy) {
		request.status = B_BUSY;
		return request.status;
	}
	request.status = ReleasePersistentRcsClient(client);
	if (request.status != B_OK)
		return request.status;

	LockRenderDevice(device);
	mutex_lock(&device.renderQueueLock);
	const bool becameBusy = client.activeJob != NULL
		|| client.queueHead != NULL;
	mutex_unlock(&device.renderQueueLock);
	request.status = becameBusy
		? B_BUSY : DestroyPpgttContextLocked(client, request.handle);
	UnlockRenderDevice(device);
	return request.status;
}


status_t
SubmitRenderCommands(ValleyViewClient& client,
	valleyview::RenderSubmit& submit, const void* immutableBatch,
	bool resetAfterSubmission)
{
	const uint32 enqueuedParserReason = submit.parserReason;
	const uint32 enqueuedCommandCount = submit.parsedCommandCount;
	const uint32 enqueuedPrimitiveCount = submit.primitiveCount;
	InitializeRenderSubmitResult(submit);
	if (immutableBatch != NULL) {
		submit.diagnosticFlags |= valleyview::kRenderSubmitBatchCopied
			| valleyview::kRenderSubmitBatchAccepted;
		submit.stage = valleyview::kRenderSubmitStageBatchParsed;
		submit.parserReason = enqueuedParserReason;
		submit.parsedCommandCount = enqueuedCommandCount;
		submit.primitiveCount = enqueuedPrimitiveCount;
	}
	const bigtime_t started = system_time();
	if (submit.submitFlags != 0
		|| !valleyview::ValidateRenderSubmitObjectHandles(
			submit.objectHandles, submit.objectCount, submit.batchHandle)) {
		submit.status = B_BAD_VALUE;
		return submit.status;
	}

	ValleyViewDevice& device = *client.device;
	if (resetAfterSubmission) {
		status_t ownershipStatus = ReleasePersistentRcsOwnership(device);
		if (ownershipStatus != B_OK) {
			submit.status = ownershipStatus;
			return ownershipStatus;
		}
	}
	LockRenderDevice(device);
	status_t status = B_OK;
	bool executed = false;
	const bool persistent = !resetAfterSubmission;
	ValleyViewRenderBuffer* workspace = NULL;
	ValleyViewRenderBuffer* objects[valleyview::kRenderSubmitMaxObjects] = {};
	if (!device.nativeActive || device.registers == NULL || device.gpuFaulted
		|| device.p0MemoryQuarantined || device.renderMemoryQuarantined
		|| !device.rcsReady) {
		status = B_NO_INIT;
	} else if (client.contextHandle == 0
		|| submit.contextHandle != client.contextHandle
		|| !client.ppgtt.ready || client.ppgtt.quarantined) {
		status = B_BAD_VALUE;
	}

	ValleyViewRenderBuffer* batchBuffer = NULL;
	for (uint32 index = 0; status == B_OK && index < submit.objectCount;
			index++) {
		ValleyViewRenderBuffer* buffer = FindRenderBuffer(client,
			submit.objectHandles[index]);
		if (buffer == NULL || buffer->quarantined
			|| (buffer->closePending && immutableBatch == NULL)
			|| buffer->ppgttOffset
				== valleyview::kInvalidRenderPpgttOffset
			|| buffer->domain != valleyview::kRenderDomainCpu) {
			status = B_BAD_VALUE;
			break;
		}
		objects[index] = buffer;
		status = MakeRenderBufferResident(client, *buffer);
		if (status != B_OK)
			break;
		buffer->domain = valleyview::kRenderDomainRcs;
		if (buffer->handle == submit.batchHandle)
			batchBuffer = buffer;
	}
	if (status == B_OK
		&& (batchBuffer == NULL
			|| !valleyview::ValidateRenderSubmitBatchRange(batchBuffer->size,
				submit.batchOffset, submit.batchLength))) {
		status = B_BAD_VALUE;
	}
	if (status == B_OK) {
		submit.diagnosticFlags |= valleyview::kRenderSubmitObjectsOwned;
		submit.stage = valleyview::kRenderSubmitStageObjectsOwned;
	}

	if (status == B_OK && persistent) {
		workspace = client.ppgtt.submissionBuffer;
		if (workspace == NULL || workspace->quarantined
			|| workspace->ggttOffset
				== valleyview::kInvalidRenderGgttOffset) {
			status = B_NO_INIT;
		}
	}
	if (status == B_OK && !persistent) {
		status = AllocatePpgttBuffer(valleyview::kRcsSubmitWorkspaceBytes,
			"intel_valleyview RCS submission",
			kValleyViewRenderGgttData,
			valleyview::kPpgttDirectoryAlignment
				/ valleyview::kPpgttPageBytes,
			workspace);
	}
	if (status == B_OK && !persistent)
		status = BindRenderBufferGgtt(device, *workspace);
	if (status == B_OK) {
		const void* batchData = immutableBatch != NULL
			? immutableBatch
			: static_cast<const uint8*>(batchBuffer->address)
				+ submit.batchOffset;
		submit.workspaceOffset = workspace->ggttOffset;
		submit.workspacePages = workspace->pageCount;
		submit.diagnosticFlags |= valleyview::kRenderSubmitWorkspaceBound;
		submit.stage = valleyview::kRenderSubmitStageWorkspaceBound;
		device.rcsSubmitSequence++;
		if (device.rcsSubmitSequence == 0)
			device.rcsSubmitSequence++;
		submit.sequence = device.rcsSubmitSequence;
		submit.completionMarker = valleyview::kRcsSubmitMarkerBase
			| (submit.sequence & 0x00ffffff);
		const bool contextInitialized = persistent
			&& device.rcsPersistentGeneration != 0
			&& client.ppgtt.hardwareContextGeneration
				== device.rcsPersistentGeneration;
		const bool switchContext = !persistent
			|| !device.rcsPersistentState.owned
			|| device.rcsPersistentState.owner != client.persistentId;
		status = PrepareRcsSubmissionWorkspace(*workspace, batchData,
			client.ppgtt.ppDirBase, submit, contextInitialized,
			switchContext, !contextInitialized);
	}
	if (status == B_OK && !device.rcsSubmissionReady
		&& (submit.batchLength != sizeof(uint32)
			|| submit.parsedCommandCount != 1
			|| submit.lriRegisterCount != 0
			|| submit.pipeControlCount != 0
			|| submit.primitiveCount != 0)) {
		status = B_NOT_ALLOWED;
	}

	if (status == B_OK) {
		memory_write_barrier();
		mutex_unlock(&device.renderLock);
		mutex_lock(&device.presentLock);
		executed = true;
		status = persistent
			? ExecutePersistentRcsSubmission(client, *workspace,
				client.ppgtt.ppDirBase, submit)
			: ExecuteRcsSubmission(device, *workspace,
				client.ppgtt.ppDirBase, submit, true);
		mutex_unlock(&device.presentLock);
		mutex_lock(&device.renderLock);
	}

	if (executed && (device.gpuFaulted || device.renderMemoryQuarantined)) {
		for (ValleyViewRenderBuffer* buffer = client.buffers; buffer != NULL;
				buffer = buffer->next) {
			buffer->quarantined = true;
		}
		QuarantinePpgtt(client, NULL, "unsafe RCS submission retirement");
	} else {
		for (uint32 index = 0; index < submit.objectCount; index++) {
			if (objects[index] != NULL)
				objects[index]->domain = valleyview::kRenderDomainCpu;
		}
		memory_read_barrier();
	}

	if (workspace != NULL && !persistent) {
		status_t workspaceStatus = ReleaseRenderBufferBacking(device,
			workspace);
		workspace = NULL;
		if (workspaceStatus == B_OK) {
			submit.diagnosticFlags
				|= valleyview::kRenderSubmitWorkspaceRestored;
		} else if (status == B_OK)
			status = workspaceStatus;
	}
	if (workspace != NULL && persistent && status == B_OK) {
		client.ppgtt.hardwareContextGeneration
			= device.rcsPersistentGeneration;
		submit.diagnosticFlags
			|= valleyview::kRenderSubmitWorkspaceRestored;
	}
	if (persistent && status != B_OK)
		client.ppgtt.hardwareContextGeneration = 0;

	if (executed) {
		device.rcsSubmissions++;
		if (status == B_OK)
			device.rcsSubmissionReady = true;
		else
			device.rcsSubmissionFailures++;
	}
	if (status == B_OK) {
		submit.stage = valleyview::kRenderSubmitStageRestored;
	} else if (device.gpuFaulted || device.renderMemoryQuarantined)
		device.rcsReady = false;
	if (device.gpuFaulted || device.renderMemoryQuarantined)
		device.rcsSubmissionReady = false;
	const bigtime_t elapsed = system_time() - started;
	submit.elapsedUs = elapsed > 0 ? static_cast<uint64>(elapsed) : 0;
	submit.status = status;
	UnlockRenderDevice(device);
	return status;
}


status_t
CreateRenderBuffer(ValleyViewClient& client,
	valleyview::RenderBufferCreate& request)
{
	request.renderAddress = 0;
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
	status_t status = EnsureClientResidentBudget(client, size);
	if (status != B_OK) {
		UnlockRenderDevice(device);
		return status;
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
	buffer->ppgttOffset = valleyview::kInvalidRenderPpgttOffset;
	buffer->ggttAlignmentPages = 1;
	buffer->ggttEncoding = kValleyViewRenderGgttData;
	buffer->domain = valleyview::kRenderDomainCpu;
	if (buffer->handle == 0) {
		free(buffer);
		UnlockRenderDevice(device);
		return B_NO_MEMORY;
	}

	status = AllocateRenderMemory(*buffer);
	if (status == B_OK) {
		buffer->residencySerial = ++client.residencySerial;
		client.residentBytes += size;
		device.renderPhysicalResidentBytes += size;
		if (device.renderPhysicalResidentBytes
				> device.renderPhysicalResidentMaxBytes) {
			device.renderPhysicalResidentMaxBytes
				= device.renderPhysicalResidentBytes;
		}
	}
	if (status == B_OK && client.ppgtt.ready)
		status = MapRenderBufferPpgtt(client, *buffer);
	if (status != B_OK) {
		status_t cleanupStatus = ReleaseClientRenderBuffer(client, buffer);
		if (cleanupStatus != B_OK)
			status = cleanupStatus;
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
	request.renderAddress = client.ppgtt.ready
		? buffer->ppgttOffset : 0;
	UnlockRenderDevice(device);
	return B_OK;
}


status_t
MapRenderBuffer(ValleyViewClient& client, valleyview::RenderBufferMap& request)
{
	ValleyViewDevice& device = *client.device;
	LockRenderDevice(device);
	ValleyViewRenderBuffer* buffer = FindRenderBuffer(client, request.handle);
	if (buffer == NULL || buffer->quarantined || buffer->closePending) {
		UnlockRenderDevice(device);
		return B_BAD_VALUE;
	}
	if (buffer->queuedReferenceCount != 0) {
		UnlockRenderDevice(device);
		return B_BUSY;
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
	if (buffer->queuedReferenceCount != 0) {
		buffer->closePending = true;
		UnlockRenderDevice(device);
		return B_OK;
	}
	*link = buffer->next;
	client.bufferCount--;
	client.allocatedBytes -= buffer->size;
	status_t status = ReleaseClientRenderBuffer(client, buffer);
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
	if (buffer == NULL || buffer->quarantined || buffer->closePending
		|| !valleyview::CanTransitionRenderBufferDomain(buffer->domain,
			request.domain, buffer->flags)) {
		UnlockRenderDevice(device);
		return B_BAD_VALUE;
	}
	if (buffer->queuedReferenceCount != 0) {
		UnlockRenderDevice(device);
		return B_BUSY;
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
SubmitRenderDirectPresent(ValleyViewClient& client,
	valleyview::RenderDirectPresent& request, bool queued)
{
	request.status = B_NO_INIT;
	request.elapsedUs = 0;
	const uint32 expectedFlags = queued
		? valleyview::kRenderDirectPresentAsynchronous : 0;
	if (request.flags != expectedFlags
		|| !valleyview::ValidateRenderDirectPresentGeometry(request)) {
		request.status = B_BAD_VALUE;
		return request.status;
	}
	const uint64 sourceBytes
		= valleyview::RenderDirectPresentSourceBytes(request);

	const bigtime_t started = system_time();
	ValleyViewDevice& device = *client.device;
	mutex_lock(&device.lock);
	mutex_lock(&device.renderLock);
	ValleyViewRenderBuffer* buffer = FindRenderBuffer(client,
		request.sourceHandle);
	status_t status = B_OK;
	bool boundForPresent = false;
	if (buffer == NULL || buffer->quarantined
		|| (buffer->closePending && !queued)
		|| (!queued && buffer->queuedReferenceCount != 0)
		|| buffer->domain != valleyview::kRenderDomainCpu
		|| request.sourceOffset > buffer->size
		|| sourceBytes > buffer->size - request.sourceOffset) {
		status = B_BAD_VALUE;
	}
	if (status == B_OK
		&& (status = MakeRenderBufferResident(client, *buffer)) == B_OK
		&& buffer->ggttOffset == valleyview::kInvalidRenderGgttOffset) {
		status = BindRenderBufferGgtt(device, *buffer);
		boundForPresent = status == B_OK;
	}
	if (status == B_OK)
		buffer->domain = valleyview::kRenderDomainBcs;
	mutex_unlock(&device.renderLock);

	if (status == B_OK) {
		mutex_lock(&device.presentLock);
		status = SubmitBcsRenderCopy(device,
			buffer->ggttOffset + request.sourceOffset, request.sourceStride,
			request);
		mutex_unlock(&device.presentLock);

		mutex_lock(&device.renderLock);
		if (device.gpuFaulted || device.renderMemoryQuarantined) {
			buffer->quarantined = true;
		} else {
			buffer->domain = valleyview::kRenderDomainCpu;
		}
		if (boundForPresent && !buffer->quarantined) {
			status_t evictionStatus = UnbindRenderBufferGgtt(device, *buffer);
			if (status == B_OK)
				status = evictionStatus;
		}
		mutex_unlock(&device.renderLock);
	}
	if (status == B_OK)
		device.renderDirectPresents++;
	else
		device.renderDirectPresentFailures++;
	mutex_unlock(&device.lock);

	const bigtime_t elapsed = system_time() - started;
	request.elapsedUs = elapsed > 0 ? static_cast<uint64>(elapsed) : 0;
	request.status = status;
	return status;
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
	bool sourceBound = false;
	bool destinationBound = false;
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
	if (status == B_OK)
		status = MakeRenderBufferResident(client, *source);
	if (status == B_OK)
		status = MakeRenderBufferResident(client, *destination);

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
	if (status == B_OK
		&& source->ggttOffset == valleyview::kInvalidRenderGgttOffset) {
		status = BindRenderBufferGgtt(device, *source);
		sourceBound = status == B_OK;
	}
	if (status == B_OK
		&& destination->ggttOffset == valleyview::kInvalidRenderGgttOffset) {
		status = BindRenderBufferGgtt(device, *destination);
		destinationBound = status == B_OK;
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
	if (destinationBound && !destination->quarantined) {
		status_t evictionStatus = UnbindRenderBufferGgtt(device, *destination);
		if (status == B_OK)
			status = evictionStatus;
	}
	if (sourceBound && !source->quarantined) {
		status_t evictionStatus = UnbindRenderBufferGgtt(device, *source);
		if (status == B_OK)
			status = evictionStatus;
	}

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
	diagnostics.shaderStatus = B_NO_INIT;
	diagnostics.shaderGgttRestoreStatus = B_NO_INIT;
	diagnostics.shaderFirstChangedOffset = UINT32_MAX;
	diagnostics.shaderLastChangedOffset = UINT32_MAX;
	diagnostics.shaderFirstUnexpectedOffset = UINT32_MAX;
	diagnostics.shaderGuardMismatchOffset = UINT32_MAX;
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
	ValleyViewRenderBuffer* shaderBuffer = NULL;
	status_t status = CreateInternalRenderBuffer(device,
		valleyview::kRcsTestBytes, buffer, diagnostics, false);
	if (status == B_OK) {
		diagnostics.ringOffset = buffer->ggttOffset
			+ valleyview::kRcsRingPage * valleyview::kPageSize;
		diagnostics.statusOffset = buffer->ggttOffset
			+ valleyview::kRcsStatusPage * valleyview::kPageSize;
		diagnostics.batchOffset = buffer->ggttOffset
			+ valleyview::kRcsBatchPage * valleyview::kPageSize;
		diagnostics.resultOffset = buffer->ggttOffset
			+ valleyview::kRcsResultPage * valleyview::kPageSize;
		status = RecordInternalRenderBuffer(*buffer, diagnostics.pteBefore,
			diagnostics.pteBound);
	}
	if (status == B_OK) {
		status = CreateInternalRenderBuffer(device,
			valleyview::kRcsShaderTotalBytes, shaderBuffer, diagnostics, true);
	}
	if (status == B_OK) {
		diagnostics.shaderOffset = shaderBuffer->ggttOffset;
		diagnostics.shaderPages = shaderBuffer->pageCount;
		status = RecordInternalRenderBuffer(*shaderBuffer,
			diagnostics.shaderPteBefore, diagnostics.shaderPteBound);
	}
	if (status == B_OK) {
		mutex_unlock(&device.renderLock);
		mutex_lock(&device.presentLock);
		status = ExecuteRcsDiagnostic(device, *buffer, *shaderBuffer,
			diagnostics);
		mutex_unlock(&device.presentLock);
		mutex_lock(&device.renderLock);
	}

	status_t shaderCleanupStatus = DestroyInternalRenderBuffer(device,
		shaderBuffer, diagnostics.shaderGgttRestoreStatus,
		diagnostics.shaderPteAfter);
	if ((diagnostics.flags & valleyview::kRcsShaderGgttBound) != 0
		&& diagnostics.shaderGgttRestoreStatus == B_OK) {
		diagnostics.flags |= valleyview::kRcsShaderGgttRestored;
		if (diagnostics.shaderStage
				>= valleyview::kRcsShaderStageOutputVerified
			&& (diagnostics.flags & valleyview::kRcsShaderCacheRestored)
				!= 0) {
			diagnostics.shaderStage = valleyview::kRcsShaderStageRestored;
		}
	}
	status_t cleanupStatus = DestroyInternalRenderBuffer(device, buffer,
		diagnostics.ggttRestoreStatus, diagnostics.pteAfter);
	if ((diagnostics.flags & valleyview::kRcsGgttBound) != 0
		&& diagnostics.ggttRestoreStatus == B_OK) {
		diagnostics.flags |= valleyview::kRcsGgttRestored;
		if (diagnostics.stage >= valleyview::kRcsStageOutputVerified)
			diagnostics.stage = valleyview::kRcsStageRestored;
	}
	if (shaderCleanupStatus != B_OK)
		status = shaderCleanupStatus;
	if (cleanupStatus != B_OK)
		status = cleanupStatus;
	if (diagnostics.shaderStatus == B_NO_INIT)
		diagnostics.shaderStatus = status;
	else if (shaderCleanupStatus != B_OK)
		diagnostics.shaderStatus = shaderCleanupStatus;

	device.rcsTests++;
	if (status == B_OK) {
		device.rcsReady = true;
		device.rcsStatus = B_OK;
	} else {
		device.rcsReady = false;
		device.rcsSubmissionReady = false;
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
		status_t status = ReleaseClientRenderBuffer(client, buffer);
		if (status != B_OK) {
			dprintf("intel_valleyview: render client cleanup failed: %"
				B_PRId32 "\n", status);
		}
	}
	if (client.contextHandle != 0) {
		status_t status = DestroyPpgttContextLocked(client,
			client.contextHandle);
		if (status != B_OK) {
			dprintf("intel_valleyview: render context cleanup failed: %"
				B_PRId32 "\n", status);
		}
	}
	UnlockRenderDevice(device);
}
