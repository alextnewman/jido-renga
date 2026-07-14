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


JR_TEST(convergence, isr_only_wakes_for_managed_sources)
{
	JR_CHECK(!IsActionableMeow(0x00000000u));
	JR_CHECK(!IsActionableMeow(0xffffffffu));
	JR_CHECK(!IsActionableMeow(irq::kCardInsertion));
	JR_CHECK(!IsActionableMeow(irq::kCardRemoval));
	JR_CHECK(!IsActionableMeow(1u << 30));
	JR_CHECK(IsActionableMeow(irq::kCommandComplete));
	JR_CHECK(IsActionableMeow(irq::kTransferComplete | irq::kCardInsertion));
	JR_CHECK(IsActionableMeow(irq::kDataCrc | irq::kErrorBit));
}


JR_TEST(convergence, storm_safe_idle_clear)
{
	// A spurious word is left alone: the idle worker must not write a garbage
	// value back onto a floating (all-ones) or quiet (all-zero) line.
	JR_CHECK(StormSafeIdleClear(0x00000000u) == 0u);
	JR_CHECK(StormSafeIdleClear(0xffffffffu) == 0u);

	// Hot-plug uses present-state polling, so card events never participate in
	// the command IRQ line and the idle drain does not claim them.
	JR_CHECK((kStatusEnableMask & irq::kCardInsertion) == 0u);
	JR_CHECK((kStatusEnableMask & irq::kCardRemoval) == 0u);
	JR_CHECK((kSignalMask & irq::kCardInsertion) == 0u);
	JR_CHECK((kSignalMask & irq::kCardRemoval) == 0u);
	JR_CHECK(StormSafeIdleClear(irq::kCardInsertion) == 0u);
	JR_CHECK(StormSafeIdleClear(irq::kCardRemoval) == 0u);

	// A stray completion word latched with no pending transaction is cleared too
	// (it is in the signal-enabled set), so nothing keeps the line asserted.
	JR_CHECK(StormSafeIdleClear(irq::kCommandComplete) == irq::kCommandComplete);

	// Bits outside the signal-enabled set cannot assert this line and must not
	// be acknowledged by the idle path.
	const uint32_t foreign = 1u << 30;
	JR_CHECK((kSignalMask & foreign) == 0u);
	JR_CHECK(StormSafeIdleClear(foreign | irq::kCommandComplete)
		== irq::kCommandComplete);
}


JR_TEST(convergence, status_signal_and_ack_masks_agree)
{
	const uint32_t commandBits = irq::kCommandComplete | irq::kTransferComplete
		| irq::kErrorBit | irq::kAllUnderlyingErrors;
	JR_CHECK((kDrainMask & commandBits) == commandBits);
	JR_CHECK((kStatusEnableMask & commandBits) == commandBits);
	JR_CHECK((kSignalMask & commandBits) == commandBits);
	JR_CHECK_EQ(kStatusEnableMask, kDrainMask);
	JR_CHECK_EQ(kSignalMask, kDrainMask);

	JR_CHECK_EQ(InterruptBitsToAcknowledge(0), 0u);
	JR_CHECK_EQ(InterruptBitsToAcknowledge(0xffffffffu), 0u);
	JR_CHECK_EQ(InterruptBitsToAcknowledge(
		irq::kCommandComplete | irq::kCardInsertion | (1u << 30)),
		irq::kCommandComplete);
}


JR_TEST(convergence, split_completion_accumulates_in_software)
{
	uint32_t status = 0;

	status = AccumulateInterruptStatus(status, irq::kCommandComplete);
	JR_CHECK_EQ(status, irq::kCommandComplete);
	JR_CHECK(ClassifyPoll(status, true) == PollVerdict::KeepPolling);

	// A duplicate hint is harmless and cannot grow or change the evidence.
	status = AccumulateInterruptStatus(status, irq::kCommandComplete);
	JR_CHECK_EQ(status, irq::kCommandComplete);

	// TransferComplete may arrive in a later hardware snapshot after the worker
	// has already acknowledged CommandComplete to lower the IRQ line.
	status = AccumulateInterruptStatus(status, irq::kTransferComplete);
	JR_CHECK_EQ(status, irq::kCommandComplete | irq::kTransferComplete);
	JR_CHECK(ClassifyPoll(status, true) == PollVerdict::TransferComplete);
}


JR_TEST(convergence, later_error_overrides_partial_completion)
{
	uint32_t status
		= AccumulateInterruptStatus(0, irq::kCommandComplete);
	JR_CHECK(ClassifyPoll(status, true) == PollVerdict::KeepPolling);

	status = AccumulateInterruptStatus(status,
		irq::kDataCrc | irq::kErrorBit);
	JR_CHECK(ClassifyPoll(status, true) == PollVerdict::DataCrc);
}


JR_TEST(convergence, buffer_ready_is_terminal_only_for_tuning)
{
	JR_CHECK(ClassifyPoll(irq::kBufferReadReady, false)
		== PollVerdict::KeepPolling);
	JR_CHECK(ClassifyPoll(irq::kBufferReadReady, true, false)
		== PollVerdict::KeepPolling);
	JR_CHECK(ClassifyPoll(irq::kCommandComplete | irq::kBufferReadReady,
		true, false) == PollVerdict::KeepPolling);
	JR_CHECK(ClassifyPoll(irq::kBufferReadReady, true, true)
		== PollVerdict::BufferReadReady);

	// TransferComplete remains authoritative if buffer readiness was also
	// observed during an ordinary DMA read.
	JR_CHECK(ClassifyPoll(irq::kBufferReadReady | irq::kTransferComplete,
		true, false) == PollVerdict::TransferComplete);
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
	JR_CHECK(InterpretInterruptStatus(irq::kAdma)
		== PollVerdict::AdmaError);
	JR_CHECK(InterpretInterruptStatus(irq::kCommandTimeout)
		== PollVerdict::CommandTimeout);
	JR_CHECK(InterpretInterruptStatus(irq::kErrorBit)
		== PollVerdict::Error);
}


JR_TEST(convergence, error_wins_over_completion)
{
	// Completion can be latched alongside the failing data phase.
	JR_CHECK(InterpretInterruptStatus(irq::kCommandComplete | irq::kErrorBit)
		== PollVerdict::Error);
	JR_CHECK(InterpretInterruptStatus(irq::kTransferComplete | irq::kAdma)
		== PollVerdict::AdmaError);
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
		== RetryAction::ResetAndReidentify);
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
		== RetryAction::ResetAndReidentify);
	JR_CHECK(DecideRetry(AttemptResult::DataCrc, 0, 4, reset, true)
		== RetryAction::ResetAndReidentify);
	// Without the bus-reset flag a data fault still recovers, via line reset.
	JR_CHECK(DecideRetry(AttemptResult::DataTimeout, 0, 4, plain, true)
		== RetryAction::RetryResetLines);
	// Exhausted budget -> fail regardless of reset policy.
	JR_CHECK(DecideRetry(AttemptResult::DataCrc, 3, 4, reset, true)
		== RetryAction::Fail);
}


JR_TEST(convergence, busy_resets_lines_without_replaying_a_command)
{
	// The reference worker recovers a busy controller with a full bus reset
	// whether or not the command carries the bus-reset quirk.
	CommandConstraints reset; reset.maxRetries = 1;
	reset.needsBusResetOnError = true;
	JR_CHECK(DecideRetry(AttemptResult::Busy, 0, 2, reset, true)
		== RetryAction::RetryResetLines);

	CommandConstraints plain; plain.maxRetries = 1;
	JR_CHECK(DecideRetry(AttemptResult::Busy, 0, 2, plain, true)
		== RetryAction::RetryResetLines);

	// Exhausted budget -> fail.
	JR_CHECK(DecideRetry(AttemptResult::Busy, 1, 2, reset, true)
		== RetryAction::Fail);
}


JR_TEST(convergence, uncertain_write_is_never_replayed)
{
	CommandConstraints write;
	write.maxRetries = 3;
	write.replaySafe = false;
	JR_CHECK(DecideRetry(AttemptResult::DataTimeout, 0, 4, write, true)
		== RetryAction::Fail);
	JR_CHECK(DecideRetry(AttemptResult::AdmaError, 0, 4, write, true)
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
