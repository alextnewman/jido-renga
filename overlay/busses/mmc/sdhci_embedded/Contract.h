// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot
#pragma once

#include <stdint.h>

#include "Convergence.h"

// The controller contract: the assumptions we make about how the silicon
// behaves, written as pure predicates so they have exactly ONE home and TWO
// enforcement venues:
//
//   * Off-target, the host mock (FakeController) obeys them when it reacts, and
//     the unit tests assert them -- so "what we believe" is executable.
//   * On-target, the engine's trace/debug path can check the same predicate and
//     shout (or panic) when reality violates it -- so a wrong assumption on real
//     hardware is loud and immediate, not a silent corruption.
//
// A meow is a hint; state is truth; and *this* is the written-down truth both
// the model and the metal are held to.

namespace jr::sdhci::contract {


// A completed command must be *justified* by the right terminal event. A non-
// data command completes on CommandComplete; a data command must not be called
// done until TransferComplete -- the controller raises command-complete first,
// while the data phase is still moving. (This is the invariant bug #4 violated:
// the engine used to stop at the first completion bit for data commands too.)
constexpr bool
CompletionJustified(bool dataPresent, uint32_t actedStatus) noexcept
{
	if (dataPresent)
		return (actedStatus & irq::kTransferComplete) != 0;
	return (actedStatus & irq::kCommandComplete) != 0;
}


// We never act on a spurious "meow": an all-zero (nothing latched) or all-one
// (floating line) status word is never a terminal verdict. If we ever complete
// on one, the poll classifier is wrong.
constexpr bool
TerminalStatusIsReal(uint32_t actedStatus) noexcept
{
	return !IsSpuriousMeow(actedStatus);
}


} // namespace jr::sdhci::contract
