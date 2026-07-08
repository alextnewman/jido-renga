// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot

#include "framework/jr_test.h"

#include "Command.h"

using namespace jr::sdhci;

namespace {
constexpr Quirk kByt = Quirk::PowerOnDelay | Quirk::TimeoutClockFromSdClock
	| Quirk::StopTransmissionBusy | Quirk::CardOnNeedsBusOn;
}


JR_TEST(command, op_cond_gets_retries_and_ocr_on_baytrail)
{
	const CommandConstraints c = GetCommandConstraints(Cmd::MmcSendOpCond, kByt);
	JR_CHECK_EQ(c.maxRetries, 20);
	JR_CHECK(c.validateOcr);
	JR_CHECK_EQ(c.Attempts(), 21u);
}


JR_TEST(command, op_cond_is_plain_without_quirks)
{
	const CommandConstraints c
		= GetCommandConstraints(Cmd::MmcSendOpCond, Quirk::None);
	JR_CHECK_EQ(c.maxRetries, 0);
	JR_CHECK(!c.validateOcr);
	JR_CHECK_EQ(c.Attempts(), 1u);
}


JR_TEST(command, app_cmd_retries_short_timeout)
{
	const CommandConstraints c = GetCommandConstraints(Cmd::AppCmd, kByt);
	JR_CHECK_EQ(c.maxRetries, 3);
	JR_CHECK_EQ(c.timeoutMs, 200);
}


JR_TEST(command, switch_needs_bus_reset)
{
	const CommandConstraints c = GetCommandConstraints(Cmd::MmcSwitch, kByt);
	JR_CHECK_EQ(c.maxRetries, 3);
	JR_CHECK_EQ(c.timeoutMs, 2000);
	JR_CHECK(c.needsBusResetOnError);
}


JR_TEST(command, data_commands_recover_via_bus_reset)
{
	for (Cmd cmd : {Cmd::ReadSingleBlock, Cmd::WriteSingleBlock,
			Cmd::ReadMultipleBlocks, Cmd::WriteMultipleBlocks}) {
		const CommandConstraints c = GetCommandConstraints(cmd, kByt);
		JR_CHECK_EQ(c.maxRetries, 3);
		JR_CHECK_EQ(c.timeoutMs, 5000);
		JR_CHECK(c.needsBusResetOnError);
	}
}


JR_TEST(command, go_idle_has_no_constraints)
{
	const CommandConstraints c = GetCommandConstraints(Cmd::GoIdleState, kByt);
	JR_CHECK_EQ(c.maxRetries, 0);
	JR_CHECK(!c.needsBusResetOnError);
	JR_CHECK(!c.validateOcr);
}


JR_TEST(command, select_card_needs_bus_reset_and_retries)
{
	const CommandConstraints c
		= GetCommandConstraints(Cmd::SelectDeselectCard, kByt);
	JR_CHECK(c.needsBusResetOnError);
	JR_CHECK_EQ(c.maxRetries, 3);
	JR_CHECK_EQ(c.timeoutMs, 2000);
}


JR_TEST(command, transfer_mode_words)
{
	JR_CHECK_EQ(ComputeTransferMode(Cmd::ReadSingleBlock), 0x0011);
	JR_CHECK_EQ(ComputeTransferMode(Cmd::ReadMultipleBlocks), 0x0037);
	JR_CHECK_EQ(ComputeTransferMode(Cmd::WriteSingleBlock), 0x0001);
	JR_CHECK_EQ(ComputeTransferMode(Cmd::WriteMultipleBlocks), 0x0027);
	JR_CHECK_EQ(ComputeTransferMode(Cmd::GoIdleState), 0x0000);
	JR_CHECK_EQ(ComputeTransferMode(Cmd::SendCsd), 0x0000);
}
