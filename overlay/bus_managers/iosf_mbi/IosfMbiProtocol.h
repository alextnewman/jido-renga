// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot
#pragma once

#include <stdint.h>

// Pure IOSF-MBI message-bus wire protocol -- no kernel dependencies, so the
// encoding can be proven on the host. On BayTrail/CherryTrail the sideband
// message bus is driven through three PCI config registers of the SoC
// transaction router (host bridge, 0:0:0): MCR, MDR, MCRX. A transaction is a
// write to MCRX (high offset bits) + MCR (opcode/port/low-offset), then a
// read/write of MDR (the data). This mirrors Linux's mbi_form_mcr and the
// MBI_MASK_HI / MBI_MASK_LO split in arch/x86/platform/intel/iosf_mbi.c.

namespace jr::iosf {


// PCI config-space offsets of the message-bus registers in the host bridge.
constexpr uint8_t kMcrOffset  = 0xd0;	// Message Control Register
constexpr uint8_t kMdrOffset  = 0xd4;	// Message Data Register
constexpr uint8_t kMcrxOffset = 0xd8;	// Message Control Register eXtended

// The MCR low byte is a fixed all-byte-enables pattern (Linux MBI_ENABLE).
constexpr uint32_t kMbiByteEnable = 0xf0;


// Build the Message Control Register value:
//   [31:24] opcode  [23:16] port(unit)  [15:8] offset low byte  [7:0] enable.
// Only the low byte of the target offset lands here; the upper 24 bits travel
// separately in MCRX (see McrxFor).
constexpr uint32_t
FormMcr(uint8_t opcode, uint8_t port, uint32_t offset) noexcept
{
	return (static_cast<uint32_t>(opcode) << 24)
		| (static_cast<uint32_t>(port) << 16)
		| (static_cast<uint32_t>(offset & 0xffu) << 8)
		| kMbiByteEnable;
}


// The high 24 bits of the target offset go in MCRX. Zero means "no extended
// register write needed" -- callers skip the MCRX write in that case, matching
// Linux's `if (mcrx)` guard.
constexpr uint32_t
McrxFor(uint32_t offset) noexcept
{
	return offset & 0xffffff00u;
}


} // namespace jr::iosf
