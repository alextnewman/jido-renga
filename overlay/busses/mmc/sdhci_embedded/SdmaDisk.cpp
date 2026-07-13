// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot

#include "Disk.h"

#include "Card.h"
#include "SdhciController.h"
#include "SdhciEngine.h"
#include "StagedRequest.h"

#include "IORequest.h"
#include <io_requests.h>
#include <util/AutoLock.h>
#include <vm/vm.h>

#include <new>

// Single-buffer SDMA strategy -- the fallback when ADMA2 is unavailable.
// Requests are serialized by this strategy and copied through one contiguous
// staging buffer, keeping both hardware DMA and its failure lifecycle away from
// IOSchedulerSimple's request-owner bookkeeping.

namespace jr::sdhci {

namespace {
	constexpr size_t kStagingSize = 512 * 1024;
}


SdmaDisk::~SdmaDisk()
{
	_StopRequestThread();
	if (fStagingArea >= 0)
		delete_area(fStagingArea);
}


status_t
SdmaDisk::InitStrategy()
{
	virtual_address_restrictions virtualRestrictions = {};
	virtualRestrictions.address_specification = B_ANY_KERNEL_ADDRESS;

	physical_address_restrictions physicalRestrictions = {};
	physicalRestrictions.high_address = 0x100000000ull;
	physicalRestrictions.alignment = B_PAGE_SIZE;
	physicalRestrictions.boundary = kStagingSize;

	void* address = nullptr;
	fStagingArea = create_area_etc(B_SYSTEM_TEAM, "sdhci_emb SDMA staging",
		kStagingSize, B_CONTIGUOUS, B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA,
		0, 0, &virtualRestrictions, &physicalRestrictions, &address);
	if (fStagingArea < B_OK)
		return fStagingArea;
	fStagingBuffer = static_cast<uint8_t*>(address);

	physical_entry entry;
	if (fStagingBuffer == nullptr
		|| get_memory_map(fStagingBuffer, kStagingSize, &entry, 1) != B_OK
		|| entry.size < kStagingSize
		|| entry.address + kStagingSize > 0x100000000ull
		|| entry.address / kStagingSize
			!= (entry.address + kStagingSize - 1) / kStagingSize) {
		delete_area(fStagingArea);
		fStagingArea = -1;
		fStagingBuffer = nullptr;
		return B_BAD_DATA;
	}

	fStagingPhysical = entry.address;
	JR_TRACE_ALWAYS(fEngine.Label(), "SDMA staging: phys %#" B_PRIxPHYSADDR
		", size %" B_PRIuSIZE "\n", fStagingPhysical, kStagingSize);
	return B_OK;
}


status_t
SdmaDisk::InitIo()
{
	mutex_init(&fMediaIoLock, "sdhci_emb SDMA media");
	fMediaIoLockInitialized = true;
	mutex_init(&fRequestLock, "sdhci_emb SDMA requests");
	fRequestLockInitialized = true;

	fRequestSem = create_sem(0, "sdhci_emb SDMA request");
	if (fRequestSem < B_OK)
		return fRequestSem;

	fRequestThread = spawn_kernel_thread(&_RequestThreadEntry,
		"sdhci_embedded SDMA I/O", B_NORMAL_PRIORITY + 2, this);
	if (fRequestThread < B_OK)
		return fRequestThread;

	status_t status = resume_thread(fRequestThread);
	if (status != B_OK) {
		kill_thread(fRequestThread);
		fRequestThread = -1;
		return status;
	}
	return B_OK;
}


void
SdmaDisk::LockMediaIo()
{
	mutex_lock(&fMediaIoLock);
}


void
SdmaDisk::UnlockMediaIo()
{
	mutex_unlock(&fMediaIoLock);
}


DmaRestrictions
SdmaDisk::Restrictions() const
{
	DmaRestrictions r;
	r.alignment = 4;
	r.boundary = 512 * 1024;			// the SDMA buffer-boundary
	r.maxTransferSize = 512 * 1024;
	r.maxSegmentSize = 512 * 1024;
	r.maxSegmentCount = 1;				// single-buffer DMA
	r.highAddress = 0x100000000ull;
	return r;
}


status_t
SdmaDisk::DoIO(io_request* request)
{
	status_t status = ValidateRequest(request);
	if (status != B_OK) {
		if (request != nullptr)
			notify_io_request(request, status);
		return status;
	}
	if (fRequestThread < B_OK || fRequestSem < B_OK) {
		notify_io_request(request, B_NO_INIT);
		return B_NO_INIT;
	}
	const int32 mediaState = MediaState();
	if ((mediaState & 1) == 0) {
		notify_io_request(request, B_DEV_NO_MEDIA);
		return B_DEV_NO_MEDIA;
	}

	RequestNode* node
		= new(std::nothrow) RequestNode{request, mediaState, nullptr};
	if (node == nullptr) {
		notify_io_request(request, B_NO_MEMORY);
		return B_NO_MEMORY;
	}

	IOBuffer* buffer = request->Buffer();
	if (buffer->IsVirtual()) {
		status = buffer->LockMemory(request->TeamID(), request->IsWrite());
		if (status != B_OK) {
			delete node;
			notify_io_request(request, status);
			return status;
		}
	}

	mutex_lock(&fRequestLock);
	if (fStopping || MediaState() != mediaState) {
		mutex_unlock(&fRequestLock);
		delete node;
		status = fStopping ? B_CANCELED : B_DEV_NO_MEDIA;
		notify_io_request(request, status);
		return status;
	}
	if (fRequestTail == nullptr)
		fRequestHead = node;
	else
		fRequestTail->next = node;
	fRequestTail = node;
	mutex_unlock(&fRequestLock);

	release_sem(fRequestSem);
	return B_OK;
}


status_t
SdmaDisk::Transfer(off_t position, const generic_io_vec* vecs, size_t vecCount,
	bool isWrite, size_t& bytesTransferred)
{
	if (vecCount != 1 || vecs == nullptr || fStagingBuffer == nullptr)
		return B_BAD_VALUE;
	if (vecs[0].length > kStagingSize)
		return B_BAD_VALUE;
	const size_t bytes = vecs[0].length;

	if (isWrite) {
		status_t status = vm_memcpy_from_physical(fStagingBuffer, vecs[0].base,
			bytes, false);
		if (status != B_OK)
			return status;
		memory_write_barrier();
	}

	status_t status = _TransferStaging(position, bytes, isWrite);
	if (status != B_OK)
		return status;

	if (!isWrite) {
		memory_read_barrier();
		status = vm_memcpy_to_physical(vecs[0].base, fStagingBuffer, bytes,
			false);
		if (status != B_OK)
			return status;
	}

	bytesTransferred = bytes;
	return B_OK;
}


status_t
SdmaDisk::_RunRequest(io_request* request, int32 mediaState)
{
	MutexLocker mediaLocker(fMediaIoLock);
	if (MediaState() != mediaState || (mediaState & 1) == 0)
		return B_DEV_NO_MEDIA;

	while (request->RemainingBytes() > 0) {
		if (MediaState() != mediaState)
			return B_DEV_NO_MEDIA;

		const uint64_t requestOffset = static_cast<uint64_t>(request->Offset())
			+ request->TransferredBytes();
		const StagedRequestChunk chunk = PlanStagedRequestChunk(requestOffset,
			request->RemainingBytes(), fCard.SectorSize(), kStagingSize);
		if (!chunk.IsValid() || chunk.mediaOffset > INT64_MAX)
			return B_BAD_VALUE;

		status_t status;
		if (request->IsWrite()) {
			if (chunk.NeedsReadBeforeWrite()) {
				status = _TransferStaging(static_cast<off_t>(chunk.mediaOffset),
					chunk.mediaBytes, false);
				if (status != B_OK)
					return status;
				if (MediaState() != mediaState)
					return B_DEV_NO_MEDIA;
			}

			status = _CopyRequestToStaging(request,
				static_cast<off_t>(requestOffset), chunk.bufferOffset,
				chunk.requestBytes);
			if (status != B_OK)
				return status;
			if (MediaState() != mediaState)
				return B_DEV_NO_MEDIA;
			memory_write_barrier();
			status = _TransferStaging(static_cast<off_t>(chunk.mediaOffset),
				chunk.mediaBytes, true);
		} else {
			status = _TransferStaging(static_cast<off_t>(chunk.mediaOffset),
				chunk.mediaBytes, false);
			if (status == B_OK) {
				memory_read_barrier();
				status = _CopyStagingToRequest(request,
					static_cast<off_t>(requestOffset), chunk.bufferOffset,
					chunk.requestBytes);
			}
		}
		if (status != B_OK)
			return status;
		if (MediaState() != mediaState)
			return B_DEV_NO_MEDIA;

		request->Advance(chunk.requestBytes);
	}

	return B_OK;
}


status_t
SdmaDisk::_CopyRequestToStaging(io_request* request, off_t requestOffset,
	size_t stagingOffset, size_t bytes)
{
	// Offset-first selects IORequest's external-buffer -> local-buffer overload.
	return request->CopyData(requestOffset, fStagingBuffer + stagingOffset, bytes);
}


status_t
SdmaDisk::_CopyStagingToRequest(io_request* request, off_t requestOffset,
	size_t stagingOffset, size_t bytes)
{
	// Buffer-first selects IORequest's local-buffer -> external-buffer overload.
	return request->CopyData(
		static_cast<const void*>(fStagingBuffer + stagingOffset), requestOffset,
		bytes);
}


status_t
SdmaDisk::_TransferStaging(off_t position, size_t byteCount, bool isWrite)
{
	if (fStagingBuffer == nullptr || byteCount == 0 || byteCount > kStagingSize
		|| byteCount % fCard.SectorSize() != 0 || position < 0
		|| position % fCard.SectorSize() != 0) {
		return B_BAD_VALUE;
	}

	const uint32_t bytes = static_cast<uint32_t>(byteCount);
	const uint32_t blocks = bytes / fCard.SectorSize();
	if (blocks == 0 || blocks > 65535)
		return B_BAD_VALUE;

	const uint64_t block = static_cast<uint64_t>(position) / fCard.SectorSize();
	const uint64_t commandAddress = fCard.UsesSectorAddressing()
		? block : static_cast<uint64_t>(position);
	if (commandAddress > UINT32_MAX)
		return B_BAD_VALUE;

	const Cmd command = isWrite
		? (blocks > 1 ? Cmd::WriteMultipleBlocks : Cmd::WriteSingleBlock)
		: (blocks > 1 ? Cmd::ReadMultipleBlocks : Cmd::ReadSingleBlock);

	DataTransfer data;
	data.blockSize = static_cast<uint16_t>(fCard.SectorSize());
	data.blockCount = blocks;
	data.sdmaAddress = fStagingPhysical;

	CommandOutcome outcome;
	status_t status = fEngine.ExecuteData(command,
		static_cast<uint32_t>(commandAddress), ReplyType::R1, data, outcome);
	if (status == B_OK && R1HasError(outcome.response[0]))
		status = B_IO_ERROR;
	if (status != B_OK) {
		SetOnline(false);
		JR_WARN(fEngine.Label(), "SDMA %s failed: CMD%u pos=%" B_PRIdOFF
			" bytes=%" B_PRIu32 " blocks=%" B_PRIu32
			" staging=%#08" B_PRIxPHYSADDR " status=%s R1=%#08" B_PRIx32 "\n",
			isWrite ? "write" : "read", static_cast<unsigned>(command), position,
			bytes, blocks, fStagingPhysical, strerror(status),
			outcome.response[0]);
	}
	return status;
}


/*static*/ int32
SdmaDisk::_RequestThreadEntry(void* data)
{
	return static_cast<SdmaDisk*>(data)->_RequestThread();
}


int32
SdmaDisk::_RequestThread()
{
	while (true) {
		status_t status;
		do {
			status = acquire_sem(fRequestSem);
		} while (status == B_INTERRUPTED);
		if (status != B_OK)
			return status;

		mutex_lock(&fRequestLock);
		RequestNode* node = fRequestHead;
		if (node != nullptr) {
			fRequestHead = node->next;
			if (fRequestHead == nullptr)
				fRequestTail = nullptr;
		}
		const bool stopping = fStopping;
		mutex_unlock(&fRequestLock);

		if (node == nullptr) {
			if (stopping)
				return B_OK;
			continue;
		}

		io_request* request = node->request;
		const int32 mediaState = node->mediaState;
		delete node;
		status = stopping ? B_CANCELED : _RunRequest(request, mediaState);
		notify_io_request(request, status);
	}
}


void
SdmaDisk::_StopRequestThread()
{
	if (fRequestLockInitialized) {
		mutex_lock(&fRequestLock);
		fStopping = true;
		mutex_unlock(&fRequestLock);
	}

	if (fRequestSem >= B_OK)
		release_sem(fRequestSem);
	if (fRequestThread >= B_OK) {
		status_t ignored;
		wait_for_thread(fRequestThread, &ignored);
		fRequestThread = -1;
	}

	while (fRequestHead != nullptr) {
		RequestNode* node = fRequestHead;
		fRequestHead = node->next;
		notify_io_request(node->request, B_CANCELED);
		delete node;
	}
	fRequestTail = nullptr;

	if (fRequestSem >= B_OK) {
		delete_sem(fRequestSem);
		fRequestSem = -1;
	}
	if (fRequestLockInitialized) {
		mutex_destroy(&fRequestLock);
		fRequestLockInitialized = false;
	}
	if (fMediaIoLockInitialized) {
		mutex_destroy(&fMediaIoLock);
		fMediaIoLockInitialized = false;
	}
}


} // namespace jr::sdhci
