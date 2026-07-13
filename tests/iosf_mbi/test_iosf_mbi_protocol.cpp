// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot

#include "framework/jr_test.h"

#include "IosfMbiProtocol.h"

// The IOSF-MBI wire encoding is pure, so we prove it off-target against values
// cross-checked with Linux (arch/x86/platform/intel/iosf_mbi.c: mbi_form_mcr,
// MBI_MASK_HI / MBI_MASK_LO). The opcodes below are IOSF_MBI_CR_READ (0x06) and
// IOSF_MBI_CR_WRITE (0x07); we spell them literally to keep this TU free of the
// kernel module header.

using namespace jr::iosf;


JR_TEST(iosf_mbi, accepts_only_known_transaction_routers)
{
	JR_CHECK(IsSupportedHostBridge(kIntelVendorId, kBayTrailHostBridgeId));
	JR_CHECK(IsSupportedHostBridge(kIntelVendorId, kCherryTrailHostBridgeId));
	JR_CHECK(!IsSupportedHostBridge(0x1234, kBayTrailHostBridgeId));
	JR_CHECK(!IsSupportedHostBridge(kIntelVendorId, 0x0f14));
	JR_CHECK(!IsSupportedHostBridge(kIntelVendorId, 0x0f16));
}


JR_TEST(iosf_mbi, form_mcr_packs_opcode_port_offset_enable)
{
	// opcode=0x06 (CR read), port=0x63 (SCCEP), offset=0x1078 -> low byte 0x78.
	// [31:24]=06 [23:16]=63 [15:8]=78 [7:0]=f0.
	JR_CHECK_EQ(FormMcr(0x06, 0x63, 0x1078), 0x066378f0u);

	// Same target, write opcode -> only the top byte changes.
	JR_CHECK_EQ(FormMcr(0x07, 0x63, 0x1078), 0x076378f0u);
}


JR_TEST(iosf_mbi, form_mcr_uses_only_the_low_offset_byte)
{
	// The high offset bits must NOT bleed into MCR; only 0xff of the offset is
	// used for the [15:8] field. 0x1078 and 0x0078 form the same MCR.
	JR_CHECK_EQ(FormMcr(0x06, 0x63, 0x1078), FormMcr(0x06, 0x63, 0x0078));

	// Enable nibble is always present in the low byte.
	JR_CHECK_EQ(FormMcr(0x00, 0x00, 0x00) & 0xffu, kMbiByteEnable);
}


JR_TEST(iosf_mbi, mcrx_carries_the_high_offset_bits)
{
	// 0x1078 -> high part 0x1000 (non-zero: an MCRX write is required).
	JR_CHECK_EQ(McrxFor(0x1078), 0x1000u);

	// A register within the first 256 bytes needs no extended write.
	JR_CHECK_EQ(McrxFor(0x00fc), 0x0u);
	JR_CHECK_EQ(McrxFor(0x00ff), 0x0u);

	// The boundary: 0x100 is the first offset that requires MCRX.
	JR_CHECK_EQ(McrxFor(0x0100), 0x100u);
}


JR_TEST(iosf_mbi, register_offsets_match_the_host_bridge_layout)
{
	// PCI config offsets of the message-bus registers (host bridge 0:0:0).
	JR_CHECK_EQ(kMcrOffset, 0xd0);
	JR_CHECK_EQ(kMdrOffset, 0xd4);
	JR_CHECK_EQ(kMcrxOffset, 0xd8);
}
