// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot

#include <new>
#include <string.h>

#include <KernelExport.h>
#include <PCI.h>
#include <bus/PCI.h>
#include <common/Trace.h>
#include <device_manager.h>

#include "Policy.h"


namespace {

using namespace byt_xhci_filter;

constexpr char kModuleName[] = "busses/usb/byt_xhci_filter/driver_v1";
constexpr char kPciDeviceModuleName[] = "bus_managers/pci/driver_v1";
constexpr char kTraceLabel[] = "byt_xhci_filter";

device_manager_info* gDeviceManager;


struct FilterCookie {
	pci_device_module_info*	pci;
	pci_device*			device;
};


FilterCookie*
filter_cookie(pci_device* device)
{
	return reinterpret_cast<FilterCookie*>(device);
}


status_t
std_ops(int32 op, ...)
{
	switch (op) {
		case B_MODULE_INIT:
			JR_DIAG_INFO(kTraceLabel, "module discovered by kernel loader\n");
			return B_OK;
		case B_MODULE_UNINIT:
			JR_DIAG_INFO(kTraceLabel, "module released\n");
			return B_OK;
		default:
			return B_BAD_VALUE;
	}
}


float
supports_device(device_node* parent)
{
	const char* bus;
	uint16 vendorId;
	uint16 deviceId;
	uint16 type;
	uint16 subType;
	uint16 interface;
	if (gDeviceManager->get_attr_string(parent, B_DEVICE_BUS, &bus, false)
			!= B_OK
		|| gDeviceManager->get_attr_uint16(parent, B_DEVICE_VENDOR_ID, &vendorId,
			false) != B_OK
		|| gDeviceManager->get_attr_uint16(parent, B_DEVICE_ID, &deviceId, false)
			!= B_OK
		|| gDeviceManager->get_attr_uint16(parent, B_DEVICE_TYPE, &type, false)
			!= B_OK
		|| gDeviceManager->get_attr_uint16(parent, B_DEVICE_SUB_TYPE, &subType,
			false) != B_OK
		|| gDeviceManager->get_attr_uint16(parent, B_DEVICE_INTERFACE, &interface,
			false) != B_OK) {
		return 0.0f;
	}

	const bool matches = strcmp(bus, "pci") == 0
		&& Matches(vendorId, deviceId, type, subType, interface);
	if (matches) {
		JR_DIAG_INFO(kTraceLabel,
			"matched PCI %04x:%04x class %02x:%02x:%02x\n", vendorId,
			deviceId, type, subType, interface);
	}
	return matches ? 1.0f : 0.0f;
}


status_t
register_device(device_node* parent)
{
	// Vendor and device IDs are intentionally absent: supports_device() reads
	// them non-recursively, so this synthetic PCI child cannot match the filter.
	device_attr attrs[] = {
		{ B_DEVICE_PRETTY_NAME, B_STRING_TYPE,
			{ .string = "Bay Trail xHCI PCI filter" } },
		{ B_DEVICE_BUS, B_STRING_TYPE, { .string = "pci" } },
		{ B_DEVICE_TYPE, B_UINT16_TYPE, { .ui16 = kSerialBusClass } },
		{ B_DEVICE_SUB_TYPE, B_UINT16_TYPE, { .ui16 = kUsbSubclass } },
		{ B_DEVICE_INTERFACE, B_UINT16_TYPE, { .ui16 = kXhciInterface } },
		{ nullptr }
	};

	JR_DIAG_INFO(kTraceLabel, "registering delegated PCI child for stock xHCI\n");
	status_t status = gDeviceManager->register_node(parent, kModuleName, attrs,
		nullptr, nullptr);
	if (status == B_OK) {
		JR_DIAG_INFO(kTraceLabel,
			"delegated PCI node registration returned B_OK\n");
	} else {
		JR_DIAG_ERROR(kTraceLabel,
			"failed to register delegated PCI child: %s\n", strerror(status));
	}
	return status;
}


status_t
init_driver(device_node* node, void** cookie)
{
	device_node* parent = gDeviceManager->get_parent_node(node);
	if (parent == nullptr)
		return B_BAD_VALUE;

	driver_module_info* parentDriver = nullptr;
	void* parentCookie = nullptr;
	status_t status = gDeviceManager->get_driver(parent, &parentDriver,
		&parentCookie);
	gDeviceManager->put_node(parent);
	if (status != B_OK)
		return status;
	if (parentDriver == nullptr || parentDriver->info.name == nullptr
		|| parentCookie == nullptr) {
		JR_DIAG_ERROR(kTraceLabel, "parent PCI driver is unavailable\n");
		return B_BAD_TYPE;
	}
	if (strcmp(parentDriver->info.name, kPciDeviceModuleName) != 0) {
		JR_DIAG_ERROR(kTraceLabel, "unexpected parent driver %s\n",
			parentDriver->info.name);
		return B_BAD_TYPE;
	}

	FilterCookie* filter = new(std::nothrow) FilterCookie {
		reinterpret_cast<pci_device_module_info*>(parentDriver),
		reinterpret_cast<pci_device*>(parentCookie)
	};
	if (filter == nullptr)
		return B_NO_MEMORY;

	const uint32 usb2Route = filter->pci->read_pci_config(filter->device,
		kUsb2Route, sizeof(uint32));
	const uint32 usb2Mask = filter->pci->read_pci_config(filter->device,
		kUsb2RouteMask, sizeof(uint32));
	const uint32 usb3Enable = filter->pci->read_pci_config(filter->device,
		kUsb3Enable, sizeof(uint32));
	const uint32 usb3Mask = filter->pci->read_pci_config(filter->device,
		kUsb3RouteMask, sizeof(uint32));
	JR_DIAG_INFO(kTraceLabel, "bound: USB2=%#08" B_PRIx32
		" mask=%#08" B_PRIx32 " USB3=%#08" B_PRIx32 " mask=%#08" B_PRIx32
		"\n", usb2Route, usb2Mask, usb3Enable, usb3Mask);
	if (usb2Route == 0 || usb3Enable == 0) {
		JR_DIAG_WARN(kTraceLabel,
			"firmware left a live routing register clear\n");
	}

	*cookie = filter;
	return B_OK;
}


void
uninit_driver(void* cookie)
{
	delete static_cast<FilterCookie*>(cookie);
}


uint8
read_io_8(pci_device* device, addr_t address)
{
	FilterCookie* filter = filter_cookie(device);
	return filter->pci->read_io_8(filter->device, address);
}


void
write_io_8(pci_device* device, addr_t address, uint8 value)
{
	FilterCookie* filter = filter_cookie(device);
	filter->pci->write_io_8(filter->device, address, value);
}


uint16
read_io_16(pci_device* device, addr_t address)
{
	FilterCookie* filter = filter_cookie(device);
	return filter->pci->read_io_16(filter->device, address);
}


void
write_io_16(pci_device* device, addr_t address, uint16 value)
{
	FilterCookie* filter = filter_cookie(device);
	filter->pci->write_io_16(filter->device, address, value);
}


uint32
read_io_32(pci_device* device, addr_t address)
{
	FilterCookie* filter = filter_cookie(device);
	return filter->pci->read_io_32(filter->device, address);
}


void
write_io_32(pci_device* device, addr_t address, uint32 value)
{
	FilterCookie* filter = filter_cookie(device);
	filter->pci->write_io_32(filter->device, address, value);
}


phys_addr_t
ram_address(pci_device* device, phys_addr_t physicalAddress)
{
	FilterCookie* filter = filter_cookie(device);
	return filter->pci->ram_address(filter->device, physicalAddress);
}


uint32
read_pci_config(pci_device* device, uint16 offset, uint8 size)
{
	FilterCookie* filter = filter_cookie(device);
	const uint16 source = ConfigReadSource(offset, size);
	const uint32 value = filter->pci->read_pci_config(filter->device, source,
		size);
	if (source != offset) {
		JR_DIAG_INFO(kTraceLabel, "config read %#04" B_PRIx16
			" sourced from %#04" B_PRIx16 " = %#08" B_PRIx32 "\n",
			offset, source, value);
	}
	return value;
}


void
write_pci_config(pci_device* device, uint16 offset, uint8 size, uint32 value)
{
	FilterCookie* filter = filter_cookie(device);
	filter->pci->write_pci_config(filter->device, offset, size, value);
}


status_t
find_pci_capability(pci_device* device, uint8 capability, uint8* offset)
{
	FilterCookie* filter = filter_cookie(device);
	return filter->pci->find_pci_capability(filter->device, capability, offset);
}


void
get_pci_info(pci_device* device, pci_info* info)
{
	FilterCookie* filter = filter_cookie(device);
	filter->pci->get_pci_info(filter->device, info);
}


status_t
find_pci_extended_capability(pci_device* device, uint16 capability,
	uint16* offset)
{
	FilterCookie* filter = filter_cookie(device);
	return filter->pci->find_pci_extended_capability(filter->device, capability,
		offset);
}


uint8
get_powerstate(pci_device* device)
{
	FilterCookie* filter = filter_cookie(device);
	return filter->pci->get_powerstate(filter->device);
}


void
set_powerstate(pci_device* device, uint8 state)
{
	FilterCookie* filter = filter_cookie(device);
	filter->pci->set_powerstate(filter->device, state);
}


uint32
get_msi_count(pci_device* device)
{
	FilterCookie* filter = filter_cookie(device);
	return filter->pci->get_msi_count(filter->device);
}


status_t
configure_msi(pci_device* device, uint32 count, uint32* startVector)
{
	FilterCookie* filter = filter_cookie(device);
	return filter->pci->configure_msi(filter->device, count, startVector);
}


status_t
unconfigure_msi(pci_device* device)
{
	FilterCookie* filter = filter_cookie(device);
	return filter->pci->unconfigure_msi(filter->device);
}


status_t
enable_msi(pci_device* device)
{
	FilterCookie* filter = filter_cookie(device);
	return filter->pci->enable_msi(filter->device);
}


status_t
disable_msi(pci_device* device)
{
	FilterCookie* filter = filter_cookie(device);
	return filter->pci->disable_msi(filter->device);
}


uint32
get_msix_count(pci_device* device)
{
	FilterCookie* filter = filter_cookie(device);
	return filter->pci->get_msix_count(filter->device);
}


status_t
configure_msix(pci_device* device, uint32 count, uint32* startVector)
{
	FilterCookie* filter = filter_cookie(device);
	return filter->pci->configure_msix(filter->device, count, startVector);
}


status_t
enable_msix(pci_device* device)
{
	FilterCookie* filter = filter_cookie(device);
	return filter->pci->enable_msix(filter->device);
}


pci_device_module_info sFilterModule = {
	{
		{
			kModuleName,
			0,
			std_ops
		},
		supports_device,
		register_device,
		init_driver,
		uninit_driver,
		nullptr,
		nullptr,
		nullptr
	},
	read_io_8,
	write_io_8,
	read_io_16,
	write_io_16,
	read_io_32,
	write_io_32,
	ram_address,
	read_pci_config,
	write_pci_config,
	find_pci_capability,
	get_pci_info,
	find_pci_extended_capability,
	get_powerstate,
	set_powerstate,
	get_msi_count,
	configure_msi,
	unconfigure_msi,
	enable_msi,
	disable_msi,
	get_msix_count,
	configure_msix,
	enable_msix
};

} // namespace


module_dependency module_dependencies[] = {
	{ B_DEVICE_MANAGER_MODULE_NAME, (module_info**)&gDeviceManager },
	{}
};


module_info* modules[] = {
	(module_info*)&sFilterModule,
	nullptr
};
