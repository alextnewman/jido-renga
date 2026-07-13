// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot

#include "Card.h"

#include <new>

#include "Command.h"
#include "SdhciEngine.h"

// Card base + BSP-directed dialect factory.

namespace jr::sdhci {


Card*
Card::Create(SdhciEngine& engine, CardDialect dialect)
{
	engine.SetDialect(dialect);
	for (int attempt = 0; attempt < 2; attempt++) {
		if (dialect == CardDialect::Mmc)
			engine.EmmcHardwareReset();

		CommandOutcome outcome;
		if (engine.Execute(Cmd::GoIdleState, 0, ReplyType::None, outcome) != B_OK)
			return nullptr;
		snooze(30000);

		bool version2 = false;
		if (dialect == CardDialect::Sd) {
			version2 = engine.Execute(Cmd::SendIfCond, 0x1aa, ReplyType::R7,
				outcome) == B_OK && (outcome.response[0] & 0xfff) == 0x1aa;
		}

		Card* card = dialect == CardDialect::Sd
			? static_cast<Card*>(new(std::nothrow) SdCard(version2))
			: static_cast<Card*>(new(std::nothrow) MmcCard);
		if (card == nullptr)
			return nullptr;

		const status_t status = card->Identify(engine);
		if (status == B_OK)
			return card;
		JR_WARN(engine.Label(), "%s identification attempt %d failed: %s\n",
			DialectLabel(dialect), attempt + 1, strerror(status));
		delete card;

		if (attempt == 0)
			engine.RecoverBus();
	}
	return nullptr;
}


} // namespace jr::sdhci
