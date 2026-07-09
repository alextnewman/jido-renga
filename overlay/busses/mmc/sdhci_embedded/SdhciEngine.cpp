// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot

#include "SdhciEngine.h"

#include <new>

#include <KernelExport.h>
#include <util/AutoLock.h>
#include <vm/vm.h>			// create_area_etc + address restriction structs

// The engine is the *mechanism*: register pokes plus the worker thread. All the
// "what does this mean / should we retry" judgement lives in the pure, host-
// tested Convergence policy -- this file just calls it and obeys.

namespace jr::sdhci {


namespace {
	// The worker never busy-spins on this soft controller. Between looks it waits
	// on the meow condition variable with a timeout: a meow (interrupt) cuts the
	// wait short -- as fast as interrupts; the timeout guarantees a look even when
	// the line stays silent -- as slow as polling. Deliberately lazy cadences,
	// because hammering Bay Trail's bus in a tight loop can wedge it.
	constexpr bigtime_t	kRecheckIntervalUs = 2000;			// in-command meow recheck
	constexpr bigtime_t	kDispatchRecheckUs = 100LL * 1000;	// idle-worker recheck (~100ms)
	// Per-attempt wall-clock budget when the command carries no timeout of its
	// own (data commands ask for seconds via their constraints).
	constexpr bigtime_t	kDefaultAttemptBudgetUs = 2000LL * 1000;	// ~2s (matches fork default)
	constexpr bigtime_t	kInhibitPollUs = 100;				// pre-issue inhibit settle
	constexpr uint32_t	kInhibitGracePolls = 100;			// ~10ms grace, then Busy
	constexpr bigtime_t	kRetryBackoffUs = 5000;				// backoff base between attempts
	constexpr bigtime_t	kBusResetSettleUs = 50000;			// Bay Trail reset settle
	constexpr int		kR1bBusyPolls = 2000;				// ~1s of 500us DataInhibit polls
	constexpr bigtime_t	kR1bBusyPollUs = 500;
	constexpr int		kRegulatorPolls = 100;				// regulator-stable settle
	constexpr bigtime_t	kRegulatorPollUs = 100;
	constexpr bigtime_t	kPowerOnDelayUs = 10000;			// POWER_ON_DELAY quirk settle
	constexpr uint8_t	kTimeoutDividerRaw = 14;			// max 2^27 period (SD-clock timeout)

	// kDrainMask / kSignalMask now live in Convergence.h beside the bit names,
	// so the "which bits matter" policy is one host-testable place (and the idle
	// worker's StormSafeIdleClear can share kSignalMask).
}


// Data-timeout counter helper wants a real sleep on hardware.
void
SoftwareResetReg::BusyWait10ms()
{
	snooze(10000);
}


SdhciEngine::~SdhciEngine()
{
	Uninit();
}


status_t
SdhciEngine::Init(volatile RegisterBlock* regs, const HostPersonality* personality,
	const TraceLabel& label)
{
	fRegs = regs;
	fPersonality = personality;
	fLabel = label;

	fCompletion = create_sem(0, "sdhci_emb completion");
	if (fCompletion < 0)
		return fCompletion;
	// The meow: an anonymous condition variable pulsed by the ISR and by callers
	// posting work. Lossy by design -- a missed pulse just becomes a timed
	// recheck, never a lost command.
	fMeowCV.Init(this, "sdhci_emb meow");

	// Make sure we are in a sane state: full software reset. (The IOSF-MBI OCP
	// fixup, when required, has already been applied by the Controller before
	// this call -- it must precede any SDHCI register access.)
	if (!fRegs->softwareReset.ResetAll())
		return B_TIMED_OUT;

	// Conservative VC defaults are replaced by the real post-reset present-state
	// before any caller consults the cache.
	_SyncVcState();

	// Spec 4.2: set interrupt masks after reset. Status-enable is wide (bits must
	// be enabled to latch, and the worker polls them); signal-enable is the set
	// that may pulse the ISR "meow".
	fRegs->interruptStatusEnable = 0xffffffff;
	fRegs->interruptSignalEnable = kSignalMask;

	fBaseClockKHz = fRegs->capabilities.BaseClockMHz() * 1000;
	fTimeoutClockKHz = fRegs->capabilities.TimeoutClockKHz();

	// Spec>4.00 controllers: clear the (reserved-on-BYT) HOST_CONTROL_2 bit 12.
	if ((fRegs->hostControllerVersion & 0xff) > 3)
		fRegs->hostControl2 = static_cast<uint16_t>(fRegs->hostControl2 & ~(1u << 12));

	// Let the personality apply its post-reset fixups (preset disable, etc.).
	if (fPersonality != nullptr)
		fPersonality->PostResetInit(*this);

	// Raise VDD and bring the bus to the identification baseline. Soldered eMMC
	// (EMMC_HW_RESET quirk) has no card-detect line, so force power on; a
	// removable slot only powers up when a card is actually present.
	const bool force = Has(fQuirks, Quirk::EmmcHardwareReset);
	if (_PowerOnBus(force))
		_BringUpBus();
	else
		JR_TRACE_ALWAYS(fLabel, "no card present at init; bus left unpowered\n");

	fWorkerRunning = true;
	fWorker = spawn_kernel_thread(_WorkerEntry, "sdhci_emb worker",
		B_REAL_TIME_PRIORITY, this);
	if (fWorker < 0) {
		fWorkerRunning = false;
		return fWorker;
	}
	resume_thread(fWorker);

	JR_TRACE_ALWAYS(fLabel, "engine up: base %" B_PRIu32 " kHz, timeout %"
		B_PRIu32 " kHz\n", fBaseClockKHz, fTimeoutClockKHz);
	return B_OK;
}


void
SdhciEngine::Uninit()
{
	if (fWorkerRunning) {
		fWorkerRunning = false;
		fMeowCV.NotifyAll();		// pulse the worker so it observes the stop flag
		status_t ignored;
		wait_for_thread(fWorker, &ignored);
		fWorker = -1;
	}
	if (fCompletion >= 0) {
		delete_sem(fCompletion);
		fCompletion = -1;
	}
}


// The "meow": interrupt context. No register reads, no locks -- just an unordered
// pulse that nudges the owner awake to go see what the cat wants. It carries no
// information and counts nothing, so an early, late, doubled, spurious, or
// wrong-command meow is harmless: the worker's poll + stateful convergence decide
// what is real. A pulse with no one waiting is simply lost, which degrades to a
// timed recheck -- never a hang. (ConditionVariable::NotifyAll is ISR-safe.)
void
SdhciEngine::HandleInterruptMeow()
{
	fMeowCV.NotifyAll();
}


status_t
SdhciEngine::Execute(Cmd command, uint32_t argument, ReplyType reply,
	const CommandConstraints& constraints, CommandOutcome& outcome)
{
	return _Submit(command, argument, reply, constraints, nullptr, outcome);
}


status_t
SdhciEngine::ExecuteData(Cmd command, uint32_t argument, ReplyType reply,
	const CommandConstraints& constraints, const DataTransfer& data,
	CommandOutcome& outcome)
{
	return _Submit(command, argument, reply, constraints, &data, outcome);
}


status_t
SdhciEngine::ReadDataBlock(Cmd command, uint32_t argument, ReplyType reply,
	void* buffer, uint32_t size)
{
	// Allocate a DMA-safe, physically-contiguous, 32-bit-addressable scratch
	// buffer for the single-block read (mirrors the fork's EXT_CSD path). SDMA
	// can only address 32 bits, so cap the physical range accordingly.
	virtual_address_restrictions vRestrictions = {};
	vRestrictions.address_specification = B_ANY_KERNEL_ADDRESS;
	physical_address_restrictions pRestrictions = {};
	pRestrictions.high_address = 0x100000000ull;	// 32-bit SDMA ceiling
	pRestrictions.alignment = 512;

	void* dmaBuffer = nullptr;
	area_id area = create_area_etc(B_SYSTEM_TEAM, "sdhci_emb datablk", size,
		B_CONTIGUOUS, B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA, 0, 0,
		&vRestrictions, &pRestrictions, &dmaBuffer);
	if (area < B_OK || dmaBuffer == nullptr)
		return B_NO_MEMORY;

	physical_entry entry;
	if (get_memory_map(dmaBuffer, size, &entry, 1) != B_OK) {
		delete_area(area);
		return B_BAD_DATA;
	}

	DataTransfer data;
	data.blockSize = static_cast<uint16_t>(size);
	data.blockCount = 1;
	data.sdmaAddress = entry.address;		// single-buffer SDMA
	data.transferMode = transfer_mode::kRead | transfer_mode::kDmaEnable;

	CommandOutcome outcome;
	status_t status = ExecuteData(command, argument, reply, data, outcome);
	if (status == B_OK)
		memcpy(buffer, dmaBuffer, size);

	delete_area(area);
	return status;
}


status_t
SdhciEngine::_Submit(Cmd command, uint32_t argument, ReplyType reply,
	const CommandConstraints& constraints, const DataTransfer* data,
	CommandOutcome& outcome)
{
	// Callers are serialized here; the worker never takes this lock.
	MutexLocker locker(fBusLock);

	Transaction* txn = Transaction::New(command);
	if (txn == nullptr)
		return B_NO_MEMORY;
	TicketHolder holder(txn);

	txn->argument = argument;
	txn->replyType = reply;
	txn->transferMode = ComputeTransferMode(command);
	txn->dataPresent = txn->transferMode != 0;

	// Stage the DMA descriptor + block geometry onto the ticket so the worker
	// programs SDMA/ADMA2 as one serialized step with the command issue.
	if (data != nullptr) {
		txn->dataPresent = true;
		txn->blockSize = data->blockSize;
		txn->blockCount = data->blockCount;
		txn->dmaAddress = data->sdmaAddress;
		txn->adma2Table = data->adma2Table;
		txn->adma2Entries = data->adma2Entries;
		// Honor an explicit Transfer Mode when the command opcode alone can't
		// convey direction (EXT_CSD's CMD8 collides with SD SEND_IF_COND).
		if (data->transferMode != 0)
			txn->transferMode = data->transferMode;
	}

	// Capture policy and dialect on the ticket itself. A worker still finishing
	// a previously-timed-out command reads *its* ticket, so it can never race
	// the state this new call is setting up.
	txn->constraints = constraints;
	txn->dialect = fActiveDialect;

	// Drain any stale completion signal from a previously-abandoned command so
	// the wait below starts from a clean slate.
	while (acquire_sem_etc(fCompletion, 1, B_RELATIVE_TIMEOUT, 0) == B_OK)
		;

	// Hand a reference to the worker so a late completion after a timeout can
	// never use-after-free the ticket (stale-discard intent of the tested model).
	txn->Retain();
	fMailbox.Post(txn);
	fMeowCV.NotifyAll();		// pulse the worker to claim the mailbox

	// Wait for *our* ticket to reach a terminal verdict. The completion
	// semaphore is only a nudge: a stale signal from an earlier abandoned
	// command can still land here, so we re-check IsDone() and keep waiting
	// until our own transaction is published or the budget expires. This is
	// what prevents a stale wake from reporting a bogus success.
	const bigtime_t deadline = system_time() + 10LL * 1000 * 1000;
	for (;;) {
		status_t acquired = acquire_sem_etc(fCompletion, 1, B_ABSOLUTE_TIMEOUT,
			deadline);
		if (acquired == B_OK) {
			if (txn->IsDone())
				break;
			// A stale signal for someone else's command; keep waiting for ours.
			continue;
		}

		// Timed out. Try to pull our ticket back; if the worker already claimed
		// it, the worker owns the reference and will Release() after MarkDone(),
		// writing only into the ticket -- never into caller storage.
		if (fMailbox.Reclaim(txn))
			txn->Release();
		JR_ERROR(fLabel, "command %u timed out in worker\n",
			(unsigned)static_cast<uint8_t>(command));
		return B_TIMED_OUT;
	}

	// Our transaction is done and the worker is no longer touching it (its last
	// write happened-before the completion release we just acquired). Copy the
	// response out of the ticket into caller storage.
	for (int i = 0; i < 4; i++)
		outcome.response[i] = txn->responseWords[i];
	outcome.result = txn->result >= 0 ? AttemptResult::Ok : AttemptResult::Error;
	return txn->result >= 0 ? B_OK : B_ERROR;
}


void
SdhciEngine::DisablePresetValueMode()
{
	// HOST_CONTROL_2 preset-value-enable is bit 15; clear it.
	fRegs->hostControl2 = static_cast<uint16_t>(fRegs->hostControl2 & ~(1u << 15));
}


int32
SdhciEngine::_WorkerEntry(void* self)
{
	return static_cast<SdhciEngine*>(self)->_WorkerLoop();
}


int32
SdhciEngine::_WorkerLoop()
{
	while (fWorkerRunning) {
		_ServiceOnce();
		if (!fWorkerRunning)
			break;

		// Idle wait, the same shape as the in-command loop: pre-arm a meow entry,
		// then re-check the mailbox. Arming *before* the check means a post+meow
		// that races us is never lost -- if work arrived after Add(), the pulse
		// marks our entry and Wait() returns at once; if it arrived before,
		// Empty() is false and we skip the wait entirely. A wholly missed meow
		// just falls through to the ~100ms recheck. Bay Trail's line is never
		// trusted to fire, so the timer is the backstop, not the plan.
		ConditionVariableEntry entry;
		fMeowCV.Add(&entry);
		if (fMailbox.Empty() && fWorkerRunning)
			entry.Wait(B_RELATIVE_TIMEOUT, kDispatchRecheckUs);
	}
	return 0;
}


void
SdhciEngine::_ServiceOnce()
{
	Transaction* txn = fMailbox.Claim();
	if (txn == nullptr) {
		// Idle wakeup (a meow with no queued work): most are spurious, but a
		// card insert/remove latches a signal-enabled bit that no command drain
		// clears. On this level-triggered line that would re-fire the ISR
		// forever, so clear it here -- the sole-accessor worker's one chance to
		// keep an eventful-but-idle bus from meowing itself to death. Detection
		// itself is the Controller's slow present-state poll, so there is nothing
		// to react to; we only need the line to fall quiet. (Mirrors the
		// reference driver's idle-path interrupt_status clear.)
		const uint32_t clear = StormSafeIdleClear(fRegs->interruptStatus);
		if (clear != 0)
			fRegs->interruptStatus = clear;
		return;
	}
	_DriveTransaction(*txn);
	release_sem(fCompletion);
	txn->Release();		// balance the reference Execute() handed the worker
}


// One command, driven to convergence. This is the worker-local retry loop: it
// issues, polls the "virtual controller" read-only until a terminal verdict,
// classifies the attempt, and asks the pure policy whether to stop, re-issue,
// reset the lines, or terminate+restore the whole bus. All hardware state lives
// under the sole-accessor worker; callers only ever read the VC cache.
void
SdhciEngine::_DriveTransaction(Transaction& txn)
{
	const uint32_t maxAttempts = txn.constraints.Attempts();

	// Per-attempt wall-clock budget: honor the command's own timeout when it set
	// one (data commands ask for seconds), else fall back to the engine default.
	const bigtime_t attemptBudgetUs = txn.constraints.timeoutMs > 0
		? static_cast<bigtime_t>(txn.constraints.timeoutMs) * 1000
		: kDefaultAttemptBudgetUs;

	// Resolve the effective reply type once (personality may override, e.g. Bay
	// Trail CMD12 -> R1b) so the issue flags and the R1b busy-wait agree.
	ReplyType reply = txn.replyType;
	if (fPersonality != nullptr) {
		ReplyType overridden;
		if (fPersonality->OverrideReplyType(txn.command, txn.dialect, overridden))
			reply = overridden;
	}

	// A zero-block data command would trip Bay Trail's strict data-length check;
	// the reference driver treats it as a no-op success rather than touching
	// hardware at all.
	if (txn.dataPresent && txn.blockCount == 0) {
		JR_TRACE_ALWAYS(fLabel, "zero block count with data present; skipping\n");
		txn.MarkDone(B_OK);
		return;
	}

	for (uint32_t attempt = 0; attempt < maxAttempts; attempt++) {
		bool usedAdma2 = false;
		AttemptResult attemptResult = AttemptResult::CommandTimeout;

		if (!_IssueToHardware(txn, reply, usedAdma2)) {
			// The line stayed inhibited past the grace period.
			attemptResult = AttemptResult::Busy;
		} else {
			const bigtime_t deadline = system_time() + attemptBudgetUs;

			// Pre-armed poll -> CV wait -> re-poll: the heart of the meow bus.
			// Each pass arms a wakeup entry *before* looking, so a meow that
			// fires while we read is not lost; a read-only poll then asks the
			// pure convergence policy whether we are done. If not, we wait on
			// that entry until the device meows (fast as interrupts) or the
			// recheck timer fires (slow as polling), then look again. The meow
			// only makes us *look* -- looking never mutates the bus, and
			// ClassifyPoll alone decides convergence, so an early/spurious/late/
			// wrong-command pulse is harmless.
			for (;;) {
				ConditionVariableEntry entry;
				fMeowCV.Add(&entry);

				const uint32_t status = _ReadInterrupts();	// read-only

				// A data transaction is only truly finished on TransferComplete:
				// the controller raises CommandComplete first (command accepted)
				// while the DMA/data phase is still running. That data-aware rule
				// lives in the pure, host-proven core (ClassifyPoll).
				const PollVerdict verdict = ClassifyPoll(status, txn.dataPresent);

				if (verdict == PollVerdict::KeepPolling) {
					JR_MEOW(fLabel, status);
					if (system_time() >= deadline)
						break;			// budget spent -> stays CommandTimeout
					entry.Wait(B_RELATIVE_TIMEOUT, kRecheckIntervalUs);
					continue;			// re-arm and look again
				}

				switch (verdict) {
					case PollVerdict::CommandComplete:
					case PollVerdict::TransferComplete:
						_ReadResponse(reply, txn.responseWords);
						attemptResult = AttemptResult::Ok;
						// OCR sanity (personality decides what is garbage).
						if (txn.constraints.validateOcr && fPersonality != nullptr
							&& !fPersonality->ValidateOcr(txn.responseWords[0],
								txn.dialect)) {
							attemptResult = AttemptResult::SpuriousOcr;
						}
						break;
					case PollVerdict::CommandTimeout:
						attemptResult = AttemptResult::CommandTimeout;
						break;
					case PollVerdict::DataTimeout:
						attemptResult = AttemptResult::DataTimeout;
						break;
					case PollVerdict::DataCrc:
						attemptResult = AttemptResult::DataCrc;
						break;
					case PollVerdict::Error:
						attemptResult = AttemptResult::Error;
						break;
					case PollVerdict::KeepPolling:
						break;
				}
				break;
			}

			// One deliberate clear of everything we latched this attempt, so the
			// next issue (or command) starts from a clean slate.
			_DrainInterrupts();

			// Restore the default SDMA mode after an ADMA2 transaction so the
			// next single-address transfer (e.g. EXT_CSD) programs the right
			// address register.
			if (usedAdma2)
				fRegs->hostControl.SetDmaMode(HostControlReg::kSdma);

			// An R1b command may leave the card holding DATA0 low; wait it out.
			if (attemptResult == AttemptResult::Ok && reply == ReplyType::R1b)
				_WaitForR1bBusy();
		}

		_SyncVcState();
		const RetryAction action = DecideRetry(attemptResult, attempt,
			maxAttempts, txn.constraints, fVcState.cardInserted);

		if (action == RetryAction::Succeed) {
			txn.MarkDone(B_OK);
			return;
		}
		if (action == RetryAction::Fail) {
			txn.MarkDone(B_ERROR);
			return;
		}
		if (action == RetryAction::RetryResetLines) {
			JR_TRACE(fLabel, "line reset before retry %" B_PRIu32 "\n", attempt);
			fRegs->softwareReset.ResetCommandLine();
			fRegs->softwareReset.ResetDataLine();
		} else if (action == RetryAction::RetryWithBusReset) {
			JR_TRACE(fLabel, "bus reset before retry %" B_PRIu32 "\n", attempt);
			_TerminateBus();
			snooze(kBusResetSettleUs);
			_RestoreAfterReset();
		}
		// RetryAction::Retry (spurious OCR) re-issues as-is: no reset.
		snooze(kRetryBackoffUs * (attempt + 1));	// gentle escalating backoff
	}

	txn.MarkDone(B_ERROR);
}


// ---- hardware sequences ---------------------------------------------------

bool
SdhciEngine::_IssueToHardware(const Transaction& txn, ReplyType reply,
	bool& usedAdma2)
{
	usedAdma2 = false;

	// A brief grace tolerates a card still finishing the previous command
	// without escalating to a bus reset; a persistently inhibited (wedged) line
	// surfaces as Busy so convergence can terminate+restore the bus.
	for (uint32_t i = 0; i < kInhibitGracePolls
			&& fRegs->presentState.CommandInhibit(); i++) {
		snooze(kInhibitPollUs);
	}
	if (fRegs->presentState.CommandInhibit())
		return false;
	if (txn.dataPresent) {
		for (uint32_t i = 0; i < kInhibitGracePolls
				&& fRegs->presentState.DataInhibit(); i++) {
			snooze(kInhibitPollUs);
		}
		if (fRegs->presentState.DataInhibit())
			return false;
	}

	fRegs->argument = txn.argument;

	if (txn.dataPresent) {
		if (txn.adma2Table != nullptr) {
			// ADMA2: switch to ADMA2 mode and program the descriptor-table
			// physical address (resolved here -- the worker is the sole HW
			// accessor). Block size/count are still needed for the controller's
			// internal timeout and data-inhibit bookkeeping.
			fRegs->hostControl.SetDmaMode(HostControlReg::kAdma32);
			usedAdma2 = true;
			addr_t admaPhys = reinterpret_cast<addr_t>(txn.adma2Table);
			physical_entry entry;
			const size_t tableSize = txn.adma2Entries * sizeof(Adma2Descriptor);
			if (get_memory_map(const_cast<Adma2Descriptor*>(txn.adma2Table),
					tableSize, &entry, 1) == B_OK) {
				admaPhys = entry.address;
			}
			fRegs->admaSystemAddress = admaPhys;
			fRegs->blockSize.Configure(txn.blockSize, BlockSizeReg::kBoundary512K);
			fRegs->blockCount = static_cast<uint16_t>(txn.blockCount);
		} else {
			// SDMA: a single 32-bit physical address.
			fRegs->systemAddress = static_cast<uint32_t>(txn.dmaAddress);
			fRegs->blockSize.Configure(txn.blockSize, BlockSizeReg::kBoundary512K);
			fRegs->blockCount = static_cast<uint16_t>(txn.blockCount);
		}
	}
	fRegs->transferMode.Set(txn.transferMode);

	// Drain stale latched bits before issuing: Bay Trail accumulates CMD_CMP/
	// TRANS_CMP from earlier commands, and the first poll after SendCommand must
	// not read a stale completion as an instant false success.
	_DrainInterrupts();

	uint8_t flags = 0;
	switch (reply) {
		case ReplyType::R2:
			flags = CommandReg::kResp136 | CommandReg::kCheckCrc;
			break;
		case ReplyType::R1b:
			flags = CommandReg::kResp48Busy | CommandReg::kCheckCrc
				| CommandReg::kCheckIndex;
			break;
		case ReplyType::None:
			flags = 0;
			break;
		default:
			flags = CommandReg::kResp48 | CommandReg::kCheckCrc
				| CommandReg::kCheckIndex;
			break;
	}
	if (txn.dataPresent)
		flags |= CommandReg::kDataPresent;

	fRegs->command.Send(static_cast<uint8_t>(txn.command), flags);
	return true;
}


// Read-only poll of the interrupt-status word. Looking never mutates the bus;
// clearing is a separate, deliberate act (_DrainInterrupts).
uint32_t
SdhciEngine::_ReadInterrupts() const
{
	return fRegs->interruptStatus;
}


void
SdhciEngine::_DrainInterrupts()
{
	// Write-1-to-clear the completion/error bits we act on. A spurious all-ones
	// (floating line) word is skipped so we never write a garbage register back
	// onto itself.
	const uint32_t status = fRegs->interruptStatus;
	if (!IsSpuriousMeow(status))
		fRegs->interruptStatus = status & kDrainMask;
}


void
SdhciEngine::_WaitForR1bBusy()
{
	// After an R1b (busy) response the card may hold DATA0 low; wait for the
	// data line to release so the next command does not collide.
	for (int i = 0; i < kR1bBusyPolls; i++) {
		if (!fRegs->presentState.DataInhibit())
			return;
		snooze(kR1bBusyPollUs);
	}
	JR_ERROR(fLabel, "data line still busy after R1b command\n");
}


void
SdhciEngine::_SyncVcState()
{
	fVcState.Update(fRegs->presentState.CommandInhibit(),
		fRegs->presentState.DataInhibit(), fRegs->presentState.CardInserted(),
		fRegs->presentState.RegulatorStable());
}


void
SdhciEngine::_ReadResponse(ReplyType reply, uint32_t out[4])
{
	if (out == nullptr)
		return;
	if (reply == ReplyType::R2) {
		for (int i = 0; i < 4; i++)
			out[i] = fRegs->response[i];
	} else {
		out[0] = fRegs->response[0];
	}
}


// ---- power / clock / bus-reset plumbing -----------------------------------

bool
SdhciEngine::_SetBusVoltage()
{
	if (fRegs->capabilities.Supports3v3())
		fRegs->powerControl.PowerOn(PowerControlReg::k3v3);
	else if (fRegs->capabilities.Supports3v0())
		fRegs->powerControl.PowerOn(PowerControlReg::k3v0);
	else if (fRegs->capabilities.Supports1v8())
		fRegs->powerControl.PowerOn(PowerControlReg::k1v8);
	else {
		fRegs->powerControl.PowerOff();
		JR_ERROR(fLabel, "no supported bus voltage\n");
		return false;
	}
	return true;
}


bool
SdhciEngine::_PowerOnBus(bool force)
{
	// A removable slot only powers up for a present card; soldered eMMC (no
	// detect line) must force power even though present-state reads absent.
	if (!force && !fRegs->presentState.CardInserted()) {
		JR_TRACE_ALWAYS(fLabel, "card not inserted; not powering bus\n");
		return false;
	}
	return _SetBusVoltage();
}


void
SdhciEngine::_BringUpBus()
{
	// Wait for the regulator, then honor the Bay Trail post-power settle.
	for (int i = 0; i < kRegulatorPolls
			&& !fRegs->presentState.RegulatorStable(); i++) {
		snooze(kRegulatorPollUs);
	}
	if (Has(fQuirks, Quirk::PowerOnDelay))
		snooze(kPowerOnDelayUs);

	// After reset Host Control 1 is undefined; force SDMA (ADMA2 is toggled
	// per-transaction) and high speed, then drop to the identification clock.
	fRegs->hostControl.SetDmaMode(HostControlReg::kSdma);
	fRegs->hostControl.SetHighSpeed(true);
	SetClock(400, false);

	// Data-timeout divider: on Bay Trail the counter rides the SD clock, so use
	// the maximum period; otherwise derive it from the timeout clock frequency.
	if (Has(fQuirks, Quirk::TimeoutClockFromSdClock)) {
		fRegs->timeoutControl.SetRaw(kTimeoutDividerRaw);
	} else {
		uint32_t timeoutKHz = fPersonality != nullptr
			? fPersonality->TimeoutClockKHz() : 0;
		if (timeoutKHz == 0)
			timeoutKHz = fRegs->capabilities.TimeoutClockKHz();
		if (timeoutKHz != 0)
			fRegs->timeoutControl.SetForDelay(timeoutKHz, 500);
	}

	_SyncVcState();
}


void
SdhciEngine::EmmcHardwareReset()
{
	if (!Has(fQuirks, Quirk::EmmcHardwareReset))
		return;
	// Spec 12.2: assert device reset (Power Control bit 4), hold >= 9us, then
	// deassert and give the card its internal-reset window (300-1000us).
	fRegs->powerControl.AssertEmmcReset();
	snooze(10);
	fRegs->powerControl.DeassertEmmcReset();
	snooze(500);
}


void
SdhciEngine::RecoverBus()
{
	_TerminateBus();
	snooze(kBusResetSettleUs);
	_RestoreAfterReset();
}


void
SdhciEngine::_TerminateBus()
{
	// Cut the line, the clock, and the power -- the first half of the heavy
	// recovery hammer.
	fRegs->interruptSignalEnable = 0;
	fRegs->interruptStatusEnable = 0;
	fRegs->clockControl.DisableSdClock();
	fRegs->powerControl.PowerOff();
}


void
SdhciEngine::_RestoreAfterReset()
{
	// Re-power (forcing for soldered eMMC), re-run the identification bring-up,
	// and re-arm interrupts. This deliberately drops the SD clock back to
	// 400 kHz -- a known post-reset speed regression the higher layer must
	// re-negotiate (see the improvement log).
	const bool force = Has(fQuirks, Quirk::EmmcHardwareReset);
	if (_PowerOnBus(force))
		_BringUpBus();

	fRegs->interruptStatusEnable = 0xffffffff;
	fRegs->interruptSignalEnable = kSignalMask;
}


// ---- clock / power plumbing (used by the controller during bring-up) ------

status_t
SdhciEngine::SetClock(uint32_t targetKHz, bool allowAuto)
{
	if (fBaseClockKHz == 0 || targetKHz == 0)
		return B_BAD_VALUE;

	// A spec>=3 controller with working presets can pick the clock itself; Bay
	// Trail's presets are broken (quirk), so the identification path never takes
	// this branch.
	if (allowAuto && (fRegs->hostControllerVersion & 0xff) > 2
			&& !Has(fQuirks, Quirk::BrokenPresetValues)) {
		fRegs->hostControl2 = static_cast<uint16_t>(fRegs->hostControl2
			| (1u << 15));	// preset value enable
		return B_OK;
	}

	fRegs->clockControl.DisableSdClock();

	// Linear divider: base(MHz)*1000 / target(kHz), matching the reference. The
	// register accessor encodes the SDHCI divided-clock form internally.
	uint16_t divider = static_cast<uint16_t>(fBaseClockKHz / targetKHz);
	if (divider == 0)
		divider = 1;
	fRegs->clockControl.SetDivider(divider);

	// Bring the internal clock up, wait for stability, enable the PLL, wait
	// again, then gate SDCLK on -- the Bay Trail sequence.
	fRegs->clockControl.EnableInternal();
	for (int i = 0; i < 100 && !fRegs->clockControl.InternalStable(); i++)
		snooze(1000);
	fRegs->clockControl.EnablePll();
	for (int i = 0; i < 100 && !fRegs->clockControl.InternalStable(); i++)
		snooze(1000);
	fRegs->clockControl.EnableSdClock();

	fVcState.currentClockKHz = targetKHz;

	// If the data-timeout counter rides the SD clock, recompute its divider now
	// that the clock changed.
	if (fPersonality != nullptr && fPersonality->TimeoutClockUsesSdClock())
		fRegs->timeoutControl.SetRaw(kTimeoutDividerRaw);

	return B_OK;
}


void
SdhciEngine::SetBusWidth(uint8_t width)
{
	switch (width) {
		case 8:
			fRegs->hostControl.SetBusWidth(HostControlReg::kWidth8);
			break;
		case 4:
			fRegs->hostControl.SetBusWidth(HostControlReg::kWidth4);
			break;
		default:
			fRegs->hostControl.SetBusWidth(HostControlReg::kWidth1);
			break;
	}
}


void
SdhciEngine::PowerOn(uint8_t voltage)
{
	fRegs->powerControl.PowerOn(voltage);
}


void
SdhciEngine::PowerOff()
{
	fRegs->powerControl.PowerOff();
}


bool
SdhciEngine::CardPresent() const
{
	return fRegs->presentState.CardInserted();
}


} // namespace jr::sdhci
