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


// Regression guard: the engine writes the raw enum value to the command
// register, so SD's ACMD41 (opcode 41, after a CMD55 prefix) must NOT be issued
// with eMMC CMD1's opcode (1). Conflating them silently sent ACMD1 and broke SD
// power-up negotiation on the known-good path.
JR_TEST(command, sd_op_cond_opcode_is_acmd41_not_cmd1)
{
	JR_CHECK_EQ(static_cast<uint8_t>(Cmd::SdSendOpCond), 41u);
	JR_CHECK_EQ(static_cast<uint8_t>(Cmd::MmcSendOpCond), 1u);
	JR_CHECK(Cmd::SdSendOpCond != Cmd::MmcSendOpCond);
}


// ACMD41 has no data phase, so its transfer-mode word must be zero (no DMA/read
// bits) even though it shares CMD1's retry policy.
JR_TEST(command, sd_op_cond_has_no_data_phase)
{
	JR_CHECK_EQ(ComputeTransferMode(Cmd::SdSendOpCond), 0x0000);
}


// The SD op-cond helper must resolve to the exact same power-on-tolerant policy
// as eMMC CMD1 (long retry budget + OCR validation under the Bay Trail quirks).
JR_TEST(command, sd_op_cond_shares_cmd1_policy)
{
	const CommandConstraints sd = GetSdOpCondConstraints(kByt);
	const CommandConstraints mmc = GetCommandConstraints(Cmd::MmcSendOpCond, kByt);
	JR_CHECK_EQ(sd.maxRetries, mmc.maxRetries);
	JR_CHECK_EQ(sd.maxRetries, 20);
	JR_CHECK(sd.validateOcr);
	JR_CHECK_EQ(sd.validateOcr, mmc.validateOcr);
}
