// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot
#pragma once

#include <stdint.h>

// Kernel-independent IOSF-MBI wire encoding. The SoC transaction router at
// 0:0.0 exposes MCR, MDR, and MCRX in PCI configuration space.

namespace jr::iosf {


constexpr uint16_t kIntelVendorId = 0x8086;
constexpr uint16_t kBayTrailHostBridgeId = 0x0f00;
constexpr uint16_t kCherryTrailHostBridgeId = 0x2280;


constexpr bool
IsSupportedHostBridge(uint16_t vendor, uint16_t device) noexcept
{
	return vendor == kIntelVendorId
		&& (device == kBayTrailHostBridgeId
			|| device == kCherryTrailHostBridgeId);
}


// PCI config-space offsets of the message-bus registers in the host bridge.
constexpr uint8_t kMcrOffset  = 0xd0;	// Message Control Register
constexpr uint8_t kMdrOffset  = 0xd4;	// Message Data Register
constexpr uint8_t kMcrxOffset = 0xd8;	// Message Control Register eXtended

// The MCR low byte enables all four data bytes.
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


// MCRX carries the high 24 bits of the target offset. Callers must write it for
// every transaction, including zero, to clear a previous extended address.
constexpr uint32_t
McrxFor(uint32_t offset) noexcept
{
	return offset & 0xffffff00u;
}


} // namespace jr::iosf
