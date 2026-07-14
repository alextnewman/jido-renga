// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot

#include "framework/jr_test.h"

#include "sdhci_embedded/FakeController.h"

#include "Contract.h"
#include "Convergence.h"

using namespace jr::sdhci;


// Drive the real snapshot accumulator and poll classifier over the
// FakeController's one-shot status evolution, exactly as the engine worker does.
static uint32_t
DriveToDone(const jr::test::FakeController& hw, bool dataPresent, uint32_t& atPoll)
{
	uint32_t accumulated = 0;
	for (uint32_t n = 0; n < 100; n++) {
		accumulated = AccumulateInterruptStatus(accumulated, hw.StatusAtPoll(n));
		const PollVerdict verdict = ClassifyPoll(accumulated, dataPresent);
		if (verdict == PollVerdict::CommandComplete
			|| verdict == PollVerdict::TransferComplete) {
			atPoll = n;
			return accumulated;
		}
	}
	atPoll = 100;
	return 0;
}


// Data-command completion is justified only by TransferComplete.
JR_TEST(contract, data_command_waits_for_transfer_complete)
{
	jr::test::FakeController hw;
	hw.dataPresent = true;
	hw.cmdCompleteAtPoll = 2;
	hw.xferCompleteAtPoll = 6;

	uint32_t atPoll = 0;
	const uint32_t acted = DriveToDone(hw, /*dataPresent*/ true, atPoll);

	JR_CHECK_EQ(atPoll, 6u);							// not poll 2 (cmd-complete)
	JR_CHECK(contract::CompletionJustified(true, acted));
	JR_CHECK(contract::TerminalStatusIsReal(acted));
}


// A command-only verdict violates the data-command completion contract.
JR_TEST(contract, data_unaware_completion_violates_the_contract)
{
	jr::test::FakeController hw;
	hw.dataPresent = true;

	const uint32_t atCmdComplete = hw.StatusAtPoll(hw.cmdCompleteAtPoll);
	JR_CHECK(InterpretInterruptStatus(atCmdComplete) == PollVerdict::CommandComplete);
	JR_CHECK(!contract::CompletionJustified(true, atCmdComplete));
}


// A non-data command legitimately completes on command-complete, and the
// contract agrees. (The rule must not over-fire and starve ordinary commands.)
JR_TEST(contract, non_data_command_completes_on_command_complete)
{
	jr::test::FakeController hw;
	hw.dataPresent = false;

	uint32_t atPoll = 0;
	const uint32_t acted = DriveToDone(hw, /*dataPresent*/ false, atPoll);

	JR_CHECK_EQ(atPoll, 2u);
	JR_CHECK(contract::CompletionJustified(false, acted));
}


// A spurious meow (floating line / nothing latched) is never terminal: the
// classifier keeps polling and the contract flags the word as unreal.
JR_TEST(contract, spurious_meow_is_never_a_terminal_verdict)
{
	JR_CHECK(ClassifyPoll(0x00000000u, false) == PollVerdict::KeepPolling);
	JR_CHECK(ClassifyPoll(0xffffffffu, true) == PollVerdict::KeepPolling);
	JR_CHECK(!contract::TerminalStatusIsReal(0x00000000u));
	JR_CHECK(!contract::TerminalStatusIsReal(0xffffffffu));
}
