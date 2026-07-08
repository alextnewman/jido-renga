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
// ("something happened, maybe") -- it never reads state. The worker thread is
// the owner who gets up to see what the device actually wants, by polling.
//
// This header holds the *policy* half of that model, split out from the
// *mechanism* (the register poking in SdhciEngine) so the decisions are pure
// and can be proven off-target:
//
//   InterpretInterruptStatus() -- what does one polled status word mean?
//   IsSpuriousMeow()           -- is this word just line noise?
//   DecideRetry()              -- given one attempt's outcome, what next?
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
	constexpr uint32_t kCardInsertion		= 1u << 6;
	constexpr uint32_t kCardRemoval			= 1u << 7;
	constexpr uint32_t kErrorBit			= 1u << 15;
	constexpr uint32_t kCommandTimeout		= 1u << 16;
	constexpr uint32_t kDataTimeout			= 1u << 20;
	constexpr uint32_t kDataCrc				= 1u << 21;
} // namespace irq


// A spurious "meow": the status word is all-zeros (nothing latched) or all-ones
// (an unpopulated/floating IRQ line). Neither reflects real completion.
constexpr bool
IsSpuriousMeow(uint32_t intStatus) noexcept
{
	return intStatus == 0x00000000u || intStatus == 0xffffffffu;
}


// The meaning of a single polled interrupt-status word. Terminal verdicts end
// the poll loop; KeepPolling asks for another look.
enum class PollVerdict : uint8_t {
	KeepPolling,
	CommandComplete,
	TransferComplete,
	CommandTimeout,
	DataTimeout,
	DataCrc,
	Error,
};


// Interpret one polled status word. Precedence matters: completion bits are
// checked before generic error so an ordinary finish isn't misread, and the
// specific data faults are surfaced distinctly so convergence can recover them.
constexpr PollVerdict
InterpretInterruptStatus(uint32_t intStatus) noexcept
{
	if (IsSpuriousMeow(intStatus))
		return PollVerdict::KeepPolling;
	if ((intStatus & irq::kCommandComplete) != 0)
		return PollVerdict::CommandComplete;
	if ((intStatus & irq::kTransferComplete) != 0)
		return PollVerdict::TransferComplete;
	if ((intStatus & irq::kDataTimeout) != 0)
		return PollVerdict::DataTimeout;
	if ((intStatus & irq::kDataCrc) != 0)
		return PollVerdict::DataCrc;
	if ((intStatus & irq::kCommandTimeout) != 0)
		return PollVerdict::CommandTimeout;
	if ((intStatus & irq::kErrorBit) != 0)
		return PollVerdict::Error;
	return PollVerdict::KeepPolling;
}


// Data-aware poll classification. Identical to InterpretInterruptStatus for a
// non-data command, but for a data command it refuses to treat CommandComplete
// as terminal: the transfer phase must finish first (the controller raises
// command-complete before transfer-complete). This is the single, host-proven
// home of that rule -- the engine calls it instead of inlining the mask, so the
// "cmd-complete precedes xfer-complete" assumption lives in the tested core.
constexpr PollVerdict
ClassifyPoll(uint32_t intStatus, bool dataPresent) noexcept
{
	// Spuriousness is judged on the *raw* word: masking a completion bit off a
	// floating all-ones line must not disguise it as a real terminal event.
	if (IsSpuriousMeow(intStatus))
		return PollVerdict::KeepPolling;
	const uint32_t effective = dataPresent
		? (intStatus & ~irq::kCommandComplete) : intStatus;
	return InterpretInterruptStatus(effective);
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
	Error,			// hard controller error
};


// What the convergence loop should do after one attempt.
enum class RetryAction : uint8_t {
	Succeed,			// converged; return success to the caller
	Retry,				// try again after backoff
	RetryWithBusReset,	// reset the bus, then try again (data recovery)
	Fail,				// give up; propagate the failure
};


// Convergence retry policy. Pure decision over (outcome, attempt, constraints,
// liveness). `cardPresent` reflects the VC-state cache after the attempt.
//
//   attempt      -- 0-based index of the attempt that just ran
//   maxAttempts  -- constraints.Attempts() (>= 1)
//
// The rules mirror the tested worker: OCR spuriousness always retries within
// budget; a genuinely absent card aborts immediately; data faults and busy on
// bus-reset commands recover via reset; everything else fails.
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
			// Retry within budget; if exhausted, treat as failure.
			return haveBudget ? RetryAction::Retry : RetryAction::Fail;

		case AttemptResult::Busy:
			if (!haveBudget)
				return RetryAction::Fail;
			return constraints.needsBusResetOnError
				? RetryAction::RetryWithBusReset : RetryAction::Retry;

		case AttemptResult::CommandTimeout:
			if (!haveBudget)
				return RetryAction::Fail;
			// No point retrying a timeout if the card physically left.
			return cardPresent ? RetryAction::Retry : RetryAction::Fail;

		case AttemptResult::DataTimeout:
		case AttemptResult::DataCrc:
			if (!haveBudget)
				return RetryAction::Fail;
			return constraints.needsBusResetOnError
				? RetryAction::RetryWithBusReset : RetryAction::Retry;

		case AttemptResult::Error:
		default:
			return RetryAction::Fail;
	}
}


} // namespace jr::sdhci
