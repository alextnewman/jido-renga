/*
 * SPDX-FileCopyrightText: 2020 Jérôme Duval, jerome.duval@gmail.com
 * SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
 * SPDX-License-Identifier: MIT
 * SPDX-FileContributor: Generated with GitHub Copilot
 */

#include "I2CPrivate.h"


I2CBus::I2CBus(device_node* node, uint8 id)
	:
	fNode(node),
	fID(id),
	fController(NULL),
	fCookie(NULL)
{
	CALLED();
	device_node* parent = gDeviceManager->get_parent_node(node);
	status_t status = gDeviceManager->get_driver(parent,
		(driver_module_info**)&fController, &fCookie);
	gDeviceManager->put_node(parent);

	if (status != B_OK)
		return;

	fController->set_i2c_bus(fCookie, this);
}


I2CBus::~I2CBus()
{
}


status_t
I2CBus::InitCheck()
{
	return B_OK;
}


status_t
I2CBus::ExecCommand(i2c_op op, i2c_addr slaveAddress, const void* cmdBuffer,
	size_t cmdLength, void* dataBuffer, size_t dataLength)
{
	CALLED();
	return fController->exec_command(fCookie, op, slaveAddress, cmdBuffer,
		cmdLength, dataBuffer, dataLength);
}


status_t
I2CBus::RegisterDevice(i2c_addr slaveAddress, char* hid, char** cid,
	acpi_handle acpiHandle)
{
	CALLED();

	device_attr attrs[9];
	uint32 count = 0;

	attrs[count++] = { B_DEVICE_PRETTY_NAME, B_STRING_TYPE,
		{ .string = "I2C device" } };
	attrs[count++] = { I2C_DEVICE_SLAVE_ADDR_ITEM, B_UINT16_TYPE,
		{ .ui16 = slaveAddress } };
	attrs[count++] = { B_DEVICE_BUS, B_STRING_TYPE, { .string = "i2c" } };
	attrs[count++] = { B_DEVICE_FLAGS, B_UINT32_TYPE,
		{ .ui32 = B_FIND_MULTIPLE_CHILDREN } };

	if (hid != NULL) {
		attrs[count++] = { ACPI_DEVICE_HID_ITEM, B_STRING_TYPE,
			{ .string = hid } };
	}
	if (cid != NULL && cid[0] != NULL) {
		attrs[count++] = { ACPI_DEVICE_CID_ITEM, B_STRING_TYPE,
			{ .string = cid[0] } };
	}

	attrs[count++] = { ACPI_DEVICE_HANDLE_ITEM, B_UINT64_TYPE,
		{ .ui64 = (addr_t)acpiHandle } };
	attrs[count] = { NULL };

	return gDeviceManager->register_node(fNode, I2C_DEVICE_MODULE_NAME, attrs,
		NULL, NULL);
}


status_t
I2CBus::Scan()
{
	CALLED();
	if (fController->scan_bus != NULL)
		fController->scan_bus(fCookie);
	return B_OK;
}


status_t
I2CBus::AcquireBus()
{
	CALLED();
	if (fController->acquire_bus != NULL)
		return fController->acquire_bus(fCookie);
	return B_OK;
}


void
I2CBus::ReleaseBus()
{
	CALLED();
	if (fController->release_bus != NULL)
		fController->release_bus(fCookie);
}


static status_t
i2c_init_bus(device_node* node, void** _bus)
{
	CALLED();
	uint8 pathID;
	if (gDeviceManager->get_attr_uint8(node, I2C_BUS_PATH_ID_ITEM, &pathID,
			false) != B_OK) {
		return B_ERROR;
	}

	I2CBus* bus = new(std::nothrow) I2CBus(node, pathID);
	if (bus == NULL)
		return B_NO_MEMORY;

	status_t result = bus->InitCheck();
	if (result != B_OK) {
		ERROR("failed to set up i2c bus object\n");
		delete bus;
		return result;
	}

	*_bus = bus;

	char name[B_DEV_NAME_LENGTH];
	snprintf(name, sizeof(name), "bus/i2c/%d/bus_raw", pathID);

	return gDeviceManager->publish_device(node, name, I2C_BUS_RAW_MODULE_NAME);
}


static void
i2c_uninit_bus(void* _bus)
{
	CALLED();
	I2CBus* bus = (I2CBus*)_bus;
	delete bus;
}


static status_t
i2c_scan_bus(void* _bus)
{
	I2CBus* bus = (I2CBus*)_bus;
	return bus->Scan();
}


status_t
i2c_bus_exec_command(void* _bus, i2c_op op, i2c_addr slaveAddress,
	const void* cmdBuffer, size_t cmdLength, void* dataBuffer,
	size_t dataLength)
{
	CALLED();
	I2CBus* bus = (I2CBus*)_bus;
	return bus->ExecCommand(op, slaveAddress, cmdBuffer, cmdLength, dataBuffer,
		dataLength);
}


static status_t
i2c_bus_acquire_bus(void* _bus)
{
	CALLED();
	I2CBus* bus = (I2CBus*)_bus;
	return bus->AcquireBus();
}


static void
i2c_bus_release_bus(void* _bus)
{
	CALLED();
	I2CBus* bus = (I2CBus*)_bus;
	bus->ReleaseBus();
}


static status_t
std_ops(int32 op, ...)
{
	switch (op) {
		case B_MODULE_INIT:
		case B_MODULE_UNINIT:
			return B_OK;

		default:
			break;
	}

	return B_ERROR;
}


i2c_bus_interface gI2CBusModule = {
	{
		{
			I2C_BUS_MODULE_NAME,
			0,
			std_ops
		},

		NULL, // supported devices
		NULL, // register node
		i2c_init_bus,
		i2c_uninit_bus,
		i2c_scan_bus, // register child devices
		NULL, // rescan
	},

	i2c_bus_exec_command,
	i2c_bus_acquire_bus,
	i2c_bus_release_bus,
};
