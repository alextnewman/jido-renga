// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot
#pragma once

#include <stdint.h>

#include "Convergence.h"

// Pure predicates shared by host tests and on-target controller checks.

namespace jr::sdhci::contract {


// Data commands require TransferComplete because CommandComplete can precede
// the end of the DMA/data phase.
constexpr bool
CompletionJustified(bool dataPresent, uint32_t actedStatus) noexcept
{
	if (dataPresent)
		return (actedStatus & irq::kTransferComplete) != 0;
	return (actedStatus & irq::kCommandComplete) != 0;
}


// Zero and all-ones status words cannot justify completion.
constexpr bool
TerminalStatusIsReal(uint32_t actedStatus) noexcept
{
	return !IsSpuriousMeow(actedStatus);
}


} // namespace jr::sdhci::contract
