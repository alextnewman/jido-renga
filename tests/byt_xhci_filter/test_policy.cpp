// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot

#include "framework/jr_test.h"

#include "Policy.h"


using namespace byt_xhci_filter;


JR_TEST(byt_xhci_filter, matches_only_bay_trail_xhci)
{
	JR_CHECK(Matches(kIntelVendorId, kBayTrailXhciDeviceId, kSerialBusClass,
		kUsbSubclass, kXhciInterface));
	JR_CHECK(!Matches(0x1234, kBayTrailXhciDeviceId, kSerialBusClass,
		kUsbSubclass, kXhciInterface));
	JR_CHECK(!Matches(kIntelVendorId, 0x1234, kSerialBusClass, kUsbSubclass,
		kXhciInterface));
	JR_CHECK(!Matches(kIntelVendorId, kBayTrailXhciDeviceId, 0x01,
		kUsbSubclass, kXhciInterface));
	JR_CHECK(!Matches(kIntelVendorId, kBayTrailXhciDeviceId, kSerialBusClass,
		0x01, kXhciInterface));
	JR_CHECK(!Matches(kIntelVendorId, kBayTrailXhciDeviceId, kSerialBusClass,
		kUsbSubclass, 0x20));
}


JR_TEST(byt_xhci_filter, substitutes_only_dword_route_mask_reads)
{
	JR_CHECK_EQ(ConfigReadSource(kUsb2RouteMask, sizeof(uint32_t)), kUsb2Route);
	JR_CHECK_EQ(ConfigReadSource(kUsb3RouteMask, sizeof(uint32_t)), kUsb3Enable);

	JR_CHECK_EQ(ConfigReadSource(kUsb2RouteMask, sizeof(uint16_t)),
		kUsb2RouteMask);
	JR_CHECK_EQ(ConfigReadSource(kUsb3RouteMask, sizeof(uint8_t)),
		kUsb3RouteMask);
	JR_CHECK_EQ(ConfigReadSource(kUsb2Route, sizeof(uint32_t)), kUsb2Route);
	JR_CHECK_EQ(ConfigReadSource(0x10, sizeof(uint32_t)), (uint16_t)0x10);
}
