// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot

#include "Card.h"

#include "Command.h"
#include "Csd.h"
#include "SdhciEngine.h"

// eMMC dialect identification: CMD1 (SEND_OP_COND) with sector-mode request,
// CMD2 for the CID, CMD3 to *assign* an RCA (the host chooses it for eMMC),
// CMD9 for the CSD, CMD7 to select, then EXT_CSD (CMD8) for the true SEC_COUNT
// on >2 GiB parts. This is the Bay Trail target path.

namespace jr::sdhci {


static constexpr uint16_t kEmmcRca = 0x0001;	// host-assigned RCA for eMMC


status_t
MmcCard::Identify(SdhciEngine& engine)
{
	CommandOutcome outcome;

	// CMD1 (SEND_OP_COND): access-mode + HCS (bit 31 valid-marker per the
	// reference) + sector addressing (bit 30) + voltage window. The op-cond
	// constraints carry the long busy-poll retry budget with OCR validation,
	// which the Bay Trail personality needs to reject garbage.
	const CommandConstraints opCond = GetCommandConstraints(Cmd::MmcSendOpCond,
		Quirk::None);
	status_t status = B_ERROR;
	for (int i = 0; i < 64; i++) {
		status = engine.Execute(Cmd::MmcSendOpCond, 0xc0ff8000, ReplyType::R3,
			opCond, outcome);
		if (status == B_OK && (outcome.response[0] & (1u << 31)) != 0) {
			// Bit 30 echoed back means the card accepted sector addressing
			// (extended capacity, > 2 GiB); otherwise it is byte-addressed.
			fSectorAddressing = (outcome.response[0] & (1u << 30)) != 0;
			break;
		}
		// Card still busy (bit 31 clear): wait for internal power-up.
		snooze(200000);
		status = B_TIMED_OUT;
	}
	if (status != B_OK)
		return status;

	// CMD2 (ALL_SEND_CID).
	if (engine.Execute(Cmd::AllSendCid, 0, ReplyType::R2, outcome) != B_OK)
		return B_ERROR;
	for (int i = 0; i < 4; i++)
		fCid[i] = outcome.response[i];

	// CMD3 (SET_RELATIVE_ADDR): for eMMC the *host* assigns the RCA.
	fRca = kEmmcRca;
	if (engine.Execute(Cmd::SendRelativeAddr, static_cast<uint32_t>(fRca) << 16,
			ReplyType::R1, outcome) != B_OK) {
		return B_ERROR;
	}

	// CMD9 (SEND_CSD): coarse geometry (and CSD version).
	if (engine.Execute(Cmd::SendCsd, static_cast<uint32_t>(fRca) << 16,
			ReplyType::R2, outcome) != B_OK) {
		return B_ERROR;
	}
	CardGeometry geometry = DecodeSdCsd(outcome.response);
	fSectorCount = geometry.blockCount;
	fSectorSize = 512;

	// CMD7 (SELECT_CARD).
	if (engine.Execute(Cmd::SelectDeselectCard, static_cast<uint32_t>(fRca) << 16,
			ReplyType::R1b, outcome) != B_OK) {
		return B_ERROR;
	}

	// EXT_CSD (CMD8) carries the authoritative SEC_COUNT for >2 GiB eMMC, which
	// the CSD alone caps at 2 GiB. Read the 512-byte block over single-buffer
	// SDMA (the proven DMA mode) and let SEC_COUNT supersede the CSD estimate
	// when present. NOTE: the eMMC data path is unproven on this hardware (see
	// improvement log); the pure decoder is host-tested, but this read is not.
	uint8_t extCsd[512];
	if (engine.ReadDataBlock(Cmd::SendExtCsd, 0, ReplyType::R1, extCsd,
			sizeof(extCsd)) == B_OK) {
		const CardGeometry extGeometry = DecodeEmmcSectorCount(extCsd);
		if (extGeometry.blockCount != 0)
			fSectorCount = extGeometry.blockCount;
	}

	// Bus stays 1-bit / 25 MHz: the reference attempts the device-first CMD6
	// HS + 8-bit upgrade in the disk layer, but that path was never made to work
	// on this hardware. Keeping identification width avoids faking a broken
	// upgrade; the proper redesign is a deferred speed item (see improvement log).
	return B_OK;
}


} // namespace jr::sdhci
