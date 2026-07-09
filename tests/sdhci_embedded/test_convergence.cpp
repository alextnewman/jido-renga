// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot

#include "framework/jr_test.h"

#include "Convergence.h"

using namespace jr::sdhci;


JR_TEST(convergence, spurious_meow_detection)
{
	JR_CHECK(IsSpuriousMeow(0x00000000u));
	JR_CHECK(IsSpuriousMeow(0xffffffffu));
	JR_CHECK(!IsSpuriousMeow(0x00000001u));
	JR_CHECK(!IsSpuriousMeow(0x00008000u));
}


JR_TEST(convergence, storm_safe_idle_clear)
{
	// A spurious word is left alone: the idle worker must not write a garbage
	// value back onto a floating (all-ones) or quiet (all-zero) line.
	JR_CHECK(StormSafeIdleClear(0x00000000u) == 0u);
	JR_CHECK(StormSafeIdleClear(0xffffffffu) == 0u);

	// The whole point of the idle drain: card insert/remove events are signal-
	// enabled but no command drain clears them, so on a level-triggered line
	// they would meow forever. The idle clear must write them back (write-1-to-
	// clear), i.e. they must survive the mask.
	JR_CHECK((kSignalMask & irq::kCardInsertion) != 0u);
	JR_CHECK((kSignalMask & irq::kCardRemoval) != 0u);
	JR_CHECK(StormSafeIdleClear(irq::kCardInsertion) == irq::kCardInsertion);
	JR_CHECK(StormSafeIdleClear(irq::kCardRemoval) == irq::kCardRemoval);

	// A stray completion word latched with no pending transaction is cleared too
	// (it is in the signal-enabled set), so nothing keeps the line asserted.
	JR_CHECK(StormSafeIdleClear(irq::kCommandComplete) == irq::kCommandComplete);

	// Bits outside the signal-enabled set are not our concern on the idle path;
	// they cannot assert the line, so we leave them untouched rather than risk a
	// spurious write to a register we do not manage.
	const uint32_t foreign = 1u << 30;
	JR_CHECK((kSignalMask & foreign) == 0u);
	JR_CHECK(StormSafeIdleClear(foreign | irq::kCardRemoval) == irq::kCardRemoval);
}


// The per-attempt drain and the signal-enable set must agree on the command
// verdict bits (a completion/timeout/error the poll loop latched has to be both
// signal-enabled to meow and drained to clear), or a converged command could
// leave a bit latched that then storms the following idle wait.
JR_TEST(convergence, drain_and_signal_masks_cover_command_bits)
{
	const uint32_t commandBits = irq::kCommandComplete | irq::kTransferComplete
		| irq::kCommandTimeout | irq::kDataTimeout;
	JR_CHECK((kDrainMask & commandBits) == commandBits);
	JR_CHECK((kSignalMask & commandBits) == commandBits);
	// Every signal-enabled command bit (i.e. not the card events) is drainable.
	const uint32_t cardBits = irq::kCardInsertion | irq::kCardRemoval;
	JR_CHECK(((kSignalMask & ~cardBits) & ~kDrainMask) == 0u);
}


JR_TEST(convergence, interpret_terminal_bits)
{
	JR_CHECK(InterpretInterruptStatus(0) == PollVerdict::KeepPolling);
	JR_CHECK(InterpretInterruptStatus(0xffffffff) == PollVerdict::KeepPolling);
	JR_CHECK(InterpretInterruptStatus(irq::kCommandComplete)
		== PollVerdict::CommandComplete);
	JR_CHECK(InterpretInterruptStatus(irq::kTransferComplete)
		== PollVerdict::TransferComplete);
	JR_CHECK(InterpretInterruptStatus(irq::kDataTimeout)
		== PollVerdict::DataTimeout);
	JR_CHECK(InterpretInterruptStatus(irq::kDataCrc)
		== PollVerdict::DataCrc);
	JR_CHECK(InterpretInterruptStatus(irq::kCommandTimeout)
		== PollVerdict::CommandTimeout);
	JR_CHECK(InterpretInterruptStatus(irq::kErrorBit)
		== PollVerdict::Error);
}


JR_TEST(convergence, completion_wins_over_error)
{
	// A finish that also latched the error summary bit must read as complete.
	JR_CHECK(InterpretInterruptStatus(irq::kCommandComplete | irq::kErrorBit)
		== PollVerdict::CommandComplete);
	// A specific data fault must surface distinctly, not as generic Error.
	JR_CHECK(InterpretInterruptStatus(irq::kDataTimeout | irq::kErrorBit)
		== PollVerdict::DataTimeout);
}


JR_TEST(convergence, retry_ok_always_succeeds)
{
	CommandConstraints c;
	JR_CHECK(DecideRetry(AttemptResult::Ok, 0, 1, c, true)
		== RetryAction::Succeed);
	JR_CHECK(DecideRetry(AttemptResult::Ok, 5, 3, c, false)
		== RetryAction::Succeed);
}


JR_TEST(convergence, retry_spurious_ocr_within_budget)
{
	CommandConstraints c; c.maxRetries = 20;
	JR_CHECK(DecideRetry(AttemptResult::SpuriousOcr, 0, 21, c, true)
		== RetryAction::Retry);
	// Last attempt exhausted -> fail.
	JR_CHECK(DecideRetry(AttemptResult::SpuriousOcr, 20, 21, c, true)
		== RetryAction::Fail);
}


JR_TEST(convergence, retry_command_timeout_needs_card)
{
	CommandConstraints c; c.maxRetries = 3;
	// A plain command timeout recovers with a cheap line reset...
	JR_CHECK(DecideRetry(AttemptResult::CommandTimeout, 0, 4, c, true)
		== RetryAction::RetryResetLines);
	// ...but a command flagged for bus reset (CMD7/CMD6 on BYT) uses the hammer.
	CommandConstraints busReset; busReset.maxRetries = 3;
	busReset.needsBusResetOnError = true;
	JR_CHECK(DecideRetry(AttemptResult::CommandTimeout, 0, 4, busReset, true)
		== RetryAction::RetryWithBusReset);
	// Card gone -> abort even with budget left.
	JR_CHECK(DecideRetry(AttemptResult::CommandTimeout, 0, 4, c, false)
		== RetryAction::Fail);
	// Budget spent -> fail regardless of presence.
	JR_CHECK(DecideRetry(AttemptResult::CommandTimeout, 3, 4, busReset, true)
		== RetryAction::Fail);
}


JR_TEST(convergence, retry_data_fault_uses_bus_reset)
{
	CommandConstraints reset; reset.maxRetries = 3;
	reset.needsBusResetOnError = true;
	CommandConstraints plain; plain.maxRetries = 3;

	JR_CHECK(DecideRetry(AttemptResult::DataTimeout, 0, 4, reset, true)
		== RetryAction::RetryWithBusReset);
	JR_CHECK(DecideRetry(AttemptResult::DataCrc, 0, 4, reset, true)
		== RetryAction::RetryWithBusReset);
	// Without the bus-reset flag a data fault still recovers, via line reset.
	JR_CHECK(DecideRetry(AttemptResult::DataTimeout, 0, 4, plain, true)
		== RetryAction::RetryResetLines);
	// Exhausted budget -> fail regardless of reset policy.
	JR_CHECK(DecideRetry(AttemptResult::DataCrc, 3, 4, reset, true)
		== RetryAction::Fail);
}


JR_TEST(convergence, retry_busy_always_bus_resets)
{
	// The reference worker recovers a busy controller with a full bus reset
	// whether or not the command carries the bus-reset quirk.
	CommandConstraints reset; reset.maxRetries = 1;
	reset.needsBusResetOnError = true;
	JR_CHECK(DecideRetry(AttemptResult::Busy, 0, 2, reset, true)
		== RetryAction::RetryWithBusReset);

	CommandConstraints plain; plain.maxRetries = 1;
	JR_CHECK(DecideRetry(AttemptResult::Busy, 0, 2, plain, true)
		== RetryAction::RetryWithBusReset);

	// Exhausted budget -> fail.
	JR_CHECK(DecideRetry(AttemptResult::Busy, 1, 2, reset, true)
		== RetryAction::Fail);
}


JR_TEST(convergence, retry_spurious_ocr_never_resets)
{
	// A garbage OCR means "re-issue as-is" -- the command line is healthy, so
	// the recovery must be a plain Retry, never a line or bus reset.
	CommandConstraints c; c.maxRetries = 20;
	c.needsBusResetOnError = true;	// even so: OCR retry does not reset
	JR_CHECK(DecideRetry(AttemptResult::SpuriousOcr, 0, 21, c, true)
		== RetryAction::Retry);
}


JR_TEST(convergence, retry_error_never_retries)
{
	CommandConstraints c; c.maxRetries = 5;
	// Hard error never retries.
	JR_CHECK(DecideRetry(AttemptResult::Error, 0, 6, c, true)
		== RetryAction::Fail);
}
