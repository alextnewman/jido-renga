// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot

#include "Card.h"

#include <new>

#include "Command.h"
#include "SdhciEngine.h"

// Card base + dialect factory. Probe() runs the dialect-neutral opening moves
// (CMD0, CMD8) then instantiates the concrete Card that speaks the right
// language. The identify sequences themselves live in SdCard.cpp / MmcCard.cpp.

namespace jr::sdhci {


Card*
Card::Probe(SdhciEngine& engine)
{
	CommandOutcome outcome;

	// eMMC vendor hardware reset (no-op without the quirk): force a clean card
	// state before the very first command.
	engine.EmmcHardwareReset();

	// CMD0: reset every card on the bus to the idle state. A timeout here means
	// the slot is empty (removable) -- there is nothing to identify.
	if (engine.Execute(Cmd::GoIdleState, 0, ReplyType::None, outcome) != B_OK)
		return nullptr;

	// Spec requires >= 8 clocks after CMD0; Bay Trail needs far longer (~20ms)
	// or the next command times out.
	snooze(30000);

	// CMD8 (SEND_IF_COND): SD 2.0+ echoes the check pattern; eMMC (which reads
	// CMD8 as the 512-byte EXT_CSD data command) and SD v1 do not answer.
	// 0x1AA = 2.7-3.6V, check pattern 0xAA.
	const bool isSd = engine.Execute(Cmd::SendIfCond, 0x1aa, ReplyType::R7,
		outcome) == B_OK && (outcome.response[0] & 0xff) == 0xaa;

	Card* card = nullptr;
	if (isSd) {
		card = new(std::nothrow) SdCard;
		engine.SetDialect(CardDialect::Sd);
	} else {
		// CMD8 with no data phase can leave an eMMC in an error state; reset the
		// bus to clear it before attempting the MMC (CMD1) identification.
		engine.RecoverBus();
		card = new(std::nothrow) MmcCard;
		engine.SetDialect(CardDialect::Mmc);
	}

	if (card == nullptr)
		return nullptr;

	if (card->Identify(engine) != B_OK) {
		delete card;
		return nullptr;
	}
	return card;
}


} // namespace jr::sdhci
