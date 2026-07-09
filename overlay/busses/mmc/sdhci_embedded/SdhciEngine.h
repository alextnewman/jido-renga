// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot
#pragma once

#include <KernelExport.h>
#include <condition_variable.h>
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
// verdict using the pure Convergence policy. Interrupts are mere "meows": a
// lossy condition-variable pulse that only wakes the worker, never touching
// hardware state. Completion (worker -> caller) travels the other way on a 1:1
// counting semaphore -- see the two members below.
//
// Policy (which bits mean what, when to retry) lives in Convergence.h and is
// host-tested. This class is only the hands: register pokes and thread plumbing.

namespace jr::sdhci {


struct CommandOutcome {
	AttemptResult	result = AttemptResult::Ok;
	uint32_t		response[4] = {0, 0, 0, 0};
};


// Data-phase parameters a Disk strategy stages for a transfer command. They are
// copied onto the ticket so the worker -- the sole hardware accessor -- programs
// SDMA/ADMA2 atomically with the command issue, instead of the caller poking DMA
// registers from its own thread (which could interleave with the worker).
struct DataTransfer {
	uint16_t			blockSize = 0;
	uint32_t			blockCount = 0;
	uint64_t			sdmaAddress = 0;		// used when adma2Table == nullptr
	Adma2Descriptor*	adma2Table = nullptr;	// non-null selects ADMA2
	uint32_t			adma2Entries = 0;
	// Optional Transfer Mode override (0 == derive from the command). Needed for
	// the eMMC EXT_CSD read: CMD8 collides with SD's non-data SEND_IF_COND, so
	// the command alone can't tell ComputeTransferMode() it is a data read.
	uint16_t			transferMode = 0;
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

	// Data-phase command submission. Same serialized worker path as Execute(),
	// but stages the DMA descriptor + block geometry onto the ticket so the
	// worker programs SDMA/ADMA2 as one step with the command issue. Used by the
	// Disk strategies; never program DMA registers from the caller's thread.
	status_t ExecuteData(Cmd command, uint32_t argument, ReplyType reply,
		const CommandConstraints& constraints, const DataTransfer& data,
		CommandOutcome& outcome);

	// Convenience overload: derives constraints from the controller quirk set.
	status_t ExecuteData(Cmd command, uint32_t argument, ReplyType reply,
		const DataTransfer& data, CommandOutcome& outcome)
	{
		return ExecuteData(command, argument, reply,
			GetCommandConstraints(command, fQuirks), data, outcome);
	}

	// One-shot register-style data read used at bring-up (e.g. eMMC EXT_CSD).
	// Allocates a transient 32-bit-physical contiguous DMA buffer, issues the
	// command through the same worker path as ExecuteData() via single-buffer
	// SDMA, then copies the bytes back into 'buffer'. Not for the hot IO path --
	// the Disk strategies decompose bulk transfers through the IO scheduler.
	status_t ReadDataBlock(Cmd command, uint32_t argument, ReplyType reply,
		void* buffer, uint32_t size);

	// IHostQuirkTarget: the personality's only register hook.
	void DisablePresetValueMode() override;

	// Clock / power / width plumbing used during controller bring-up.
	// allowAuto lets a spec>=3 controller keep preset values (skipped on Bay
	// Trail, whose presets are broken); the identification path passes false.
	status_t SetClock(uint32_t targetKHz, bool allowAuto = false);
	void SetBusWidth(uint8_t width);
	void PowerOn(uint8_t voltage);
	void PowerOff();
	bool CardPresent() const;

	// eMMC vendor hardware reset: pulse Power Control bit 4 before CMD0 to force
	// a clean card state. No-op unless the EmmcHardwareReset quirk is set. Safe
	// only between transactions (the worker must be idle); card identification
	// calls it before the very first command.
	void EmmcHardwareReset();
	// The heavy bus-recovery hammer exposed for card identification: when a
	// command leaves the card wedged in an error state (classically CMD8 issued
	// to an eMMC, which expects a 512-byte EXT_CSD data phase), cut power / clock
	// / interrupts, let Bay Trail settle, then re-power and re-arm. Callers must
	// be between transactions.
	void RecoverBus();

	// The "meow": called from interrupt context. An unordered pulse that wakes
	// the worker to LOOK; it carries no data and counts nothing, so an early,
	// late, doubled, or wrong-command meow is harmless. Performs no register
	// reads. See HandleInterruptMeow().
	void HandleInterruptMeow();

	const TraceLabel& Label() const { return fLabel; }

private:
	static int32 _WorkerEntry(void* self);
	int32 _WorkerLoop();
	void _ServiceOnce();
	void _DriveTransaction(Transaction& txn);

	// Shared body of Execute()/ExecuteData(): build the ticket (staging `data`
	// when non-null), hand it to the worker, and block until it reaches a
	// terminal verdict or the budget expires.
	status_t _Submit(Cmd command, uint32_t argument, ReplyType reply,
		const CommandConstraints& constraints, const DataTransfer* data,
		CommandOutcome& outcome);

	// Hardware sequences (implementation points; see the design doc at
	// docs/design/sdhci_embedded.md).
	// Bring the powered bus to the identification baseline: wait for the
	// regulator, honor the power-on settle quirk, force SDMA + high speed, drop
	// to 400 kHz, and program the data-timeout divider. Shared by Init() and the
	// bus-reset recovery so both leave the controller in an identical state.
	void _BringUpBus();
	// Pick the highest supported bus voltage from Capabilities and raise VDD.
	// `force` bypasses the card-present check for soldered eMMC (no detect line).
	bool _PowerOnBus(bool force);
	bool _SetBusVoltage();
	// The heavy recovery hammer: cut interrupts, clock, and power (TerminateBus),
	// then re-power, re-bring-up, and re-arm interrupts (RestoreAfterReset). Used
	// when the SD-domain timeout clock freezes or the controller wedges busy.
	void _TerminateBus();
	void _RestoreAfterReset();
	// Issue one transaction to the controller. Returns false without issuing if
	// the command (or, for data, the data) line stays inhibited past a short
	// grace period -- the caller then classifies the attempt as Busy. On a data
	// command it programs SDMA/ADMA2 and reports which via `usedAdma2` so the
	// completion path can restore the default SDMA mode. `reply` is the already
	// personality-resolved response type.
	bool _IssueToHardware(const Transaction& txn, ReplyType reply,
		bool& usedAdma2);
	// Read-only poll of the interrupt-status word. The meow ideal: looking never
	// mutates the bus. Clearing is done once, deliberately, by _DrainInterrupts.
	uint32_t _ReadInterrupts() const;
	void _DrainInterrupts();
	// After an R1b command the card may hold DATA0 low (busy); wait it out so the
	// next command does not collide with a still-busy card.
	void _WaitForR1bBusy();
	void _SyncVcState();
	void _ReadResponse(ReplyType reply, uint32_t out[4]);

	volatile RegisterBlock*		fRegs = nullptr;
	const HostPersonality*		fPersonality = nullptr;
	TraceLabel					fLabel;

	mutex						fBusLock = MUTEX_INITIALIZER("sdhci_emb bus");
	// fCompletion: worker -> caller, 1:1 and ordered. A counting semaphore is
	// right here -- a completion must never be lost, and the caller re-checks
	// the ticket's IsDone() to reject a stale nudge.
	sem_id						fCompletion = -1;
	// fMeowCV: the "meow bus", ISR/caller -> worker. An unordered, LOSSY pulse
	// (NotifyAll). A counting semaphore would be wrong here: banked pulses would
	// become phantom rechecks that hammer this fragile bus. A missed pulse is
	// harmless -- it simply degrades to a timed recheck.
	ConditionVariable			fMeowCV;
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
