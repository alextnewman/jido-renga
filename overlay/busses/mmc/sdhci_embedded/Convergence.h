// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot
#pragma once

#include <stdint.h>

#include "Command.h"

// ===========================================================================
// The "meow bus" convergence policy.
//
// The Bay Trail controller's interrupts are spurious, late, and sometimes
// missing. We model the interrupt line as a cat: an ISR only *meows*
// ("something happened, maybe"). It reads the raw status only to reject an
// empty/floating/unmanaged source; the worker remains the sole owner that
// acknowledges and interprets controller state.
//
// This header holds the *policy* half of that model, split out from the
// *mechanism* (the register poking in SdhciEngine) so the decisions are pure
// and can be proven off-target:
//
//   InterpretInterruptStatus()  -- what does accumulated evidence mean?
//   IsActionableMeow()          -- should this IRQ wake the worker?
//   AccumulateInterruptStatus() -- retain split completion/error evidence?
//   DecideRetry()               -- given one attempt's outcome, what next?
//
// The engine's worker loop is then a thin driver over these decisions.
// ===========================================================================

namespace jr::sdhci {


// SDHCI Normal/Error Interrupt Status bits we act on (offset 0x30). Only the
// ones the policy cares about are named here; the register accessor owns the
// full map.
namespace irq {
	constexpr uint32_t kCommandComplete		= 1u << 0;
	constexpr uint32_t kTransferComplete	= 1u << 1;
	constexpr uint32_t kBufferReadReady		= 1u << 5;
	constexpr uint32_t kCardInsertion		= 1u << 6;
	constexpr uint32_t kCardRemoval			= 1u << 7;
	constexpr uint32_t kErrorBit			= 1u << 15;
	constexpr uint32_t kCommandTimeout		= 1u << 16;
	constexpr uint32_t kCommandCrc			= 1u << 17;
	constexpr uint32_t kCommandEndBit		= 1u << 18;
	constexpr uint32_t kCommandIndex		= 1u << 19;
	constexpr uint32_t kDataTimeout			= 1u << 20;
	constexpr uint32_t kDataCrc				= 1u << 21;
	constexpr uint32_t kDataEndBit			= 1u << 22;
	constexpr uint32_t kCurrentLimit		= 1u << 23;
	constexpr uint32_t kAutoCommand			= 1u << 24;
	constexpr uint32_t kAdma				= 1u << 25;
	constexpr uint32_t kTuning				= 1u << 26;
	constexpr uint32_t kAllUnderlyingErrors	= 0x07ff0000u;
} // namespace irq


// Interrupt-status bit groups the engine acts on, kept beside the bit names so
// the "which bits matter" policy lives in one host-testable place instead of
// buried in the engine's translation unit.
//
//   kDrainMask        -- completion/timeout/error evidence retained by the
//                        worker and acknowledged snapshot-by-snapshot.
//   kStatusEnableMask -- only bits the worker understands are allowed to latch.
//   kSignalMask       -- the same command sources may pulse the ISR "meow".
//
// Card insertion/removal are intentionally absent. The Controller's slow
// present-state watcher owns hot-plug detection, and signal-enabling card events
// would let an otherwise irrelevant level-triggered bit storm the command path.
constexpr uint32_t kDrainMask = irq::kCommandComplete | irq::kTransferComplete
	| irq::kBufferReadReady | irq::kAllUnderlyingErrors | irq::kErrorBit;
constexpr uint32_t kStatusEnableMask = kDrainMask;
constexpr uint32_t kSignalMask = kDrainMask;


// A spurious "meow": the status word is all-zeros (nothing latched) or all-ones
// (an unpopulated/floating IRQ line). Neither reflects real completion.
constexpr bool
IsSpuriousMeow(uint32_t intStatus) noexcept
{
	return intStatus == 0x00000000u || intStatus == 0xffffffffu;
}


// The ISR's whole filter: reject empty/floating words and status sources that
// this driver did not signal-enable. It deliberately does not classify command
// meaning; a plausible source remains only a content-free wake hint.
constexpr bool
IsActionableMeow(uint32_t intStatus) noexcept
{
	return !IsSpuriousMeow(intStatus) && (intStatus & kSignalMask) != 0;
}


// Acknowledge exactly the evidence the convergence policy knows how to retain.
// Never write a spurious all-ones word back to the MMIO register.
constexpr uint32_t
InterruptBitsToAcknowledge(uint32_t intStatus) noexcept
{
	return IsSpuriousMeow(intStatus) ? 0u : (intStatus & kDrainMask);
}


// SDHCI often reports CommandComplete before TransferComplete. The worker must
// clear each observed hardware snapshot to lower the level-triggered IRQ line,
// then retain those bits in software until the command reaches a terminal
// verdict. Later errors therefore still outrank earlier partial completion.
constexpr uint32_t
AccumulateInterruptStatus(uint32_t accumulated, uint32_t snapshot) noexcept
{
	return accumulated | InterruptBitsToAcknowledge(snapshot);
}


// What the worker must write-1-to-clear when it wakes with NO pending
// transaction. The SDHCI interrupt line is level-triggered, so a signal-enabled
// late completion/error bit would otherwise re-fire the ISR forever. There is
// no command result to preserve on the idle path, so every signal-enabled bit is
// cleared. A spurious all-zero/all-ones word is left untouched.
constexpr uint32_t
StormSafeIdleClear(uint32_t intStatus) noexcept
{
	return IsSpuriousMeow(intStatus) ? 0u : (intStatus & kSignalMask);
}


// The meaning of a single polled interrupt-status word. Terminal verdicts end
// the poll loop; KeepPolling asks for another look.
enum class PollVerdict : uint8_t {
	KeepPolling,
	CommandComplete,
	TransferComplete,
	BufferReadReady,
	CommandTimeout,
	DataTimeout,
	DataCrc,
	AdmaError,
	Error,
};


// Interpret one polled status word. Any underlying error outranks completion:
// controllers may latch TRANSFER_COMPLETE and an ADMA/CRC fault together.
constexpr PollVerdict
InterpretInterruptStatus(uint32_t intStatus) noexcept
{
	if (IsSpuriousMeow(intStatus))
		return PollVerdict::KeepPolling;
	if ((intStatus & irq::kCommandTimeout) != 0)
		return PollVerdict::CommandTimeout;
	if ((intStatus & irq::kDataTimeout) != 0)
		return PollVerdict::DataTimeout;
	if ((intStatus & irq::kDataCrc) != 0)
		return PollVerdict::DataCrc;
	if ((intStatus & irq::kAdma) != 0)
		return PollVerdict::AdmaError;
	if ((intStatus & (irq::kAllUnderlyingErrors | irq::kErrorBit)) != 0)
		return PollVerdict::Error;
	if ((intStatus & irq::kTransferComplete) != 0)
		return PollVerdict::TransferComplete;
	if ((intStatus & irq::kBufferReadReady) != 0)
		return PollVerdict::BufferReadReady;
	if ((intStatus & irq::kCommandComplete) != 0)
		return PollVerdict::CommandComplete;
	return PollVerdict::KeepPolling;
}


// Ordinary (non-tuning) poll classification. BufferReadReady is only terminal
// for tuning; treating it as success for a DMA read could release live DMA
// memory before TransferComplete. Data commands also refuse CommandComplete as
// terminal because the transfer phase must finish first.
constexpr PollVerdict
ClassifyPoll(uint32_t intStatus, bool dataPresent) noexcept
{
	// Spuriousness is judged on the *raw* word: masking a completion bit off a
	// floating all-ones line must not disguise it as a real terminal event.
	if (IsSpuriousMeow(intStatus))
		return PollVerdict::KeepPolling;
	uint32_t effective = intStatus & ~irq::kBufferReadReady;
	if (dataPresent)
		effective &= ~irq::kCommandComplete;
	return InterpretInterruptStatus(effective);
}


constexpr PollVerdict
ClassifyPoll(uint32_t intStatus, bool dataPresent, bool tuning) noexcept
{
	if (!tuning)
		return ClassifyPoll(intStatus, dataPresent);
	if (IsSpuriousMeow(intStatus))
		return PollVerdict::KeepPolling;

	const PollVerdict verdict = InterpretInterruptStatus(intStatus);
	if (verdict == PollVerdict::BufferReadReady
		|| verdict == PollVerdict::CommandTimeout
		|| verdict == PollVerdict::DataTimeout
		|| verdict == PollVerdict::DataCrc
		|| verdict == PollVerdict::AdmaError
		|| verdict == PollVerdict::Error) {
		return verdict;
	}
	return PollVerdict::KeepPolling;
}


// The outcome of a single execution attempt, as classified from the poll
// verdict plus post-checks (OCR sanity). Distinct from status_t so the policy
// stays kernel-free; the engine maps this to status_t at its boundary.
enum class AttemptResult : uint8_t {
	Ok,				// completed and (if applicable) OCR looked sane
	SpuriousOcr,	// completed but the OCR failed validation -> retry quietly
	Busy,			// controller inhibit -> transient
	CommandTimeout,
	DataTimeout,
	DataCrc,
	AdmaError,
	Error,			// hard controller error
};


// What the convergence loop should do after one attempt. The two reset flavors
// mirror the reference worker exactly: a plain line reset recovers the command
// and data state machines cheaply, while a full bus reset (terminate power +
// clock, settle, restore) is the heavy hammer the Bay Trail errata require when
// the SD-domain timeout clock has frozen or the controller is wedged busy.
enum class RetryAction : uint8_t {
	Succeed,			// converged; return success to the caller
	Retry,				// re-issue as-is, no reset (spurious OCR only)
	RetryResetLines,	// reset the command+data lines, then re-issue
	ResetAndReidentify,	// card state was lost; caller must identify again
	Fail,				// give up; propagate the failure
};


// Convergence retry policy. Pure decision over (outcome, attempt, constraints,
// liveness). `cardPresent` reflects the VC-state cache after the attempt.
//
//   attempt      -- 0-based index of the attempt that just ran
//   maxAttempts  -- constraints.Attempts() (>= 1)
//
// The rules mirror the reference worker's _ConvergeCommand:
//   * OCR spuriousness re-issues within budget, never resetting anything (the
//     command *completed*; only its payload was garbage);
//   * any timeout (command or data) or data-CRC fault is one recovery class --
//     abort if the card physically left or the budget is spent, else reset and
//     retry, choosing the full bus reset when the command is flagged for it and
//     the cheap line reset otherwise;
//   * a busy controller is always recovered with a full bus reset;
//   * a hard error never retries.
constexpr RetryAction
DecideRetry(AttemptResult outcome, uint32_t attempt, uint32_t maxAttempts,
	const CommandConstraints& constraints, bool cardPresent) noexcept
{
	const bool haveBudget = (attempt + 1u) < maxAttempts;

	switch (outcome) {
		case AttemptResult::Ok:
			return RetryAction::Succeed;

		case AttemptResult::SpuriousOcr:
			// A completed command with a garbage OCR is not a real success.
			// Retry within budget; if exhausted, treat as failure. No reset:
			// the command line is healthy, only the payload lied.
			return haveBudget ? RetryAction::Retry : RetryAction::Fail;

		case AttemptResult::Busy:
			// No command was issued, so retrying after a host line reset cannot
			// duplicate a write.
			return haveBudget ? RetryAction::RetryResetLines : RetryAction::Fail;

		case AttemptResult::CommandTimeout:
		case AttemptResult::DataTimeout:
		case AttemptResult::DataCrc:
		case AttemptResult::AdmaError:
			// A late ISR may have refreshed presence between attempts; a card
			// that physically left never recovers, and an exhausted budget is
			// terminal. Otherwise recover per the command's reset policy.
			if (!cardPresent || !haveBudget || !constraints.replaySafe)
				return RetryAction::Fail;
			return constraints.needsBusResetOnError
				? RetryAction::ResetAndReidentify : RetryAction::RetryResetLines;

		case AttemptResult::Error:
		default:
			return RetryAction::Fail;
	}
}


} // namespace jr::sdhci
