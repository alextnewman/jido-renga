// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot

#include "SdhciEngine.h"

#include <new>

#include <KernelExport.h>
#include <util/AutoLock.h>

// The engine is the *mechanism*: register pokes plus the worker thread. All the
// "what does this mean / should we retry" judgement lives in the pure, host-
// tested Convergence policy -- this file just calls it and obeys.

namespace jr::sdhci {


namespace {
	constexpr bigtime_t	kPollIntervalUs = 50;
	constexpr bigtime_t	kRetryBackoffUs = 1000;
	constexpr uint32_t	kMaxPollsPerAttempt = 4000;	// ~200ms at 50us
	constexpr uint32_t	kMaxInhibitPolls = 20000;	// ~1s at 50us; then give up
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
	fWakeup = create_sem(0, "sdhci_emb wakeup");
	if (fWakeup < 0)
		return fWakeup;

	status_t status = _ResetFull();
	if (status != B_OK)
		return status;

	fBaseClockKHz = fRegs->capabilities.BaseClockMHz() * 1000;
	fTimeoutClockKHz = fRegs->capabilities.TimeoutClockKHz();

	// Let the personality apply its post-reset fixups (preset disable, etc.).
	if (fPersonality != nullptr)
		fPersonality->PostResetInit(*this);

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
		release_sem(fWakeup);
		status_t ignored;
		wait_for_thread(fWorker, &ignored);
		fWorker = -1;
	}
	if (fCompletion >= 0) {
		delete_sem(fCompletion);
		fCompletion = -1;
	}
	if (fWakeup >= 0) {
		delete_sem(fWakeup);
		fWakeup = -1;
	}
}


// The "meow": interrupt context. No register reads, no locks -- just nudge the
// owner awake to go see what the cat wants.
void
SdhciEngine::HandleInterruptMeow()
{
	release_sem_etc(fWakeup, 1, B_DO_NOT_RESCHEDULE);
}


status_t
SdhciEngine::Execute(Cmd command, uint32_t argument, ReplyType reply,
	const CommandConstraints& constraints, CommandOutcome& outcome)
{
	// Callers are serialized here; the worker never takes this lock.
	MutexLocker locker(fBusLock);

	Transaction* txn = Transaction::New(command);
	if (txn == nullptr)
		return B_NO_MEMORY;
	TicketHolder holder(txn);

	txn->argument = argument;
	txn->replyType = reply;
	txn->dataPresent = ComputeTransferMode(command) != 0;

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
	release_sem(fWakeup);

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
		// Wait for a meow, but wake on a 100ms timer regardless: Bay Trail's
		// interrupts are not to be trusted, so the owner checks periodically.
		acquire_sem_etc(fWakeup, 1, B_RELATIVE_TIMEOUT, 100LL * 1000);
		if (!fWorkerRunning)
			break;
		_ServiceOnce();
	}
	return 0;
}


void
SdhciEngine::_ServiceOnce()
{
	Transaction* txn = fMailbox.Claim();
	if (txn == nullptr)
		return;
	_DriveTransaction(*txn);
	release_sem(fCompletion);
	txn->Release();		// balance the reference Execute() handed the worker
}


// One command, driven to convergence. This is the worker-local retry loop: it
// issues, polls the "virtual controller" until a terminal verdict, classifies
// the attempt, and asks the pure policy whether to stop, retry, or reset+retry.
void
SdhciEngine::_DriveTransaction(Transaction& txn)
{
	const uint32_t maxAttempts = txn.constraints.Attempts();

	// Per-attempt poll budget: honor the command's own timeout when it set one
	// (data commands ask for seconds), else fall back to the engine default.
	const uint32_t maxPolls = txn.constraints.timeoutMs > 0
		? static_cast<uint32_t>(static_cast<uint64_t>(txn.constraints.timeoutMs)
			* 1000u / kPollIntervalUs)
		: kMaxPollsPerAttempt;

	for (uint32_t attempt = 0; attempt < maxAttempts; attempt++) {
		_IssueToHardware(txn);

		AttemptResult attemptResult = AttemptResult::CommandTimeout;

		// Poll the device up to the budget; each read is a peek at what the
		// meowing controller currently wants.
		for (uint32_t poll = 0; poll < maxPolls; poll++) {
			const uint32_t status = _ReadAndClearInterrupts();

			// A data transaction is only truly finished on TransferComplete:
			// the controller raises CommandComplete first (command accepted)
			// while the DMA/data phase is still running. The data-aware rule
			// lives in the pure, host-proven core (ClassifyPoll) so this
			// assumption is tested, not inlined.
			const PollVerdict verdict = ClassifyPoll(status, txn.dataPresent);

			if (verdict == PollVerdict::KeepPolling) {
				JR_MEOW(fLabel, status);
				snooze(kPollIntervalUs);
				continue;
			}

			switch (verdict) {
				case PollVerdict::CommandComplete:
				case PollVerdict::TransferComplete:
					_ReadResponse(txn.replyType, txn.responseWords);
					attemptResult = AttemptResult::Ok;
					// OCR sanity check (personality decides what is garbage).
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

		fVcState.Update(fRegs->presentState.CommandInhibit(),
			fRegs->presentState.DataInhibit(), CardPresent(),
			fRegs->presentState.RegulatorStable());
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
		if (action == RetryAction::RetryWithBusReset) {
			JR_TRACE(fLabel, "bus reset before retry %" B_PRIu32 "\n", attempt);
			fRegs->softwareReset.ResetCommandLine();
			fRegs->softwareReset.ResetDataLine();
		}
		snooze(kRetryBackoffUs);
	}

	txn.MarkDone(B_ERROR);
}


// ---- hardware sequences ---------------------------------------------------

status_t
SdhciEngine::_ResetFull()
{
	if (!fRegs->softwareReset.ResetAll())
		return B_TIMED_OUT;

	// Enable the interrupt sources we consume, but only as status bits -- the
	// signal enable stays selective; the worker polls, it does not depend on the
	// line. (Bay Trail's line lies; that is the whole point of the meow bus.)
	fRegs->interruptStatusEnable = 0xffffffff;
	fRegs->interruptSignalEnable = irq::kCommandComplete | irq::kTransferComplete
		| irq::kCardInsertion | irq::kCardRemoval;
	return B_OK;
}


void
SdhciEngine::_IssueToHardware(const Transaction& txn)
{
	// Wait (bounded) for the command line to be free before issuing. Bay Trail
	// can wedge the inhibit bit; capping the wait lets a stuck controller surface
	// as a command timeout in the poll loop instead of hanging the worker -- and
	// therefore Uninit()'s wait_for_thread() -- forever.
	for (uint32_t i = 0; i < kMaxInhibitPolls
			&& fRegs->presentState.CommandInhibit(); i++) {
		snooze(kPollIntervalUs);
	}
	if (txn.dataPresent) {
		for (uint32_t i = 0; i < kMaxInhibitPolls
				&& fRegs->presentState.DataInhibit(); i++) {
			snooze(kPollIntervalUs);
		}
	}

	// Let the personality override the reply type (e.g. Bay Trail CMD12 -> R1b).
	ReplyType reply = txn.replyType;
	if (fPersonality != nullptr) {
		ReplyType overridden;
		if (fPersonality->OverrideReplyType(txn.command, txn.dialect,
				overridden)) {
			reply = overridden;
		}
	}

	fRegs->argument = txn.argument;
	fRegs->transferMode.Set(ComputeTransferMode(txn.command));

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
}


uint32_t
SdhciEngine::_ReadAndClearInterrupts()
{
	const uint32_t status = fRegs->interruptStatus;
	if (status != 0)
		fRegs->interruptStatus = status;	// write-1-to-clear
	return status;
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


// ---- clock / power plumbing (used by the controller during bring-up) ------

status_t
SdhciEngine::SetClock(uint32_t targetKHz)
{
	if (fBaseClockKHz == 0 || targetKHz == 0)
		return B_BAD_VALUE;

	fRegs->clockControl.DisableSdClock();

	uint16_t divider = 1;
	while ((fBaseClockKHz / divider) > targetKHz && divider < 0x3ff)
		divider <<= 1;

	fRegs->clockControl.SetDivider(divider);
	fRegs->clockControl.EnableInternal();
	for (int i = 0; i < 100 && !fRegs->clockControl.InternalStable(); i++)
		snooze(1000);
	fRegs->clockControl.EnableSdClock();
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


void
SdhciEngine::ProgramAdma(uint64_t descriptorTablePhysical) volatile
{
	fRegs->admaSystemAddress = descriptorTablePhysical;
	fRegs->hostControl.SetDmaMode(HostControlReg::kAdma32);
}


void
SdhciEngine::ProgramSdma(uint32_t bufferPhysical, uint16_t blockSize,
	uint16_t blockCount) volatile
{
	fRegs->systemAddress = bufferPhysical;
	fRegs->blockSize.Configure(blockSize, BlockSizeReg::kBoundary512K);
	fRegs->blockCount = blockCount;
	fRegs->hostControl.SetDmaMode(HostControlReg::kSdma);
}


} // namespace jr::sdhci
