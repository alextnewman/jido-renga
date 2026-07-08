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
	JR_CHECK(DecideRetry(AttemptResult::CommandTimeout, 0, 4, c, true)
		== RetryAction::Retry);
	// Card gone -> abort even with budget left.
	JR_CHECK(DecideRetry(AttemptResult::CommandTimeout, 0, 4, c, false)
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
	JR_CHECK(DecideRetry(AttemptResult::DataTimeout, 0, 4, plain, true)
		== RetryAction::Retry);
	// Exhausted budget -> fail regardless of reset policy.
	JR_CHECK(DecideRetry(AttemptResult::DataCrc, 3, 4, reset, true)
		== RetryAction::Fail);
}


JR_TEST(convergence, retry_busy_and_error)
{
	CommandConstraints reset; reset.maxRetries = 1;
	reset.needsBusResetOnError = true;
	JR_CHECK(DecideRetry(AttemptResult::Busy, 0, 2, reset, true)
		== RetryAction::RetryWithBusReset);

	CommandConstraints c; c.maxRetries = 5;
	// Hard error never retries.
	JR_CHECK(DecideRetry(AttemptResult::Error, 0, 6, c, true)
		== RetryAction::Fail);
}
