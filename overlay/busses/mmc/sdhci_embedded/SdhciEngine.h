// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot
#pragma once

#include <KernelExport.h>
#include <lock.h>

#include "Command.h"
#include "Convergence.h"
#include "Personality.h"
#include "SdhciRegisters.h"
#include "Trace.h"
#include "Transaction.h"

// SdhciEngine -- the mechanism behind the "meow bus".
//
// The engine owns the MMIO register block, the single bus lock (caller
// serialization only; the worker never takes it), the lock-free mailbox, the
// completion semaphore, and the worker thread. Callers submit a Cmd; the worker
// (the "owner") polls the controller and drives each transaction to a terminal
// verdict using the pure Convergence policy. Interrupts are mere "meows": the
// ISR only wakes the worker, it never touches hardware state.
//
// Policy (which bits mean what, when to retry) lives in Convergence.h and is
// host-tested. This class is only the hands: register pokes and thread plumbing.

namespace jr::sdhci {


struct CommandOutcome {
	AttemptResult	result = AttemptResult::Ok;
	uint32_t		response[4] = {0, 0, 0, 0};
};


class SdhciEngine : public IHostQuirkTarget {
public:
	SdhciEngine() = default;
	~SdhciEngine();

	SdhciEngine(const SdhciEngine&) = delete;
	SdhciEngine& operator=(const SdhciEngine&) = delete;

	// Bring the engine up around an already-mapped register block. Resets the
	// controller, applies the personality's post-reset fixups, and starts the
	// worker. Does not power a card -- the Controller drives that.
	status_t Init(volatile RegisterBlock* regs, const HostPersonality* personality,
		const TraceLabel& label);
	void Uninit();

	// The dialect of the card currently on the bus, so OCR validation can be
	// dialect-aware. Set by the Controller once the card is classified.
	void SetDialect(CardDialect dialect) { fActiveDialect = dialect; }

	// The controller's quirk set, used to derive per-command constraints for the
	// convenience Execute() overload below.
	void SetQuirks(Quirk quirks) { fQuirks = quirks; }

	// Serialized command execution. Blocks until the worker reaches a terminal
	// verdict for this command (or the convergence budget is exhausted). Thread
	// safe: callers are serialized on the bus lock.
	status_t Execute(Cmd command, uint32_t argument, ReplyType reply,
		const CommandConstraints& constraints, CommandOutcome& outcome);

	// Convenience overload: derives constraints from the controller quirk set.
	status_t Execute(Cmd command, uint32_t argument, ReplyType reply,
		CommandOutcome& outcome)
	{
		return Execute(command, argument, reply,
			GetCommandConstraints(command, fQuirks), outcome);
	}

	// IHostQuirkTarget: the personality's only register hook.
	void DisablePresetValueMode() override;

	// Data-path helpers (DMA address is programmed by the Disk strategy before
	// issuing the transfer command).
	void ProgramAdma(uint64_t descriptorTablePhysical) volatile;
	void ProgramSdma(uint32_t bufferPhysical, uint16_t blockSize,
		uint16_t blockCount) volatile;

	// Clock / power / width plumbing used during controller bring-up.
	status_t SetClock(uint32_t targetKHz);
	void SetBusWidth(uint8_t width);
	void PowerOn(uint8_t voltage);
	void PowerOff();
	bool CardPresent() const;

	// The "meow": called from interrupt context. Records that the device wants
	// attention and wakes the worker. Performs no register reads.
	void HandleInterruptMeow();

	const TraceLabel& Label() const { return fLabel; }

private:
	static int32 _WorkerEntry(void* self);
	int32 _WorkerLoop();
	void _ServiceOnce();
	void _DriveTransaction(Transaction& txn);

	// Hardware sequences (implementation points; see sdhci-worker-architecture).
	status_t _ResetFull();
	void _IssueToHardware(const Transaction& txn);
	uint32_t _ReadAndClearInterrupts();
	void _ReadResponse(ReplyType reply, uint32_t out[4]);

	volatile RegisterBlock*		fRegs = nullptr;
	const HostPersonality*		fPersonality = nullptr;
	TraceLabel					fLabel;

	mutex						fBusLock = MUTEX_INITIALIZER("sdhci_emb bus");
	sem_id						fCompletion = -1;
	sem_id						fWakeup = -1;
	thread_id					fWorker = -1;
	volatile bool				fWorkerRunning = false;

	TransactionMailbox			fMailbox;		// caller -> worker (lock-free)
	VirtualControllerState		fVcState;		// worker-local device model

	// The dialect the engine stamps onto each new transaction; set by the
	// Controller once the card is classified. Per-command policy now travels on
	// the Transaction itself (see Transaction.h) so nothing here is raced by a
	// worker still finishing a timed-out command.
	CardDialect					fActiveDialect = CardDialect::Unknown;

	Quirk						fQuirks = Quirk::None;

	uint32_t					fBaseClockKHz = 0;
	uint32_t					fTimeoutClockKHz = 0;
};


} // namespace jr::sdhci
