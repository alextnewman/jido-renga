// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with Qwen 3.6
// SPDX-FileContributor: Generated with GitHub Copilot

//! Driver for Atmel maXTouch touch controllers.
// Based on Object-Based Protocol (OBP) specification for WINKY Chromebook 2


#include "Driver.h"
#include "MxtDevice.h"

#include <ACPI.h>
#include <device_manager.h>
#include <i2c.h>

#include <lock.h>

#include <new>
#include <stdio.h>
#include <string.h>


#define I2C_ATMEL_MXT_DRIVER_NAME "drivers/input/i2c_atmel_mxt/driver_v1"
#define I2C_ATMEL_MXT_DEVICE_NAME "drivers/input/i2c_atmel_mxt/device_v1"

#define ACPI_NAME_ATMEL_MXT		"ATML0000"


static device_manager_info* sDeviceManager;
static acpi_module_info* sACPI;

DeviceList *gDeviceList = NULL;
static mutex sDriverLock;


struct mxt_open_cookie {
	MxtDevice*	fDevice;
};


// WINKY I2C slave address (ADDSEL pin pulled high)
#define MXT_I2C_SLAVE_ADDR	0x4B


// Device hooks (for published device node)
static status_t
i2c_atmel_mxt_init_device(void* driverCookie, void** cookie)
{
	*cookie = driverCookie;
	return B_OK;
}


static void
i2c_atmel_mxt_uninit_device(void* _cookie)
{
}


static status_t
i2c_atmel_mxt_open(void* initCookie, const char* path, int flags,
	void** _cookie)
{
	TRACE_ALWAYS("OPEN: path=%s, cookie=%p\n", path, initCookie);

	MxtDevice* device = (MxtDevice*)initCookie;
	if (!device || device->InitCheck() != B_OK)
		return B_ERROR;

	mxt_open_cookie* c = new(std::nothrow) mxt_open_cookie();
	if (c == NULL)
		return B_NO_MEMORY;

	c->fDevice = device;

	status_t result = device->Open(flags);
	if (result != B_OK) {
		delete c;
		return result;
	}

	*_cookie = c;
	return B_OK;
}


static status_t
i2c_atmel_mxt_read(void* _cookie, off_t position, void* buffer,
	size_t* numBytes)
{
	*numBytes = 0;
	return B_ERROR;
}


static status_t
i2c_atmel_mxt_write(void* _cookie, off_t position, const void* buffer,
	size_t* numBytes)
{
	*numBytes = 0;
	return B_ERROR;
}


static status_t
i2c_atmel_mxt_control(void* _cookie, uint32 op, void* buffer, size_t length)
{
	mxt_open_cookie* c = static_cast<mxt_open_cookie*>(_cookie);
	if (!c || !c->fDevice)
		return B_ERROR;

	TRACE("control(%p, %" B_PRIu32 ", %p, %" B_PRIuSIZE ")\n", _cookie, op,
		buffer, length);
	return c->fDevice->Control(op, buffer, length);
}


static status_t
i2c_atmel_mxt_close(void* _cookie)
{
	mxt_open_cookie* c = static_cast<mxt_open_cookie*>(_cookie);
	if (c && c->fDevice)
		c->fDevice->Close();
	return B_OK;
}


static status_t
i2c_atmel_mxt_free(void* _cookie)
{
	mxt_open_cookie* c = static_cast<mxt_open_cookie*>(_cookie);
	if (!c)
		return B_OK;

	MxtDevice* device = c->fDevice;
	TRACE("free(cookie=%p, device=%p)\n", c, device);

	mutex_lock(&sDriverLock);
	if (device && device->IsRemoved() && !device->IsOpen()) {
		delete device;
	}
	mutex_unlock(&sDriverLock);

	delete c;
	return B_OK;
}


// Driver hooks

static float
i2c_atmel_mxt_support(device_node* parent)
{
	// Make sure parent is really the I2C bus manager
	const char* bus = NULL;
	const status_t busStatus
		= sDeviceManager->get_attr_string(parent, B_DEVICE_BUS, &bus, false);
	if (busStatus != B_OK || bus == NULL) {
		return 0.0f;
	}

	if (strcmp(bus, "i2c"))
		return 0.0;

	// Check whether it's an Atmel maXTouch device
	uint64 handlePointer;
	if (sDeviceManager->get_attr_uint64(parent, ACPI_DEVICE_HANDLE_ITEM,
			&handlePointer, false) != B_OK) {
		TRACE("i2c_atmel_mxt_support found an i2c device without acpi handle\n");
		return B_ERROR;
	}

	const char* name = NULL;
	if (sDeviceManager->get_attr_string(parent, ACPI_DEVICE_HID_ITEM, &name,
			false) == B_OK && name != NULL
		&& strcmp(name, ACPI_NAME_ATMEL_MXT) == 0) {
		TRACE_ALWAYS("i2c_atmel_mxt_support: matched HID=%s\n", name);
		return 0.6;
	}

	if (sDeviceManager->get_attr_string(parent, ACPI_DEVICE_CID_ITEM, &name,
			false) == B_OK && name != NULL
		&& strcmp(name, ACPI_NAME_ATMEL_MXT) == 0) {
		TRACE_ALWAYS("i2c_atmel_mxt_support: matched CID=%s\n", name);
		return 0.6;
	}

	TRACE_ALWAYS("i2c_atmel_mxt_support: no match, HID=(%s), CID=(%s)\n",
		name ? name : "(null)",
		(sDeviceManager->get_attr_string(parent, ACPI_DEVICE_CID_ITEM, &name, false) == B_OK && name != NULL) ? name : "(null)");
	return 0.0;
}


static status_t
i2c_atmel_mxt_register_device(device_node* node)
{
	device_attr attrs[] = {
		{ B_DEVICE_PRETTY_NAME, B_STRING_TYPE,
			{ .string = "Atmel maXTouch Touchpad" }},
		{ NULL }
	};

	return sDeviceManager->register_node(node, I2C_ATMEL_MXT_DRIVER_NAME,
		attrs, NULL, NULL);
}


static status_t
i2c_atmel_mxt_init_driver(device_node* node, void** driverCookie)
{
	TRACE_ALWAYS("i2c_atmel_mxt_init_driver: node=%p\n", node);

	// Attributes are on the parent i2c/device/v1 node (set by I2CBus::RegisterDevice).
	// The node passed here is a child created by _RegisterDynamic(), so attributes
	// are inherited from parent — use searchParents=true.
	const char* hid = NULL;
	sDeviceManager->get_attr_string(node, ACPI_DEVICE_HID_ITEM, &hid, true);

	uint16 slaveAddr = 0;
	sDeviceManager->get_attr_uint16(node, I2C_DEVICE_SLAVE_ADDR_ITEM,
		&slaveAddr, true);

	TRACE_ALWAYS("i2c_atmel_mxt: matched node HID=%s, slave_addr=0x%02x\n",
		hid ? hid : "(null)", slaveAddr);

	// The i2c/device/v1 parent provides transport access.
	device_node* i2cParent = sDeviceManager->get_parent_node(node);
	i2c_device_interface* i2c;
	i2c_device i2cCookie;
	sDeviceManager->get_driver(i2cParent, (driver_module_info**)&i2c,
		(void**)&i2cCookie);
	sDeviceManager->put_node(i2cParent);

	// The I2C bus manager stores the ACPI handle on this node.
	uint64 acpiHandleRaw = 0;
	sDeviceManager->get_attr_uint64(node, ACPI_DEVICE_HANDLE_ITEM,
		&acpiHandleRaw, true);
	acpi_handle acpiHandle = (acpi_handle)acpiHandleRaw;

	MxtDevice* mxtDevice = new(std::nothrow) MxtDevice(node, i2c, i2cCookie,
		sACPI, acpiHandle);
	if (mxtDevice == NULL) {
		ERROR("failed to allocate MXT device\n");
		return B_NO_MEMORY;
	}

	status_t status = mxtDevice->Initialize();
	if (status != B_OK) {
		ERROR("failed to initialize MXT device: %s\n", strerror(status));
		delete mxtDevice;
		return B_IO_ERROR;
	}

	*driverCookie = mxtDevice;
	return B_OK;
}


static void
i2c_atmel_mxt_uninit_driver(void* driverCookie)
{
	MxtDevice* device = (MxtDevice*)driverCookie;
	delete device;
}


static status_t
i2c_atmel_mxt_register_child_devices(void* cookie)
{
	MxtDevice* device = (MxtDevice*)cookie;
	if (device == NULL)
		return B_OK;

	// Allocate the first free path; hot removal can leave numbering gaps.
	int32 index = 0;
	char pathBuffer[B_DEV_NAME_LENGTH];
	while (true) {
		sprintf(pathBuffer, "input/touchpad/" DEVICE_PATH_SUFFIX "/%" B_PRId32, index++);
		if (gDeviceList->FindDevice(pathBuffer) == NULL) {
			device->SetPublishPath(strdup(pathBuffer));
			break;
		}
	}

	gDeviceList->AddDevice(device->PublishPath(), device);

	TRACE_ALWAYS("Publishing device at: %s\n", pathBuffer);
	status_t publishStatus = sDeviceManager->publish_device(device->Parent(), pathBuffer,
		I2C_ATMEL_MXT_DEVICE_NAME);
	if (publishStatus != B_OK) {
		ERROR("failed to publish device node: %s\n", strerror(publishStatus));
	} else {
		TRACE_ALWAYS("Device node published successfully at: /dev/%s\n", pathBuffer);
	}

	return B_OK;
}


static status_t
std_ops(int32 op, ...)
{
	switch (op) {
		case B_MODULE_INIT:
			gDeviceList = new(std::nothrow) DeviceList();
			if (gDeviceList == NULL) {
				return B_NO_MEMORY;
			}
			mutex_init(&sDriverLock, "i2c atmel mxt driver lock");
			return B_OK;
		case B_MODULE_UNINIT:
			delete gDeviceList;
			gDeviceList = NULL;
			mutex_destroy(&sDriverLock);
			return B_OK;
		default:
			break;
	}

	return B_ERROR;
}


// Driver module info
driver_module_info i2c_atmel_mxt_driver_module = {
	{
		I2C_ATMEL_MXT_DRIVER_NAME,
		0,
		&std_ops
	},

	i2c_atmel_mxt_support,
	i2c_atmel_mxt_register_device,
	i2c_atmel_mxt_init_driver,
	i2c_atmel_mxt_uninit_driver,
	i2c_atmel_mxt_register_child_devices,
	NULL,	// rescan
	NULL,	// removed
	NULL,	// suspend
	NULL,	// resume
};


// Device module info
struct device_module_info i2c_atmel_mxt_device_module = {
	{
		I2C_ATMEL_MXT_DEVICE_NAME,
		0,
		NULL
	},

	i2c_atmel_mxt_init_device,
	i2c_atmel_mxt_uninit_device,
	NULL,

	i2c_atmel_mxt_open,
	i2c_atmel_mxt_close,
	i2c_atmel_mxt_free,
	i2c_atmel_mxt_read,
	i2c_atmel_mxt_write,
	NULL,
	i2c_atmel_mxt_control,

	NULL,
	NULL
};


module_dependency module_dependencies[] = {
	{ B_DEVICE_MANAGER_MODULE_NAME, (module_info**)&sDeviceManager },
	{ B_ACPI_MODULE_NAME, (module_info**)&sACPI },
	{}
};


module_info* modules[] = {
	(module_info*)&i2c_atmel_mxt_driver_module,
	(module_info*)&i2c_atmel_mxt_device_module,
	NULL
};
