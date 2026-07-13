// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot
#pragma once

#include <stdint.h>

#include "Convergence.h"

// A tiny *reacting* model of an SDHCI controller's interrupt-status evolution
// for a single command. It does not virtualize a kernel -- it just codifies our
// timing assumptions so the real poll classifier can be driven against them:
//
//   * a command presents CommandComplete at `cmdCompleteAtPoll`;
//   * a data command presents TransferComplete later, at `xferCompleteAtPoll`;
//   * each word is a one-poll snapshot, as if the worker acknowledged it.
//
// The engine's software accumulator must retain the first snapshot while
// acknowledging hardware, then converge on the *justified* terminal event.

namespace jr::test {


struct FakeController {
	bool		dataPresent = false;
	uint32_t	cmdCompleteAtPoll = 2;
	uint32_t	xferCompleteAtPoll = 6;		// deliberately > cmdCompleteAtPoll
	uint32_t	errorAtPoll = 0xffffffffu;	// no error by default
	uint32_t	errorBits = 0;

	// The newly latched status a controller would present at poll `n`.
	uint32_t
	StatusAtPoll(uint32_t n) const
	{
		uint32_t s = 0;
		if (n == cmdCompleteAtPoll)
			s |= jr::sdhci::irq::kCommandComplete;
		if (dataPresent && n == xferCompleteAtPoll)
			s |= jr::sdhci::irq::kTransferComplete;
		if (n == errorAtPoll)
			s |= errorBits;
		return s;
	}
};


} // namespace jr::test
