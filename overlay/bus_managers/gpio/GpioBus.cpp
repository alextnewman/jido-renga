// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot

#include <common/Gpio.h>
#include <common/Trace.h>

#include <ACPI.h>
#include <new>
#include <string.h>
#include <util/AutoLock.h>

#include "acpi.h"


#define TRACE_GPIO
#define GPIO_TRACE_LABEL "gpio"
#ifdef TRACE_GPIO
#	define TRACE(x...) JR_DIAG_TRACE(GPIO_TRACE_LABEL, x)
#else
#	define TRACE(x...) JR_DIAG_DISABLED()
#endif
#define ERROR(x...) JR_DIAG_ERROR(GPIO_TRACE_LABEL, x)


namespace {

constexpr uint32 kMaximumControllers = 16;
constexpr size_t kMaximumAcpiPath = 128;


device_manager_info* gDeviceManager = nullptr;
acpi_module_info* gAcpi = nullptr;


struct ControllerRecord {
	bool				used = false;
	gpio::FirmwareNode	node = gpio::FirmwareNode::Acpi(0);
	gpio::Controller*	controller = nullptr;
};


struct PinCookie {
	gpio::Controller*		controller = nullptr;
	uint16				pin = 0;
	gpio::ConnectionInfo	connection;
};


struct AcpiConnectionSearch {
	uint32				wantedResource = 0;
	uint32				wantedPin = 0;
	uint32				resourceIndex = 0;
	bool				found = false;
	char				source[kMaximumAcpiPath] = {};
	gpio::ConnectionInfo	connection;
};


gpio::Bias
BiasFromAcpi(uint8 pinConfig)
{
	switch (pinConfig) {
		case ACPI_PIN_CONFIG_NOPULL:
			return gpio::Bias::None;
		case ACPI_PIN_CONFIG_PULLUP:
			return gpio::Bias::PullUp;
		case ACPI_PIN_CONFIG_PULLDOWN:
			return gpio::Bias::PullDown;
		default:
			return gpio::Bias::Firmware;
	}
}


gpio::Access
AccessFromAcpi(uint8 restriction)
{
	switch (restriction) {
		case ACPI_IO_RESTRICT_INPUT:
			return gpio::Access::Input;
		case ACPI_IO_RESTRICT_OUTPUT:
			return gpio::Access::Output;
		case ACPI_IO_RESTRICT_NONE:
			return gpio::Access::Bidirectional;
		default:
			return gpio::Access::Preserve;
	}
}


acpi_status
FindAcpiConnection(acpi_resource* resource, void* context)
{
	AcpiConnectionSearch* search
		= static_cast<AcpiConnectionSearch*>(context);
	if (resource->Type != ACPI_RESOURCE_TYPE_GPIO)
		return AE_OK;

	if (search->resourceIndex++ != search->wantedResource)
		return AE_OK;

	const ACPI_RESOURCE_GPIO& gpio = resource->Data.Gpio;
	if (search->wantedPin >= gpio.PinTableLength || gpio.PinTable == nullptr
		|| gpio.ResourceSource.StringPtr == nullptr) {
		return AE_BAD_DATA;
	}
	if (strlcpy(search->source, gpio.ResourceSource.StringPtr,
			sizeof(search->source)) >= sizeof(search->source)) {
		return AE_LIMIT;
	}

	search->connection.pin = gpio.PinTable[search->wantedPin];
	search->connection.firmwareBias = BiasFromAcpi(gpio.PinConfig);
	search->connection.access = AccessFromAcpi(gpio.IoRestriction);
	search->connection.interruptResource
		= gpio.ConnectionType == ACPI_RESOURCE_GPIO_TYPE_INT;
	search->connection.activeLow
		= search->connection.interruptResource
			&& gpio.Polarity == ACPI_ACTIVE_LOW;
	search->connection.wakeCapable
		= gpio.WakeCapable == ACPI_WAKE_CAPABLE;
	search->found = true;
	return AE_OK;
}


class GpioBus {
public:
	GpioBus() { mutex_init(&fLock, "gpio controllers"); }
	~GpioBus() { mutex_destroy(&fLock); }

	status_t RegisterController(gpio::FirmwareNode node,
		gpio::Controller* controller);
	void UnregisterController(gpio::Controller* controller);
	status_t AcquireAcpi(device_node* consumer, uint32 resourceIndex,
		uint32 pinIndex, gpio::pin_handle* handle,
		gpio::ConnectionInfo* info);

private:
	status_t _ConsumerAcpiHandle(device_node* consumer,
		acpi_handle& handle) const;
	gpio::Controller* _ControllerFor(gpio::FirmwareNode node) const;

	mutable mutex		fLock;
	ControllerRecord	fControllers[kMaximumControllers];
};


status_t
GpioBus::RegisterController(gpio::FirmwareNode node,
	gpio::Controller* controller)
{
	if (controller == nullptr || node.value == 0)
		return B_BAD_VALUE;

	MutexLocker locker(fLock);
	for (ControllerRecord& record : fControllers) {
		if (record.used && record.node == node)
			return B_NAME_IN_USE;
	}
	for (ControllerRecord& record : fControllers) {
		if (!record.used) {
			record.used = true;
			record.node = node;
			record.controller = controller;
			TRACE("registered controller %p for firmware node %#" B_PRIxADDR
				" (%" B_PRIu32 " pins)\n", controller,
				static_cast<addr_t>(node.value), controller->PinCount());
			return B_OK;
		}
	}
	return B_NO_MEMORY;
}


void
GpioBus::UnregisterController(gpio::Controller* controller)
{
	if (controller == nullptr)
		return;

	MutexLocker locker(fLock);
	for (ControllerRecord& record : fControllers) {
		if (record.used && record.controller == controller) {
			record = {};
			return;
		}
	}
}


status_t
GpioBus::_ConsumerAcpiHandle(device_node* consumer,
	acpi_handle& handle) const
{
	uint64 rawHandle = 0;
	if (gDeviceManager->get_attr_uint64(consumer, ACPI_DEVICE_HANDLE_ITEM,
			&rawHandle, false) == B_OK) {
		handle = reinterpret_cast<acpi_handle>(
			static_cast<uintptr_t>(rawHandle));
		return handle != nullptr ? B_OK : B_BAD_VALUE;
	}

	const char* path = nullptr;
	if (gDeviceManager->get_attr_string(consumer, ACPI_DEVICE_PATH_ITEM,
			&path, false) != B_OK || path == nullptr) {
		return B_NAME_NOT_FOUND;
	}
	return gAcpi->get_handle(nullptr, path, &handle);
}


gpio::Controller*
GpioBus::_ControllerFor(gpio::FirmwareNode node) const
{
	for (const ControllerRecord& record : fControllers) {
		if (record.used && record.node == node)
			return record.controller;
	}
	return nullptr;
}


status_t
GpioBus::AcquireAcpi(device_node* consumer, uint32 resourceIndex,
	uint32 pinIndex, gpio::pin_handle* handle, gpio::ConnectionInfo* info)
{
	if (consumer == nullptr || handle == nullptr || info == nullptr)
		return B_BAD_VALUE;

	acpi_handle consumerHandle = nullptr;
	status_t status = _ConsumerAcpiHandle(consumer, consumerHandle);
	if (status != B_OK)
		return status;

	AcpiConnectionSearch search;
	search.wantedResource = resourceIndex;
	search.wantedPin = pinIndex;
	status = gAcpi->walk_resources(consumerHandle, const_cast<char*>("_CRS"),
		FindAcpiConnection, &search);
	if (status != B_OK)
		return status;
	if (!search.found)
		return B_NAME_NOT_FOUND;

	acpi_handle controllerHandle = nullptr;
	status = gAcpi->get_handle(nullptr, search.source, &controllerHandle);
	if (status != B_OK || controllerHandle == nullptr)
		return status != B_OK ? status : B_NAME_NOT_FOUND;

	MutexLocker locker(fLock);
	gpio::Controller* controller = _ControllerFor(gpio::FirmwareNode::Acpi(
		reinterpret_cast<uintptr_t>(controllerHandle)));
	if (controller == nullptr) {
		ERROR("ACPI GPIO controller %s is not registered\n", search.source);
		return B_DEVICE_NOT_FOUND;
	}
	if (search.connection.pin >= controller->PinCount())
		return B_BAD_VALUE;

	PinCookie* cookie = new(std::nothrow) PinCookie;
	if (cookie == nullptr)
		return B_NO_MEMORY;

	status = controller->Claim(search.connection.pin);
	if (status != B_OK) {
		delete cookie;
		return status;
	}

	cookie->controller = controller;
	cookie->pin = search.connection.pin;
	cookie->connection = search.connection;
	*info = search.connection;
	*handle = cookie;
	TRACE("acquired ACPI GPIO resource %" B_PRIu32 " pin %" B_PRIu16
		" from %s\n", resourceIndex, cookie->pin, search.source);
	return B_OK;
}


GpioBus* gBus = nullptr;


status_t
RegisterController(gpio::FirmwareNode node, gpio::Controller* controller)
{
	return gBus != nullptr
		? gBus->RegisterController(node, controller) : B_NO_INIT;
}


void
UnregisterController(gpio::Controller* controller)
{
	if (gBus != nullptr)
		gBus->UnregisterController(controller);
}


status_t
AcquireAcpi(device_node* consumer, uint32 resourceIndex, uint32 pinIndex,
	gpio::pin_handle* handle, gpio::ConnectionInfo* info)
{
	return gBus != nullptr
		? gBus->AcquireAcpi(consumer, resourceIndex, pinIndex, handle, info)
		: B_NO_INIT;
}


void
Release(gpio::pin_handle handle)
{
	PinCookie* cookie = static_cast<PinCookie*>(handle);
	if (cookie == nullptr)
		return;
	cookie->controller->StopWatching(cookie->pin);
	cookie->controller->Release(cookie->pin);
	delete cookie;
}


status_t
ConfigureInput(gpio::pin_handle handle, const gpio::InputConfig* config)
{
	PinCookie* cookie = static_cast<PinCookie*>(handle);
	if (cookie == nullptr || config == nullptr)
		return B_BAD_VALUE;
	if (cookie->connection.access == gpio::Access::Output)
		return B_NOT_ALLOWED;

	gpio::InputConfig resolved = *config;
	if (resolved.bias == gpio::Bias::Firmware)
		resolved.bias = cookie->connection.firmwareBias;
	return cookie->controller->ConfigureInput(cookie->pin, resolved);
}


status_t
ConfigureOutput(gpio::pin_handle handle, const gpio::OutputConfig* config)
{
	PinCookie* cookie = static_cast<PinCookie*>(handle);
	if (cookie == nullptr || config == nullptr)
		return B_BAD_VALUE;
	if (cookie->connection.interruptResource
		|| cookie->connection.access == gpio::Access::Input) {
		return B_NOT_ALLOWED;
	}
	return cookie->controller->ConfigureOutput(cookie->pin, *config);
}


status_t
Read(gpio::pin_handle handle, gpio::Level* level)
{
	PinCookie* cookie = static_cast<PinCookie*>(handle);
	if (cookie == nullptr || level == nullptr)
		return B_BAD_VALUE;
	return cookie->controller->Read(cookie->pin, *level);
}


status_t
Write(gpio::pin_handle handle, gpio::Level level)
{
	PinCookie* cookie = static_cast<PinCookie*>(handle);
	if (cookie == nullptr)
		return B_BAD_VALUE;
	if (cookie->connection.interruptResource
		|| cookie->connection.access == gpio::Access::Input) {
		return B_NOT_ALLOWED;
	}
	return cookie->controller->Write(cookie->pin, level);
}


status_t
Watch(gpio::pin_handle handle, const gpio::InterruptConfig* config,
	gpio::EventHandler handler, void* context)
{
	PinCookie* cookie = static_cast<PinCookie*>(handle);
	if (cookie == nullptr || config == nullptr || handler == nullptr)
		return B_BAD_VALUE;
	if (cookie->connection.access == gpio::Access::Output)
		return B_NOT_ALLOWED;

	gpio::InputConfig input;
	input.bias = cookie->connection.firmwareBias;
	status_t status = cookie->controller->ConfigureInput(cookie->pin, input);
	if (status != B_OK)
		return status;
	return cookie->controller->Watch(cookie->pin, *config, handler, context);
}


void
StopWatching(gpio::pin_handle handle)
{
	PinCookie* cookie = static_cast<PinCookie*>(handle);
	if (cookie != nullptr)
		cookie->controller->StopWatching(cookie->pin);
}


status_t
GpioStdOps(int32 op, ...)
{
	switch (op) {
		case B_MODULE_INIT:
			gBus = new(std::nothrow) GpioBus;
			return gBus != nullptr ? B_OK : B_NO_MEMORY;

		case B_MODULE_UNINIT:
			delete gBus;
			gBus = nullptr;
			return B_OK;

		default:
			return B_BAD_VALUE;
	}
}


gpio::module_info sGpioModule = {
	{
		B_GPIO_MODULE_NAME,
		0,
		GpioStdOps
	},
	RegisterController,
	UnregisterController,
	AcquireAcpi,
	Release,
	ConfigureInput,
	ConfigureOutput,
	Read,
	Write,
	Watch,
	StopWatching
};

} // namespace


module_info* modules[] = {
	reinterpret_cast<module_info*>(&sGpioModule),
	nullptr
};


module_dependency module_dependencies[] = {
	{ B_DEVICE_MANAGER_MODULE_NAME,
		reinterpret_cast<module_info**>(&gDeviceManager) },
	{ B_ACPI_MODULE_NAME, reinterpret_cast<module_info**>(&gAcpi) },
	{}
};
