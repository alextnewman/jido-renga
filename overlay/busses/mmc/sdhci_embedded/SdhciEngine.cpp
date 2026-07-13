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
	constexpr int		kR1bBusyPolls = 10000;				// up to 5s of busy polling
	constexpr bigtime_t	kR1bBusyPollUs = 500;
	constexpr int		kRegulatorPolls = 100;				// regulator-stable settle
	constexpr bigtime_t	kRegulatorPollUs = 100;
	constexpr bigtime_t	kPowerOnDelayUs = 10000;			// POWER_ON_DELAY quirk settle
	constexpr uint8_t	kTimeoutDividerRaw = 14;			// max 2^27 period (SD-clock timeout)
	constexpr uint8_t	kHostVersion410 = 4;

	// Interrupt masks and snapshot acknowledgement policy live in Convergence.h
	// beside the bit names so "which bits matter" remains host-testable.

	const char*
	AttemptResultLabel(AttemptResult result)
	{
		switch (result) {
			case AttemptResult::Ok:				return "ok";
			case AttemptResult::SpuriousOcr:		return "bad-ocr";
			case AttemptResult::Busy:			return "inhibit";
			case AttemptResult::CommandTimeout:	return "command-timeout";
			case AttemptResult::DataTimeout:		return "data-timeout";
			case AttemptResult::DataCrc:			return "data-crc";
			case AttemptResult::AdmaError:		return "adma-error";
			case AttemptResult::Error:			return "controller-error";
			default:							return "unknown";
		}
	}

	const char*
	RetryActionLabel(RetryAction action)
	{
		switch (action) {
			case RetryAction::Succeed:				return "succeed";
			case RetryAction::Retry:					return "retry";
			case RetryAction::RetryResetLines:		return "reset-lines";
			case RetryAction::ResetAndReidentify:	return "reidentify";
			case RetryAction::Fail:					return "fail";
			default:								return "unknown";
		}
	}
}


status_t
SdhciEngine::ExecuteTuning(Cmd command, uint16_t blockSize)
{
	fRegs->hostControl2.SetExecuteTuning(true);

	for (int attempt = 0; attempt < 40; attempt++) {
		DataTransfer data;
		data.blockSize = blockSize;
		data.blockCount = 1;
		data.transferMode = transfer_mode::kRead;
		data.tuning = true;

		CommandConstraints constraints;
		constraints.timeoutMs = 50;
		CommandOutcome outcome;
		status_t status = ExecuteData(command, 0, ReplyType::R1, constraints,
			data, outcome);
		if (status != B_OK) {
			fRegs->hostControl2.ClearTuning();
			fRegs->softwareReset.ResetCommandLine();
			fRegs->softwareReset.ResetDataLine();
			return status;
		}

		if (!fRegs->hostControl2.ExecuteTuning()) {
			if (fRegs->hostControl2.TunedClock())
				return B_OK;
			break;
		}
	}

	fRegs->hostControl2.ClearTuning();
	return B_ERROR;
}


// Data-timeout counter helper wants a real sleep on hardware.
void
SoftwareResetReg::BusyWait10ms()
{
	snooze(10000);
}


HostCapabilities
SdhciEngine::Capabilities() const
{
	const uint8_t platformCaps = fPlatform != nullptr
		? fPlatform->UhsCapabilities() : 0;
	return DecodeHostCapabilities(fRegs->capabilities.Raw(),
		fRegs->capabilities.Raw1(), platformCaps);
}


status_t
SdhciEngine::ConfigureBus(const BusMode& mode)
{
	SetBusWidth(mode.width);

	uint16_t uhsMode = HostControl2Reg::kSdr12;
	switch (mode.timing) {
		case BusTiming::MmcHs200:
		case BusTiming::SdSdr104:
			uhsMode = HostControl2Reg::kSdr104;
			break;
		case BusTiming::MmcDdr52:
		case BusTiming::SdDdr50:
			uhsMode = HostControl2Reg::kDdr50;
			break;
		case BusTiming::SdSdr50:
			uhsMode = HostControl2Reg::kSdr50;
			break;
		case BusTiming::SdSdr25:
			uhsMode = HostControl2Reg::kSdr25;
			break;
		default:
			break;
	}
	fRegs->hostControl2.SetUhsMode(uhsMode);

	const bool highSpeed = mode.timing != BusTiming::Legacy
		&& mode.timing != BusTiming::SdSdr12;
	fRegs->hostControl.SetHighSpeed(highSpeed);
	return SetClock(mode.clockKHz, false);
}


status_t
SdhciEngine::SwitchSignalVoltage(bool to1v8, bool checkDataLines)
{
	if (checkDataLines && to1v8 && fRegs->presentState.DataLineLevels() != 0)
		return B_IO_ERROR;

	fRegs->clockControl.DisableSdClock();
	fRegs->hostControl2.SetSignal1v8(to1v8);

	status_t status = fPlatform != nullptr
		? fPlatform->SwitchSignalVoltage(to1v8) : B_NOT_SUPPORTED;
	if (status != B_OK) {
		fRegs->hostControl2.SetSignal1v8(!to1v8);
		fRegs->clockControl.EnableSdClock();
		return status;
	}

	snooze(5000);
	fRegs->clockControl.EnableSdClock();
	snooze(1000);

	if (checkDataLines && to1v8 && fRegs->presentState.DataLineLevels() != 0xf)
		return B_IO_ERROR;
	return B_OK;
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

	// Latch and signal only the completion/error sources the convergence policy
	// understands. Card detection is owned by the present-state watcher.
	fRegs->interruptStatusEnable = kStatusEnableMask;
	fRegs->interruptSignalEnable = kSignalMask;

	const uint32_t capabilities = fRegs->capabilities.Raw();
	const uint32_t capabilities1 = fRegs->capabilities.Raw1();
	fBaseClockKHz = fRegs->capabilities.BaseClockMHz() * 1000;
	fTimeoutClockKHz = fRegs->capabilities.TimeoutClockKHz();
	if (Has(fQuirks, Quirk::Fixed1MHzTimeoutClock))
		fTimeoutClockKHz = 1000;

	// Spec>4.00 controllers: clear the (reserved-on-BYT) HOST_CONTROL_2 bit 12.
	if ((fRegs->hostControllerVersion & 0xff) > 3)
		fRegs->hostControl2.ClearReservedV4Bit();

	// Let the personality apply its post-reset fixups (preset disable, etc.).
	if (fPersonality != nullptr)
		fPersonality->PostResetInit(*this);

	// Raise VDD and bring the bus to the identification baseline. Soldered eMMC
	// (EMMC_HW_RESET quirk) has no card-detect line, so force power on; a
	// removable slot only powers up when a card is actually present.
	const bool force = Has(fQuirks, Quirk::EmmcHardwareReset);
	if (_PowerOnBus(force)) {
		const status_t status = _BringUpBus();
		if (status != B_OK)
			return status;
	} else if (force) {
		JR_ERROR(fLabel, "could not power soldered eMMC bus\n");
		return B_IO_ERROR;
	} else {
		JR_TRACE_ALWAYS(fLabel, "no card present at init; bus left unpowered\n");
	}

	fWorkerRunning = true;
	fWorker = spawn_kernel_thread(_WorkerEntry, "sdhci_emb worker",
		B_NORMAL_PRIORITY, this);
	if (fWorker < 0) {
		fWorkerRunning = false;
		return fWorker;
	}
	resume_thread(fWorker);

	JR_TRACE_ALWAYS(fLabel, "engine up: caps %#08" B_PRIx32 "/%#08" B_PRIx32
		", host v%u, base %" B_PRIu32 " kHz, timeout %" B_PRIu32 " kHz\n",
		capabilities, capabilities1, fRegs->hostControllerVersion & 0xff,
		fBaseClockKHz, fTimeoutClockKHz);
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


// The "meow": interrupt context. Read only enough raw status to disown an empty,
// floating, or unmanaged source; never classify command meaning and never write
// hardware here. A plausible interrupt becomes an unordered wake hint. Returning
// B_INVOKE_SCHEDULER when a waiter was released lets the worker clear the
// level-triggered source promptly instead of allowing the ISR to starve it.
int32
SdhciEngine::HandleInterruptMeow()
{
	const uint32_t status = fRegs->interruptStatus;
	if (!IsActionableMeow(status))
		return B_UNHANDLED_INTERRUPT;

	const int32 notified = fMeowCV.NotifyAll();
	return notified > 0 ? B_INVOKE_SCHEDULER : B_HANDLED_INTERRUPT;
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

	const size_t areaSize = ROUNDUP(size, B_PAGE_SIZE);
	void* dmaBuffer = nullptr;
	area_id area = create_area_etc(B_SYSTEM_TEAM, "sdhci_emb datablk", areaSize,
		B_CONTIGUOUS, B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA, 0, 0,
		&vRestrictions, &pRestrictions, &dmaBuffer);
	if (area < B_OK || dmaBuffer == nullptr)
		return B_NO_MEMORY;

	physical_entry entry;
	if (get_memory_map(dmaBuffer, areaSize, &entry, 1) != B_OK
		|| entry.size < size || entry.address >= 0x100000000ull) {
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
	if (status == B_OK && reply == ReplyType::R1
		&& R1HasError(outcome.response[0])) {
		status = B_IO_ERROR;
	}
	if (status == B_OK) {
		memory_read_barrier();
		memcpy(buffer, dmaBuffer, size);
	}

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
		txn->adma2Address = data->adma2Address;
		txn->adma2Entries = data->adma2Entries;
		txn->tuning = data->tuning;
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

	// Once the worker can claim a data transaction, caller buffers must remain
	// pinned until the worker has reset/quiesced the controller and acknowledged
	// completion. Per-attempt budgets live inside the worker; there is no unsafe
	// outer timeout that can release active DMA memory.
	while (!txn->IsDone()) {
		status_t acquired = acquire_sem(fCompletion);
		if (acquired == B_INTERRUPTED)
			continue;
		if (acquired != B_OK)
			return acquired;
	}

	// Our transaction is done and the worker is no longer touching it (its last
	// write happened-before the completion release we just acquired). Copy the
	// response out of the ticket into caller storage.
	for (int i = 0; i < 4; i++)
		outcome.response[i] = txn->responseWords[i];
	outcome.result = txn->result >= 0 ? AttemptResult::Ok : AttemptResult::Error;
	return txn->result;
}


void
SdhciEngine::DisablePresetValueMode()
{
	// HOST_CONTROL_2 preset-value-enable is bit 15; clear it.
	fRegs->hostControl2.DisablePresetValues();
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
		// Idle wakeup (a meow with no queued work): clear any late managed
		// completion/error source so the level-triggered line cannot stay
		// asserted. Hot-plug itself is detected from Present State, not IRQ bits.
		const uint32_t clear = StormSafeIdleClear(fRegs->interruptStatus);
		if (clear != 0)
			fRegs->interruptStatus = clear;
		_SyncVcState();
		return;
	}
	_DriveTransaction(*txn);
	release_sem(fCompletion);
	txn->Release();		// balance the reference Execute() handed the worker
}


// One command, driven to convergence. This is the worker-local retry loop: it
// issues, snapshots/acknowledges the "virtual controller" until a terminal verdict,
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
		uint32_t accumulatedStatus = 0;
		uint32_t lastSnapshot = 0;

		if (!_IssueToHardware(txn, reply, usedAdma2)) {
			// The line stayed inhibited past the grace period.
			attemptResult = AttemptResult::Busy;
		} else {
			const bigtime_t deadline = system_time() + attemptBudgetUs;
			bool wokeByMeow = false;

			// Pre-armed poll -> CV wait -> re-poll: the heart of the meow bus.
			// Each pass arms a wakeup entry *before* looking, so a meow that
			// fires while we read is not lost. Each snapshot is acknowledged
			// immediately to lower the level-triggered line, while its meaningful
			// bits accumulate in worker-owned state. ClassifyPoll alone decides
			// convergence, so split, early, duplicate, spurious, late, and
			// wrong-command pulses remain harmless.
			for (;;) {
				ConditionVariableEntry entry;
				fMeowCV.Add(&entry);

				const uint32_t snapshot = _ReadAndClearInterrupts();
				lastSnapshot = snapshot;
				if (wokeByMeow)
					JR_MEOW(fLabel, snapshot);
				accumulatedStatus
					= AccumulateInterruptStatus(accumulatedStatus, snapshot);

				// A data transaction is only truly finished on TransferComplete:
				// the controller raises CommandComplete first (command accepted)
				// while the DMA/data phase is still running. That data-aware rule
				// lives in the pure, host-proven core (ClassifyPoll).
				const PollVerdict verdict
					= ClassifyPoll(accumulatedStatus, txn.dataPresent, txn.tuning);

				if (verdict == PollVerdict::KeepPolling) {
					if (system_time() >= deadline)
						break;			// budget spent -> stays CommandTimeout
					wokeByMeow = entry.Wait(B_RELATIVE_TIMEOUT,
						kRecheckIntervalUs) == B_OK;
					continue;			// re-arm and look again
				}

				switch (verdict) {
					case PollVerdict::CommandComplete:
					case PollVerdict::TransferComplete:
					case PollVerdict::BufferReadReady:
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
					case PollVerdict::AdmaError:
						_LogAdmaError(txn);
						attemptResult = AttemptResult::AdmaError;
						break;
					case PollVerdict::Error:
						attemptResult = AttemptResult::Error;
						break;
					case PollVerdict::KeepPolling:
						break;
				}
				break;
			}

			// Clear anything that raced the terminal snapshot so the next issue
			// starts from a clean slate. Normal evidence was already acknowledged
			// snapshot-by-snapshot above.
			_DrainInterrupts();

			// An R1b command may leave the card holding DATA0 low; wait it out.
			if (attemptResult == AttemptResult::Ok && reply == ReplyType::R1b
				&& !_WaitForR1bBusy()) {
				attemptResult = AttemptResult::DataTimeout;
			}
		}

		_SyncVcState();
		const RetryAction action = DecideRetry(attemptResult, attempt,
			maxAttempts, txn.constraints, fVcState.cardInserted);

		if (action == RetryAction::Succeed) {
			if (usedAdma2)
				fRegs->hostControl.SetDmaMode(HostControlReg::kSdma);
			txn.MarkDone(B_OK);
			return;
		}
		_LogAttemptFailure(txn, attempt, maxAttempts, attemptResult, action,
			accumulatedStatus, lastSnapshot, usedAdma2);
		// Restore the default SDMA mode after capturing the failed ADMA state.
		// The next single-address transfer (for example EXT_CSD) must interpret
		// offset 0x58 as the SDMA address register.
		if (usedAdma2)
			fRegs->hostControl.SetDmaMode(HostControlReg::kSdma);
		if (action == RetryAction::Fail) {
			status_t failure = B_ERROR;
			if (txn.dataPresent
				&& !fRegs->softwareReset.ResetCommandAndDataLines()) {
				// Do not release caller DMA memory until the controller is
				// unquestionably quiescent. Power removal is the final fence.
				_TerminateBus();
				snooze(kBusResetSettleUs);
				_RestoreAfterReset();
				failure = B_DEV_NOT_READY;
			}
			txn.MarkDone(failure);
			return;
		}
		if (action == RetryAction::RetryResetLines) {
			if (!fRegs->softwareReset.ResetCommandAndDataLines()) {
				// A failed reset cannot prove DMA quiescence. Remove power before
				// releasing the caller's pinned memory.
				_TerminateBus();
				snooze(kBusResetSettleUs);
				_RestoreAfterReset();
				txn.MarkDone(B_DEV_NOT_READY);
				return;
			}
		} else if (action == RetryAction::ResetAndReidentify) {
			JR_TRACE(fLabel, "bus reset requires card re-identification\n");
			_TerminateBus();
			snooze(kBusResetSettleUs);
			_RestoreAfterReset();
			txn.MarkDone(B_DEV_NOT_READY);
			return;
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
	if (fRegs->presentState.CommandInhibit()) {
		JR_WARN(fLabel, "CMD%u issue blocked by command inhibit; present %#08"
			B_PRIx32 "\n", static_cast<unsigned>(txn.command),
			fRegs->presentState.Raw());
		return false;
	}
	if (RequiresDataLineIdle(txn.dataPresent, reply)) {
		for (uint32_t i = 0; i < kInhibitGracePolls
				&& fRegs->presentState.DataInhibit(); i++) {
			snooze(kInhibitPollUs);
		}
		if (fRegs->presentState.DataInhibit()) {
			JR_WARN(fLabel, "CMD%u issue blocked by data inhibit; present %#08"
				B_PRIx32 "\n", static_cast<unsigned>(txn.command),
				fRegs->presentState.Raw());
			return false;
		}
	}

	fRegs->argument = txn.argument;

	if (txn.dataPresent) {
		if (txn.tuning) {
			fRegs->blockSize.Configure(txn.blockSize, BlockSizeReg::kBoundary512K);
			fRegs->blockCount = 1;
		} else if (txn.adma2Table != nullptr) {
			// ADMA2: switch to ADMA2 mode and program the descriptor-table
			// low 32-bit physical address. This host does not advertise 64-bit
			// ADMA, so offset 0x5C must not be touched.
			if (txn.adma2Address == 0 || txn.adma2Address >= 0x100000000ull) {
				JR_WARN(fLabel, "CMD%u invalid ADMA2 table address %#" B_PRIx64
					"\n", static_cast<unsigned>(txn.command), txn.adma2Address);
				return false;
			}
			fRegs->hostControl.SetDmaMode(HostControlReg::kAdma32);
			usedAdma2 = true;
			memory_write_barrier();
			fRegs->admaSystemAddress = static_cast<uint32_t>(txn.adma2Address);
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
	const uint32_t residualStatus = _DrainInterrupts();
	if (InterruptBitsToAcknowledge(residualStatus) != 0) {
		JR_WARN(fLabel, "CMD%u issue blocked by stale interrupt status %#08"
			B_PRIx32 "\n", static_cast<unsigned>(txn.command), residualStatus);
		return false;
	}

	uint8_t flags = ResponseFlags(reply);
	if (txn.dataPresent)
		flags |= CommandReg::kDataPresent;

	fRegs->command.Send(static_cast<uint8_t>(txn.command), flags);
	return true;
}


uint32_t
SdhciEngine::_ReadAndClearInterrupts()
{
	const uint32_t snapshot = fRegs->interruptStatus;
	const uint32_t clear = InterruptBitsToAcknowledge(snapshot);
	if (clear != 0)
		fRegs->interruptStatus = clear;
	return snapshot;
}


uint32_t
SdhciEngine::_DrainInterrupts()
{
	// Flush each write-one-to-clear with a readback before the next command can
	// be issued. One pass is insufficient on Bay Trail when completion from the
	// previous command is still propagating through the interrupt latch.
	uint32_t status = fRegs->interruptStatus;
	for (uint32_t pass = 0; pass < 4; pass++) {
		const uint32_t clear = InterruptBitsToAcknowledge(status);
		if (clear == 0)
			break;
		fRegs->interruptStatus = clear;
		status = fRegs->interruptStatus;
	}
	return status;
}


void
SdhciEngine::_LogAttemptFailure(const Transaction& txn, uint32_t attempt,
	uint32_t maxAttempts, AttemptResult result, RetryAction action,
	uint32_t accumulatedStatus, uint32_t lastSnapshot, bool usedAdma2) const
{
	JR_WARN(fLabel, "cookie CMD%u arg %#08" B_PRIx32 " attempt %" B_PRIu32
		"/%" B_PRIu32 " result=%s action=%s irq=%#08" B_PRIx32
		" last=%#08" B_PRIx32 " present=%#08" B_PRIx32 "\n",
		static_cast<unsigned>(txn.command), txn.argument, attempt + 1,
		maxAttempts, AttemptResultLabel(result), RetryActionLabel(action),
		accumulatedStatus, lastSnapshot, fRegs->presentState.Raw());

	if (!txn.dataPresent)
		return;

	JR_WARN(fLabel, "cookie data=%ux%u mode=%#04x host=%#02x block=%#04x"
		" timeout=%#02x dma=%s addr=%#08" B_PRIx32 "\n",
		txn.blockCount, txn.blockSize, fRegs->transferMode.Get(),
		fRegs->hostControl.Get(), fRegs->blockSize.Get(),
		fRegs->timeoutControl.Get(), usedAdma2 ? "ADMA2" : "SDMA",
		usedAdma2 ? fRegs->admaSystemAddress : fRegs->systemAddress);

	if (usedAdma2 && txn.adma2Table != nullptr && txn.adma2Entries > 0) {
		const Adma2Descriptor& first = txn.adma2Table[0];
		const Adma2Descriptor& last = txn.adma2Table[txn.adma2Entries - 1];
		JR_WARN(fLabel, "cookie ADMA state=%#02x current=%#08" B_PRIx32
			" table=%#08" B_PRIx64 " entries=%" B_PRIu32
			" first=%#04x/%#04x/%#08x last=%#04x/%#04x/%#08x\n",
			fRegs->admaErrorStatus, fRegs->admaSystemAddress, txn.adma2Address,
			txn.adma2Entries, AdmaLittle16(first.attributes),
			AdmaLittle16(first.length), AdmaLittle32(first.address),
			AdmaLittle16(last.attributes), AdmaLittle16(last.length),
			AdmaLittle32(last.address));
	}
}


void
SdhciEngine::_LogAdmaError(const Transaction& txn) const
{
	const uint32_t current = fRegs->admaSystemAddress;
	JR_ERROR(fLabel, "ADMA2 error: state %#02x current %#" B_PRIx32
		" table %#" B_PRIx64 " entries %" B_PRIu32 "\n",
		fRegs->admaErrorStatus, current, txn.adma2Address, txn.adma2Entries);

	if (txn.adma2Table == nullptr)
		return;
	for (uint32_t i = 0; i < txn.adma2Entries; i++) {
		const Adma2Descriptor& d = txn.adma2Table[i];
		JR_ERROR(fLabel, "  adma[%" B_PRIu32 "] attr %#04x len %#04x addr %#08x\n",
			i, AdmaLittle16(d.attributes), AdmaLittle16(d.length),
			AdmaLittle32(d.address));
		if ((AdmaLittle16(d.attributes)
				& static_cast<uint16_t>(Adma2Attr::TransferEnd))
			== static_cast<uint16_t>(Adma2Attr::TransferEnd)) {
			break;
		}
	}
}


bool
SdhciEngine::_WaitForR1bBusy()
{
	// After an R1b (busy) response the card may hold DATA0 low; wait for the
	// data line to release so the next command does not collide.
	for (int i = 0; i < kR1bBusyPolls; i++) {
		if (!fRegs->presentState.DataInhibit())
			return true;
		snooze(kR1bBusyPollUs);
	}
	JR_ERROR(fLabel, "data line still busy after R1b command\n");
	return false;
}


void
SdhciEngine::_SyncVcState()
{
	const bool inserted = Has(fQuirks, Quirk::EmmcHardwareReset)
		|| fRegs->presentState.CardInserted();
	const bool regulatorStable
		= (fRegs->hostControllerVersion & 0xff) < kHostVersion410
			|| fRegs->presentState.RegulatorStable();
	fVcState.Update(fRegs->presentState.CommandInhibit(),
		fRegs->presentState.DataInhibit(), inserted, regulatorStable);
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


status_t
SdhciEngine::_BringUpBus()
{
	// SDHCI 4.10+ exposes a regulator-stable indication. Bay Trail predates that
	// contract, so its profile uses the bounded post-power delay below instead of
	// polling a reserved Present State bit.
	if ((fRegs->hostControllerVersion & 0xff) >= kHostVersion410) {
		for (int i = 0; i < kRegulatorPolls
				&& !fRegs->presentState.RegulatorStable(); i++) {
			snooze(kRegulatorPollUs);
		}
		if (!fRegs->presentState.RegulatorStable()) {
			JR_ERROR(fLabel, "bus regulator did not stabilize\n");
			return B_TIMED_OUT;
		}
	}

	if (Has(fQuirks, Quirk::PowerOnDelay))
		snooze(kPowerOnDelayUs);

	// After reset Host Control 1 is undefined; force SDMA, one-bit legacy timing,
	// then drop to the identification clock.
	fRegs->hostControl.SetDmaMode(HostControlReg::kSdma);
	fRegs->hostControl.SetHighSpeed(false);
	fRegs->hostControl.SetBusWidth(HostControlReg::kWidth1);
	const status_t clockStatus = SetClock(400, false);
	if (clockStatus != B_OK) {
		JR_ERROR(fLabel, "identification clock failed: %s\n",
			strerror(clockStatus));
		return clockStatus;
	}

	uint32_t timeoutKHz = fTimeoutClockKHz;
	if (timeoutKHz == 0 && fPersonality != nullptr)
		timeoutKHz = fPersonality->TimeoutClockKHz();
	if (timeoutKHz != 0)
		fRegs->timeoutControl.SetForDelay(timeoutKHz, 500);
	else
		fRegs->timeoutControl.SetRaw(kTimeoutDividerRaw);
	JR_TRACE_ALWAYS(fLabel, "identification baseline: clock %" B_PRIu32
		" kHz, timeout source %" B_PRIu32 " kHz, DTOCV %u\n",
		fVcState.currentClockKHz, timeoutKHz, fRegs->timeoutControl.Get());

	_SyncVcState();
	return B_OK;
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
	if (!fRegs->softwareReset.ResetAll()) {
		JR_ERROR(fLabel, "controller reset did not quiesce after power cycle\n");
		return;
	}
	if (fPersonality != nullptr)
		fPersonality->PostResetInit(*this);

	// Re-power (forcing for soldered eMMC), re-run the identification bring-up,
	// and re-arm interrupts. Card protocol state is intentionally not restored:
	// callers must start identification again after this destructive reset.
	const bool force = Has(fQuirks, Quirk::EmmcHardwareReset);
	if (_PowerOnBus(force)) {
		const status_t status = _BringUpBus();
		if (status != B_OK) {
			JR_ERROR(fLabel, "bus restore failed: %s\n", strerror(status));
			return;
		}
	} else {
		JR_ERROR(fLabel, "bus restore could not reapply power\n");
		return;
	}

	fRegs->interruptStatusEnable = kStatusEnableMask;
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
		fRegs->hostControl2.Set(static_cast<uint16_t>(
			fRegs->hostControl2.Get() | (1u << 15)));
		return B_OK;
	}

	fRegs->clockControl.DisableSdClock();

	const ClockSetting setting = ComputeClockSetting(fBaseClockKHz, targetKHz);
	const uint16_t applied
		= fRegs->clockControl.SetDivider(setting.divider);

	// Bring the internal clock up, wait for stability, run the Bay Trail PLL
	// stage when selected by the profile, then gate SDCLK on.
	fRegs->clockControl.EnableInternal();
	for (int i = 0; i < 100 && !fRegs->clockControl.InternalStable(); i++)
		snooze(1000);
	if (!fRegs->clockControl.InternalStable())
		return B_TIMED_OUT;
	if (Has(fQuirks, Quirk::ClockPllSequence)) {
		fRegs->clockControl.EnablePll();
		for (int i = 0; i < 100 && !fRegs->clockControl.InternalStable(); i++)
			snooze(1000);
		if (!fRegs->clockControl.InternalStable())
			return B_TIMED_OUT;
	}
	fRegs->clockControl.EnableSdClock();

	fVcState.currentClockKHz = fBaseClockKHz / applied;

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
	return Has(fQuirks, Quirk::EmmcHardwareReset) || fVcState.cardInserted;
}


} // namespace jr::sdhci
