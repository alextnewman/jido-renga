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

	// CMD0: reset every card on the bus to the idle state.
	engine.Execute(Cmd::GoIdleState, 0, ReplyType::None, outcome);

	// CMD8 (SEND_IF_COND): SD 2.0+ echoes the check pattern; eMMC ignores it.
	// 0x1AA = 2.7-3.6V, check pattern 0xAA.
	const bool isSd = engine.Execute(Cmd::SendIfCond, 0x1aa, ReplyType::R7,
		outcome) == B_OK && (outcome.response[0] & 0xff) == 0xaa;

	Card* card = nullptr;
	if (isSd) {
		card = new(std::nothrow) SdCard;
		engine.SetDialect(CardDialect::Sd);
	} else {
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
