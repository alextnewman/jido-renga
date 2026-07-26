// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot
#pragma once

#include <stdint.h>


namespace byt_xhci_filter {

constexpr uint16_t kIntelVendorId = 0x8086;
constexpr uint16_t kBayTrailXhciDeviceId = 0x0f35;
constexpr uint16_t kSerialBusClass = 0x0c;
constexpr uint16_t kUsbSubclass = 0x03;
constexpr uint16_t kXhciInterface = 0x30;

constexpr uint16_t kUsb2Route = 0xd0;
constexpr uint16_t kUsb2RouteMask = 0xd4;
constexpr uint16_t kUsb3Enable = 0xd8;
constexpr uint16_t kUsb3RouteMask = 0xdc;


constexpr bool
Matches(uint16_t vendorId, uint16_t deviceId, uint16_t type, uint16_t subType,
	uint16_t interface)
{
	return vendorId == kIntelVendorId
		&& deviceId == kBayTrailXhciDeviceId
		&& type == kSerialBusClass
		&& subType == kUsbSubclass
		&& interface == kXhciInterface;
}


constexpr uint16_t
ConfigReadSource(uint16_t offset, uint8_t size)
{
	if (size != sizeof(uint32_t))
		return offset;
	if (offset == kUsb2RouteMask)
		return kUsb2Route;
	if (offset == kUsb3RouteMask)
		return kUsb3Enable;
	return offset;
}

} // namespace byt_xhci_filter
