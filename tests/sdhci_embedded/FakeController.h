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
//   * a command latches CommandComplete after `cmdCompleteAtPoll` polls;
//   * a data command latches TransferComplete later, at `xferCompleteAtPoll`;
//   * bits accumulate (write-1-to-clear is the engine's job, not modeled here).
//
// The engine's poll loop is expected to converge on the *justified* terminal
// event, not the first bit it happens to see. Point `xferCompleteAtPoll` after
// `cmdCompleteAtPoll` and you have reproduced exactly the trap bug #4 fell into.

namespace jr::test {


struct FakeController {
	bool		dataPresent = false;
	uint32_t	cmdCompleteAtPoll = 2;
	uint32_t	xferCompleteAtPoll = 6;		// deliberately > cmdCompleteAtPoll
	uint32_t	errorAtPoll = 0xffffffffu;	// no error by default
	uint32_t	errorBits = 0;

	// The status a real (assumed) controller would present at poll `n`.
	uint32_t
	StatusAtPoll(uint32_t n) const
	{
		uint32_t s = 0;
		if (n >= cmdCompleteAtPoll)
			s |= jr::sdhci::irq::kCommandComplete;
		if (dataPresent && n >= xferCompleteAtPoll)
			s |= jr::sdhci::irq::kTransferComplete;
		if (n >= errorAtPoll)
			s |= errorBits;
		return s;
	}
};


} // namespace jr::test
