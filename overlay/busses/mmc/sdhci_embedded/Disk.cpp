// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot

#include "Disk.h"

#include <new>

#include "dma_resources.h"
#include "IORequest.h"
#include "IOSchedulerSimple.h"

#include "Card.h"
#include "SdhciController.h"
#include "SdhciEngine.h"

// Disk base: capacity plumbing (from the Card), the block-IO scheduler wiring,
// and the block-IO entry point. The concrete strategies (ADMA2 / SDMA / PIO)
// live in their own files; the factory here picks one from the DmaStrategy the
// controller resolved, then builds its DMA resource + IO scheduler.

namespace jr::sdhci {


Disk::~Disk()
{
	// The scheduler references the DMA resource, so tear it down first.
	delete fScheduler;
	delete fDmaResource;
}


Disk*
Disk::Create(DmaStrategy strategy, SdhciController& controller, Card& card,
	SdhciEngine& engine)
{
	Disk* disk = nullptr;
	switch (strategy) {
		case DmaStrategy::Adma2:
			disk = new(std::nothrow) Adma2Disk(controller, card, engine);
			break;
		case DmaStrategy::Sdma:
			disk = new(std::nothrow) SdmaDisk(controller, card, engine);
			break;
		case DmaStrategy::Pio:
			disk = new(std::nothrow) PioDisk(controller, card, engine);
			break;
		default:
			return nullptr;
	}

	if (disk == nullptr)
		return nullptr;

	// A Disk isn't usable until its DMA resource + scheduler exist; fold that
	// into construction so callers never see a half-built device.
	if (disk->InitIo() != B_OK) {
		delete disk;
		return nullptr;
	}

	return disk;
}


status_t
Disk::InitIo()
{
	// Translate the strategy's dependency-free restrictions into the kernel's
	// dma_restrictions. Zero fields are the kernel's "no limit" sentinels, so we
	// only fill what the strategy actually constrains; max_transfer_size stays 0
	// (unbounded) because max segment size * count already bounds an operation.
	const DmaRestrictions dr = Restrictions();

	dma_restrictions restrictions = {};
	restrictions.low_address = dr.lowAddress;
	restrictions.high_address = dr.highAddress;
	restrictions.alignment = dr.alignment;
	restrictions.boundary = dr.boundary;
	restrictions.max_segment_size = dr.maxSegmentSize;
	restrictions.max_segment_count = dr.maxSegmentCount;

	fDmaResource = new(std::nothrow) DMAResource;
	if (fDmaResource == nullptr)
		return B_NO_MEMORY;

	status_t status = fDmaResource->Init(restrictions, BlockSize(), 16, 16);
	if (status != B_OK)
		return status;

	fScheduler = new(std::nothrow) IOSchedulerSimple(fDmaResource);
	if (fScheduler == nullptr)
		return B_NO_MEMORY;

	status = fScheduler->Init("sdhci_embedded storage");
	if (status != B_OK)
		return status;

	// The scheduler serializes this callback onto a single worker thread, which
	// is what lets each strategy reuse per-Disk scratch (e.g. the ADMA2 table)
	// without its own locking.
	fScheduler->SetCallback(&_IoCallback, this);
	return B_OK;
}


uint64_t
Disk::Capacity() const
{
	return fCard.SectorCount();
}


uint32_t
Disk::BlockSize() const
{
	return fCard.SectorSize();
}


/*static*/ status_t
Disk::_IoCallback(void* data, IOOperation* operation)
{
	return static_cast<Disk*>(data)->_RunOperation(operation);
}


status_t
Disk::_RunOperation(IOOperation* operation)
{
	// The operation's vecs are already decomposed to honor our restrictions and
	// translated to physical addresses (bounce-buffered if needed), so the
	// strategy can consume them directly.
	size_t transferred = 0;
	status_t status = Transfer(operation->Offset(), operation->Vecs(),
		operation->VecCount(), operation->IsWrite(), transferred);

	fScheduler->OperationCompleted(operation, status,
		status == B_OK ? transferred : 0);
	return status;
}


// Block-IO entry. devfs read/write land here; we hand the request to the
// scheduler, which decomposes it into operations and drives _IoCallback ->
// Transfer() per operation.
status_t
Disk::DoIO(io_request* request)
{
	if (fScheduler == nullptr)
		return B_NO_INIT;
	return fScheduler->ScheduleRequest(request);
}


} // namespace jr::sdhci
