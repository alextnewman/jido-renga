// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot

#include "BayTrailController.h"
#include "Trace.h"

#include <ACPI.h>
#include <common/Gpio.h>
#include <device_manager.h>

#include <new>
#include <string.h>


using gpio::baytrail::BayTrailController;


device_manager_info* gDeviceManager = nullptr;
acpi_module_info* gAcpi = nullptr;
gpio::module_info* gGpio = nullptr;


namespace {

constexpr const char* kControllerModule
	= "busses/gpio/byt_gpio/driver_v1";


bool
IsSupportedUid(const char* uid)
{
	return uid != nullptr
		&& (strcmp(uid, "1") == 0 || strcmp(uid, "2") == 0
			|| strcmp(uid, "3") == 0);
}


float
SupportsDevice(device_node* parent)
{
	const char* bus = nullptr;
	const char* hid = nullptr;
	const char* uid = nullptr;
	if (gDeviceManager->get_attr_string(parent, B_DEVICE_BUS, &bus, false)
			!= B_OK
		|| strcmp(bus, "acpi") != 0
		|| gDeviceManager->get_attr_string(parent, ACPI_DEVICE_HID_ITEM,
			&hid, false) != B_OK
		|| gDeviceManager->get_attr_string(parent, ACPI_DEVICE_UID_ITEM,
			&uid, false) != B_OK) {
		return 0.0f;
	}

	const bool supportedHid = strcmp(hid, "INT33FC") == 0
		|| strcmp(hid, "INT33B2") == 0;
	return supportedHid && IsSupportedUid(uid) ? 1.0f : 0.0f;
}


status_t
RegisterDevice(device_node* parent)
{
	const char* uid = nullptr;
	if (gDeviceManager->get_attr_string(parent, ACPI_DEVICE_UID_ITEM, &uid,
			false) != B_OK || !IsSupportedUid(uid)) {
		return B_BAD_VALUE;
	}

	const char* prettyName = strcmp(uid, "1") == 0
		? "Bay Trail SCORE GPIO"
		: strcmp(uid, "2") == 0
			? "Bay Trail NCORE GPIO" : "Bay Trail SUS GPIO";
	device_attr attrs[] = {
		{ B_DEVICE_PRETTY_NAME, B_STRING_TYPE, { .string = prettyName } },
		{ B_DEVICE_FLAGS, B_UINT32_TYPE, { .ui32 = B_KEEP_DRIVER_LOADED } },
		{ nullptr }
	};
	return gDeviceManager->register_node(parent, kControllerModule, attrs,
		nullptr, nullptr);
}


status_t
InitDriver(device_node* node, void** cookie)
{
	BayTrailController* controller = new(std::nothrow) BayTrailController(node);
	if (controller == nullptr)
		return B_NO_MEMORY;

	status_t status = controller->Start();
	if (status != B_OK) {
		ERROR("controller initialization failed: %s\n", strerror(status));
		delete controller;
		return status;
	}

	*cookie = controller;
	return B_OK;
}


void
UninitDriver(void* cookie)
{
	delete static_cast<BayTrailController*>(cookie);
}


driver_module_info sControllerModule = {
	{
		kControllerModule,
		0,
		nullptr
	},
	SupportsDevice,
	RegisterDevice,
	InitDriver,
	UninitDriver,
	nullptr,
	nullptr,
	nullptr
};

} // namespace


module_info* modules[] = {
	reinterpret_cast<module_info*>(&sControllerModule),
	nullptr
};


module_dependency module_dependencies[] = {
	{ B_DEVICE_MANAGER_MODULE_NAME,
		reinterpret_cast<module_info**>(&gDeviceManager) },
	{ B_ACPI_MODULE_NAME, reinterpret_cast<module_info**>(&gAcpi) },
	{ B_GPIO_MODULE_NAME, reinterpret_cast<module_info**>(&gGpio) },
	{}
};
