// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot
#pragma once

#include <KernelExport.h>
#include <device_manager.h>

#include <util/iovec_support.h>	// generic_io_vec (physical segments from the IO op)

#include "Adma2.h"
#include "Types.h"


// Kernel IO-scheduling machinery lives in the global namespace; forward-declare
// so this header stays light (the definitions are pulled into Disk.cpp only).
class DMAResource;
class IOSchedulerSimple;
struct IOOperation;


namespace jr::sdhci {

class Card;
class SdhciController;
class SdhciEngine;


// The DMA constraints a strategy imposes, in dependency-free form. The
// Controller translates this into the kernel's dma_restrictions when it builds
// the DMA resource -- our headers stay clean of private kernel source headers.
struct DmaRestrictions {
	uint64_t	lowAddress = 0;
	uint64_t	highAddress = 0;		// 0 == no ceiling
	uint32_t	alignment = 0;
	uint32_t	boundary = 0;			// segment must not cross this
	uint32_t	maxSegmentSize = 0;
	uint32_t	maxSegmentCount = 0;
};


// Disk -- the DMA-strategy axis, and node #2 (the devfs-published device).
//
// This is the second (and last) device_manager node we create. A Disk turns
// block IORequests into card commands, and the *strategy* subclass decides how
// bytes move: ADMA2 scatter/gather, single-buffer SDMA, or (unused) PIO. That
// choice changes IORequest decomposition and the DMA restrictions we advertise,
// which is exactly why it is a class axis rather than a flag.
//
// The Disk borrows the Controller's Card and Engine; the Controller owns their
// lifetime.
class Disk {
public:
	virtual ~Disk();

	// DMA restrictions this strategy imposes (max segment size, boundary,
	// alignment). The block IO scheduler decomposes requests to honor these.
	virtual DmaRestrictions Restrictions() const = 0;

	virtual DmaStrategy Strategy() const = 0;
	virtual const char* StrategyName() const = 0;

	// Execute one already-decomposed transfer described by the physical vecs.
	// The vecs come straight from the IO operation (post-DMAResource
	// translation), so their addresses are physical.
	virtual status_t Transfer(off_t position, const generic_io_vec* vecs,
		size_t vecCount, bool isWrite, size_t& bytesTransferred) = 0;

	// devfs read/write entry points route through here after scheduling.
	status_t DoIO(io_request* request);

	uint64_t Capacity() const;
	uint32_t BlockSize() const;

	// Factory: pick the strategy from what the controller/card advertise, then
	// build its DMA resource + IO scheduler. Bay Trail eMMC uses ADMA2; SDMA is
	// the fallback; PIO is intentionally unused (present only as a reference).
	static Disk* Create(DmaStrategy strategy, SdhciController& controller,
		Card& card, SdhciEngine& engine);

protected:
	Disk(SdhciController& controller, Card& card, SdhciEngine& engine)
		: fController(controller), fCard(card), fEngine(engine) {}

	// Build the DMAResource (from this strategy's Restrictions()) and the block
	// IO scheduler, wiring the scheduler callback to _RunOperation.
	status_t InitIo();

	SdhciController&	fController;
	Card&				fCard;
	SdhciEngine&		fEngine;

private:
	// IOSchedulerSimple callback: hand one decomposed operation to the strategy.
	static status_t _IoCallback(void* data, IOOperation* operation);
	status_t _RunOperation(IOOperation* operation);

	DMAResource*		fDmaResource = nullptr;
	IOSchedulerSimple*	fScheduler = nullptr;
};


class Adma2Disk final : public Disk {
public:
	Adma2Disk(SdhciController& c, Card& card, SdhciEngine& e) : Disk(c, card, e) {}

	DmaRestrictions Restrictions() const override;
	DmaStrategy Strategy() const override { return DmaStrategy::Adma2; }
	const char* StrategyName() const override { return "ADMA2"; }
	status_t Transfer(off_t position, const generic_io_vec* vecs, size_t vecCount,
		bool isWrite, size_t& bytesTransferred) override;

private:
	Adma2Descriptor	fDescriptors[kAdma2MaxDescriptors];
};


class SdmaDisk final : public Disk {
public:
	SdmaDisk(SdhciController& c, Card& card, SdhciEngine& e) : Disk(c, card, e) {}

	DmaRestrictions Restrictions() const override;
	DmaStrategy Strategy() const override { return DmaStrategy::Sdma; }
	const char* StrategyName() const override { return "SDMA"; }
	status_t Transfer(off_t position, const generic_io_vec* vecs, size_t vecCount,
		bool isWrite, size_t& bytesTransferred) override;
};


// Intentionally unused: a straight PIO reference path. Kept as an oracle for
// tests and debugging, never selected by the factory.
class PioDisk final : public Disk {
public:
	PioDisk(SdhciController& c, Card& card, SdhciEngine& e) : Disk(c, card, e) {}

	DmaRestrictions Restrictions() const override;
	DmaStrategy Strategy() const override { return DmaStrategy::Pio; }
	const char* StrategyName() const override { return "PIO(unused)"; }
	status_t Transfer(off_t position, const generic_io_vec* vecs, size_t vecCount,
		bool isWrite, size_t& bytesTransferred) override;
};


} // namespace jr::sdhci
