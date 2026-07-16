// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot

#include "Card.h"
#include "Debug.h"
#include "ModuleNames.h"

#include <ACPI.h>
#include <PCI.h>
#include <device_manager.h>
#include <i2c.h>

#include <new>
#include <stdio.h>
#include <string.h>


device_manager_info* gDeviceManager = nullptr;
pci_module_info* gPci = nullptr;
gpio::module_info* gGpio = nullptr;


namespace jr::byt_audio {

namespace {

constexpr const char* kLpeHid = "80860F28";
constexpr const char* kCodecHid = "193C9890";
constexpr const char* kPublishPath = "audio/hmulti/byt_max98090/0";

mutex sModuleLock = MUTEX_INITIALIZER("BYT MAX98090 module");
int32 sModuleReferences = 0;

struct LpeCookie {
	device_node* node;
};

struct CodecCookie {
	i2c_device cookie;
	device_node* node;
};


const char*
MatchingAcpiId(device_node* node, const char* expected)
{
	const char* id = nullptr;
	if (gDeviceManager->get_attr_string(node, ACPI_DEVICE_HID_ITEM, &id,
			false) == B_OK && id != nullptr && strcmp(id, expected) == 0) {
		return id;
	}
	if (gDeviceManager->get_attr_string(node, ACPI_DEVICE_CID_ITEM, &id,
			false) == B_OK && id != nullptr && strcmp(id, expected) == 0) {
		return id;
	}
	return nullptr;
}


float
LpeSupport(device_node* parent)
{
	const char* bus = nullptr;
	if (gDeviceManager->get_attr_string(parent, B_DEVICE_BUS, &bus, false)
			!= B_OK || bus == nullptr || strcmp(bus, "acpi") != 0) {
		return 0.0f;
	}

	uint32 type = 0;
	if (gDeviceManager->get_attr_uint32(parent, ACPI_DEVICE_TYPE_ITEM, &type,
			false) != B_OK || type != ACPI_TYPE_DEVICE) {
		return 0.0f;
	}

	const char* id = MatchingAcpiId(parent, kLpeHid);
	if (id == nullptr)
		return 0.0f;

	const char* path = nullptr;
	gDeviceManager->get_attr_string(parent, ACPI_DEVICE_PATH_ITEM, &path, false);
	TRACE("matched LPE ACPI node: id=%s path=%s\n", id,
		path != nullptr ? path : "(unavailable)");
	return 0.9f;
}


status_t
LpeRegister(device_node* parent)
{
	device_attr attributes[] = {
		{B_DEVICE_PRETTY_NAME, B_STRING_TYPE,
			{.string = "Intel Bay Trail SST audio"}},
		{nullptr}
	};
	const status_t status = gDeviceManager->register_node(parent,
		kLpeDriverModule, attributes,
		nullptr, nullptr);
	if (status != B_OK) {
		ERROR("failed to register LPE driver node: %s\n",
			strerror(status));
	}
	return status;
}


status_t
LpeInit(device_node* node, void** cookie)
{
	TRACE("initializing LPE driver node\n");
	LpeCookie* context = new(std::nothrow) LpeCookie;
	if (context == nullptr)
		return B_NO_MEMORY;
	context->node = node;

	device_node* parent = gDeviceManager->get_parent_node(node);
	if (parent == nullptr) {
		delete context;
		return B_BAD_VALUE;
	}
	const status_t status = gCard->AttachLpe(parent);
	gDeviceManager->put_node(parent);
	if (status != B_OK) {
		ERROR("LPE attachment failed: %s\n", strerror(status));
		delete context;
		return status;
	}
	*cookie = context;
	return B_OK;
}


void
LpeUninit(void* cookie)
{
	gCard->DetachLpe();
	delete static_cast<LpeCookie*>(cookie);
}


status_t
LpeRegisterChildren(void* cookie)
{
	LpeCookie* context = static_cast<LpeCookie*>(cookie);
	status_t status = gDeviceManager->publish_device(context->node, kPublishPath,
		kDeviceModule);
	if (status == B_OK) {
		TRACE("published /dev/%s; open waits for firmware and codec\n",
			kPublishPath);
	} else {
		ERROR("failed to publish /dev/%s: %s\n", kPublishPath,
			strerror(status));
	}
	return status;
}


float
CodecSupport(device_node* parent)
{
	const char* bus = nullptr;
	if (gDeviceManager->get_attr_string(parent, B_DEVICE_BUS, &bus, false)
			!= B_OK || bus == nullptr || strcmp(bus, "i2c") != 0) {
		return 0.0f;
	}

	const char* id = MatchingAcpiId(parent, kCodecHid);
	if (id == nullptr)
		return 0.0f;

	uint16 address = 0;
	const status_t addressStatus = gDeviceManager->get_attr_uint16(parent,
		I2C_DEVICE_SLAVE_ADDR_ITEM, &address, false);
	if (addressStatus == B_OK) {
		TRACE("matched MAX98090 I2C node: id=%s address=0x%02x\n", id,
			address);
	} else {
		TRACE("matched MAX98090 I2C node: id=%s address unavailable: %s\n",
			id, strerror(addressStatus));
	}
	return 0.9f;
}


status_t
CodecRegister(device_node* parent)
{
	device_attr attributes[] = {
		{B_DEVICE_PRETTY_NAME, B_STRING_TYPE,
			{.string = "MAX98090 audio codec"}},
		{nullptr}
	};
	const status_t status = gDeviceManager->register_node(parent,
		kCodecDriverModule, attributes,
		nullptr, nullptr);
	if (status != B_OK) {
		ERROR("failed to register codec driver node: %s\n",
			strerror(status));
	}
	return status;
}


status_t
CodecInit(device_node* node, void** cookie)
{
	TRACE("initializing MAX98090 codec driver node\n");
	device_node* parent = gDeviceManager->get_parent_node(node);
	if (parent == nullptr)
		return B_BAD_VALUE;

	i2c_device_interface* interface = nullptr;
	i2c_device i2cCookie = nullptr;
	status_t status = gDeviceManager->get_driver(parent,
		reinterpret_cast<driver_module_info**>(&interface),
		reinterpret_cast<void**>(&i2cCookie));
	if (status != B_OK) {
		ERROR("failed to acquire I2C transport: %s\n", strerror(status));
		gDeviceManager->put_node(parent);
		return status;
	}

	CodecCookie* context = new(std::nothrow) CodecCookie;
	if (context == nullptr) {
		gDeviceManager->put_node(parent);
		return B_NO_MEMORY;
	}
	context->cookie = i2cCookie;
	context->node = parent;
	status = gCard->AttachCodec(parent, interface, i2cCookie);
	if (status != B_OK) {
		ERROR("codec attachment failed: %s\n", strerror(status));
		gDeviceManager->put_node(parent);
		delete context;
		return status;
	}
	*cookie = context;
	return B_OK;
}


void
CodecUninit(void* cookie)
{
	CodecCookie* context = static_cast<CodecCookie*>(cookie);
	gCard->DetachCodec(context->cookie);
	gDeviceManager->put_node(context->node);
	delete context;
}


status_t
DeviceInit(void*, void** cookie)
{
	*cookie = gCard;
	return B_OK;
}


void
DeviceUninit(void*)
{
}


status_t
DeviceOpen(void* cookie, const char*, int, void** openCookie)
{
	Card* card = static_cast<Card*>(cookie);
	status_t status = card->Open();
	if (status == B_OK)
		*openCookie = card;
	return status;
}


status_t
DeviceClose(void* cookie)
{
	return static_cast<Card*>(cookie)->Close();
}


status_t
DeviceFree(void*)
{
	return B_OK;
}


status_t
DeviceRead(void*, off_t, void*, size_t* length)
{
	*length = 0;
	return B_NOT_ALLOWED;
}


status_t
DeviceWrite(void*, off_t, const void*, size_t* length)
{
	*length = 0;
	return B_NOT_ALLOWED;
}


status_t
DeviceControl(void* cookie, uint32 op, void* buffer, size_t length)
{
	return static_cast<Card*>(cookie)->Control(op, buffer, length);
}


status_t
StdOps(int32 op, ...)
{
	mutex_lock(&sModuleLock);
	status_t status = B_OK;
	switch (op) {
		case B_MODULE_INIT:
			if (sModuleReferences == 0) {
				gCard = new(std::nothrow) Card;
				if (gCard == nullptr) {
					status = B_NO_MEMORY;
					break;
				}
				TRACE("module loaded; probing ACPI HID %s and I2C HID %s\n",
					kLpeHid, kCodecHid);
			}
			sModuleReferences++;
			break;
		case B_MODULE_UNINIT:
			if (--sModuleReferences == 0) {
				delete gCard;
				gCard = nullptr;
			}
			break;
		default:
			status = B_ERROR;
			break;
	}
	mutex_unlock(&sModuleLock);
	return status;
}

} // namespace

} // namespace jr::byt_audio


using namespace jr::byt_audio;

static driver_module_info sLpeDriver = {
	{
		kLpeDriverModule,
		0,
		StdOps
	},
	LpeSupport,
	LpeRegister,
	LpeInit,
	LpeUninit,
	LpeRegisterChildren,
	nullptr,
	nullptr,
	nullptr,
	nullptr
};

static driver_module_info sCodecDriver = {
	{
		kCodecDriverModule,
		0,
		StdOps
	},
	CodecSupport,
	CodecRegister,
	CodecInit,
	CodecUninit,
	nullptr,
	nullptr,
	nullptr,
	nullptr,
	nullptr
};

static device_module_info sDevice = {
	{
		kDeviceModule,
		0,
		nullptr
	},
	DeviceInit,
	DeviceUninit,
	nullptr,
	DeviceOpen,
	DeviceClose,
	DeviceFree,
	DeviceRead,
	DeviceWrite,
	nullptr,
	DeviceControl,
	nullptr,
	nullptr
};

module_dependency module_dependencies[] = {
	{B_DEVICE_MANAGER_MODULE_NAME, reinterpret_cast<module_info**>(
		&gDeviceManager)},
	{B_PCI_MODULE_NAME, reinterpret_cast<module_info**>(&gPci)},
	{B_GPIO_MODULE_NAME, reinterpret_cast<module_info**>(&gGpio)},
	{}
};

module_info* modules[] = {
	reinterpret_cast<module_info*>(&sLpeDriver),
	reinterpret_cast<module_info*>(&sCodecDriver),
	reinterpret_cast<module_info*>(&sDevice),
	nullptr
};
