// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot

#include "Card.h"

#include "Command.h"
#include "Csd.h"
#include "SdhciEngine.h"

// SD dialect identification: ACMD41 (with the CMD55 prefix) to leave idle,
// CMD2 for the CID, CMD3 to obtain the published RCA, CMD9 for the CSD (whence
// geometry), then CMD7 to select the card into transfer state.

namespace jr::sdhci {


namespace {

// The application-command prefix (CMD55) that must precede any ACMD.
status_t
SendAppPrefix(SdhciEngine& engine, uint16_t rca)
{
	CommandOutcome outcome;
	return engine.Execute(Cmd::AppCmd, static_cast<uint32_t>(rca) << 16,
		ReplyType::R1, outcome);
}

} // namespace


status_t
SdCard::Identify(SdhciEngine& engine)
{
	CommandOutcome outcome;

	// ACMD41 (SD_SEND_OP_COND): negotiate voltage and wait for power-up. HCS bit
	// (30) requests high-capacity addressing; the convergence budget in
	// GetSdOpCondConstraints() handles the busy-poll retries.
	const CommandConstraints opCond = GetSdOpCondConstraints(Quirk::None);
	status_t status = B_ERROR;
	for (int i = 0; i < 64; i++) {
		if (SendAppPrefix(engine, 0) != B_OK)
			continue;
		status = engine.Execute(Cmd::MmcSendOpCond, 0x40ff8000, ReplyType::R3,
			opCond, outcome);
		if (status == B_OK && (outcome.response[0] & (1u << 31)) != 0) {
			fHighCapacity = (outcome.response[0] & (1u << 30)) != 0;
			break;
		}
		status = B_TIMED_OUT;
	}
	if (status != B_OK)
		return status;

	// CMD2 (ALL_SEND_CID).
	if (engine.Execute(Cmd::AllSendCid, 0, ReplyType::R2, outcome) != B_OK)
		return B_ERROR;
	for (int i = 0; i < 4; i++)
		fCid[i] = outcome.response[i];

	// CMD3 (SEND_RELATIVE_ADDR): the card proposes its RCA.
	if (engine.Execute(Cmd::SendRelativeAddr, 0, ReplyType::R6, outcome) != B_OK)
		return B_ERROR;
	fRca = static_cast<uint16_t>(outcome.response[0] >> 16);

	// CMD9 (SEND_CSD): geometry.
	if (engine.Execute(Cmd::SendCsd, static_cast<uint32_t>(fRca) << 16,
			ReplyType::R2, outcome) != B_OK) {
		return B_ERROR;
	}
	CardGeometry geometry = DecodeSdCsd(outcome.response);
	if (!geometry.valid)
		return B_ERROR;
	fSectorCount = geometry.blockCount;
	fSectorSize = 512;

	// CMD7 (SELECT_CARD): move into the transfer state.
	if (engine.Execute(Cmd::SelectDeselectCard, static_cast<uint32_t>(fRca) << 16,
			ReplyType::R1b, outcome) != B_OK) {
		return B_ERROR;
	}

	engine.SetBusWidth(4);
	return B_OK;
}


} // namespace jr::sdhci
