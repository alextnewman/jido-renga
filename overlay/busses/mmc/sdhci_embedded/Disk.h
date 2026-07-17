// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot
#pragma once

#include <KernelExport.h>
#include <device_manager.h>
#include <lock.h>

#include <util/iovec_support.h>	// generic_io_vec (physical segments from the IO op)

#include "Adma2.h"
#include "MediaStatus.h"
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
// the DMA resource, keeping private kernel types out of this header.
struct DmaRestrictions {
	uint64_t	lowAddress = 0;
	uint64_t	highAddress = 0;		// 0 == no ceiling
	uint32_t	alignment = 0;
	uint32_t	boundary = 0;			// segment must not cross this
	uint32_t	maxTransferSize = 0;
	uint32_t	maxSegmentSize = 0;
	uint32_t	maxSegmentCount = 0;
};


// Disk -- the DMA-strategy axis, and node #2 (the devfs-published device).
//
// This is the second and final device_manager node. A Disk turns
// block IORequests into card commands, and the *strategy* subclass decides how
// bytes move: ADMA2 scatter/gather, single-buffer SDMA, or (unused) PIO. That
// choice changes IORequest decomposition and the advertised DMA restrictions,
// which is exactly why it is a class axis rather than a flag.
//
// The Disk borrows the Controller's Card and Engine; the Controller owns their
// lifetime.
class Disk {
public:
	virtual ~Disk();

	// DMA restrictions imposed when this strategy uses Haiku's scheduler path.
	virtual DmaRestrictions Restrictions() const = 0;

	virtual DmaStrategy Strategy() const = 0;
	virtual const char* StrategyName() const = 0;

	// Execute one already-decomposed transfer described by the physical vecs.
	// The vecs come straight from the IO operation (post-DMAResource
	// translation), so their addresses are physical.
	virtual status_t Transfer(off_t position, const generic_io_vec* vecs,
		size_t vecCount, bool isWrite, size_t& bytesTransferred) = 0;

	// devfs read/write entry points route through the strategy's serialized
	// request path.
	virtual status_t DoIO(io_request* request);

	uint64_t Capacity() const;
	uint32_t BlockSize() const;
	bool IsOnline() const { return fMediaStatus.IsOnline(); }
	int32 MediaState() const { return fMediaStatus.State(); }
	void SetOnline(bool online) { fMediaStatus.SetOnline(online); }
	status_t ConsumeMediaStatus() { return fMediaStatus.ConsumeStatus(); }
	void RestoreMediaChange() { fMediaStatus.RestoreChange(); }
	virtual void LockMediaIo() {}
	virtual void UnlockMediaIo() {}

	// Factory: pick the strategy from what the controller/card advertise, then
	// build its block-I/O path. Bay Trail eMMC uses ADMA2; SDMA is the fallback;
	// PIO is intentionally unused (present only as a reference).
	static Disk* Create(DmaStrategy strategy, SdhciController& controller,
		Card& card, SdhciEngine& engine);

protected:
	Disk(SdhciController& controller, Card& card, SdhciEngine& engine)
		: fController(controller), fCard(card), fEngine(engine) {}

	virtual status_t InitStrategy() { return B_OK; }

	// Build the strategy's block-I/O execution path.
	virtual status_t InitIo();
	status_t ValidateRequest(io_request* request) const;

	SdhciController&	fController;
	Card&				fCard;
	SdhciEngine&		fEngine;

private:
	// IOSchedulerSimple callback: hand one decomposed operation to the strategy.
	static status_t _IoCallback(void* data, IOOperation* operation);
	status_t _RunOperation(IOOperation* operation);

	DMAResource*		fDmaResource = nullptr;
	IOSchedulerSimple*	fScheduler = nullptr;
	MediaStatusTracker	fMediaStatus;
};


class Adma2Disk final : public Disk {
public:
	Adma2Disk(SdhciController& c, Card& card, SdhciEngine& e) : Disk(c, card, e) {}
	~Adma2Disk() override;

	DmaRestrictions Restrictions() const override;
	DmaStrategy Strategy() const override { return DmaStrategy::Adma2; }
	const char* StrategyName() const override { return "ADMA2"; }
	status_t Transfer(off_t position, const generic_io_vec* vecs, size_t vecCount,
		bool isWrite, size_t& bytesTransferred) override;

private:
	status_t InitStrategy() override;

	area_id				fDescriptorArea = -1;
	Adma2Descriptor*	fDescriptors = nullptr;
	phys_addr_t			fDescriptorPhysical = 0;
};


class SdmaDisk final : public Disk {
public:
	SdmaDisk(SdhciController& c, Card& card, SdhciEngine& e) : Disk(c, card, e) {}
	~SdmaDisk() override;

	DmaRestrictions Restrictions() const override;
	DmaStrategy Strategy() const override { return DmaStrategy::Sdma; }
	const char* StrategyName() const override { return "SDMA"; }
	status_t Transfer(off_t position, const generic_io_vec* vecs, size_t vecCount,
		bool isWrite, size_t& bytesTransferred) override;
	status_t DoIO(io_request* request) override;
	void LockMediaIo() override;
	void UnlockMediaIo() override;

private:
	status_t InitStrategy() override;
	status_t InitIo() override;
	status_t _RunRequest(io_request* request, int32 mediaState);
	status_t _TransferStaging(off_t position, size_t bytes, bool isWrite);
	status_t _CopyRequestToStaging(io_request* request, off_t requestOffset,
		size_t stagingOffset, size_t bytes);
	status_t _CopyStagingToRequest(io_request* request, off_t requestOffset,
		size_t stagingOffset, size_t bytes);

	struct RequestNode {
		io_request*	request;
		int32		mediaState;
		RequestNode*	next;
	};

	static int32 _RequestThreadEntry(void* data);
	int32 _RequestThread();
	void _StopRequestThread();

	area_id		fStagingArea = -1;
	uint8_t*	fStagingBuffer = nullptr;
	phys_addr_t	fStagingPhysical = 0;
	mutex		fRequestLock;
	bool		fRequestLockInitialized = false;
	mutex		fMediaIoLock;
	bool		fMediaIoLockInitialized = false;
	sem_id		fRequestSem = -1;
	thread_id	fRequestThread = -1;
	RequestNode* fRequestHead = nullptr;
	RequestNode* fRequestTail = nullptr;
	bool		fStopping = false;
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
