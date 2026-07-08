// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot

#include "Disk.h"

#include <new>

#include "Card.h"
#include "SdhciController.h"
#include "SdhciEngine.h"

// Disk base: capacity plumbing (from the Card) and the block-IO entry point.
// The concrete strategies (ADMA2 / SDMA / PIO) live in their own files; the
// factory here picks one from the DmaStrategy the controller resolved.

namespace jr::sdhci {


Disk*
Disk::Create(DmaStrategy strategy, SdhciController& controller, Card& card,
	SdhciEngine& engine)
{
	switch (strategy) {
		case DmaStrategy::Adma2:
			return new(std::nothrow) Adma2Disk(controller, card, engine);
		case DmaStrategy::Sdma:
			return new(std::nothrow) SdmaDisk(controller, card, engine);
		case DmaStrategy::Pio:
			return new(std::nothrow) PioDisk(controller, card, engine);
		default:
			return nullptr;
	}
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


// Block-IO entry. The real path hands the request to an IOScheduler built from
// this strategy's Restrictions(), which calls back per operation into
// Transfer(). (Implementation point: IOScheduler wiring during bring-up.)
status_t
Disk::DoIO(io_request* request)
{
	(void)request;
	return B_NOT_SUPPORTED;
}


} // namespace jr::sdhci
