// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot

#include "I2CPrivate.h"


I2CBus::I2CBus(device_node* node, uint8 id)
	:
	fNode(node),
	fID(id),
	fController(NULL),
	fCookie(NULL)
{
	device_node* parent = gDeviceManager->get_parent_node(node);
	if (parent == NULL)
		return;

	status_t status = gDeviceManager->get_driver(parent,
		(driver_module_info**)&fController, &fCookie);
	gDeviceManager->put_node(parent);
	if (status != B_OK || fController == NULL
		|| fController->set_i2c_bus == NULL
		|| fController->exec_command == NULL) {
		fController = NULL;
		fCookie = NULL;
		return;
	}

	fController->set_i2c_bus(fCookie, this);
}


I2CBus::~I2CBus()
{
}


status_t
I2CBus::InitCheck()
{
	return fController != NULL ? B_OK : B_NO_INIT;
}


status_t
I2CBus::ExecCommand(i2c_op op, i2c_addr slaveAddress,
	const void* cmdBuffer, size_t cmdLength, void* dataBuffer,
	size_t dataLength)
{
	if (fController == NULL)
		return B_NO_INIT;

	return fController->exec_command(fCookie, op, slaveAddress, cmdBuffer,
		cmdLength, dataBuffer, dataLength);
}


status_t
I2CBus::RegisterDevice(i2c_addr slaveAddress, char* hid, char** cid,
	acpi_handle acpiHandle)
{
	device_attr attributes[8];
	size_t count = 0;

	attributes[count++] = { B_DEVICE_PRETTY_NAME, B_STRING_TYPE,
		{ .string = "I2C device" } };
	attributes[count++] = { I2C_DEVICE_SLAVE_ADDR_ITEM, B_UINT16_TYPE,
		{ .ui16 = slaveAddress } };
	attributes[count++] = { B_DEVICE_BUS, B_STRING_TYPE,
		{ .string = "i2c" } };
	attributes[count++] = { B_DEVICE_FLAGS, B_UINT32_TYPE,
		{ .ui32 = B_FIND_MULTIPLE_CHILDREN } };

	if (hid != NULL) {
		attributes[count++] = { ACPI_DEVICE_HID_ITEM, B_STRING_TYPE,
			{ .string = hid } };
	}
	if (cid != NULL && cid[0] != NULL) {
		attributes[count++] = { ACPI_DEVICE_CID_ITEM, B_STRING_TYPE,
			{ .string = cid[0] } };
	}

	attributes[count++] = { ACPI_DEVICE_HANDLE_ITEM, B_UINT64_TYPE,
		{ .ui64 = (addr_t)acpiHandle } };
	attributes[count] = { NULL };

	return gDeviceManager->register_node(fNode, I2C_DEVICE_MODULE_NAME,
		attributes, NULL, NULL);
}


status_t
I2CBus::Scan()
{
	if (fController == NULL)
		return B_NO_INIT;
	if (fController->scan_bus != NULL)
		fController->scan_bus(fCookie);
	return B_OK;
}


status_t
I2CBus::AcquireBus()
{
	if (fController == NULL)
		return B_NO_INIT;
	if (fController->acquire_bus != NULL)
		return fController->acquire_bus(fCookie);
	return B_OK;
}


void
I2CBus::ReleaseBus()
{
	if (fController != NULL && fController->release_bus != NULL)
		fController->release_bus(fCookie);
}


static status_t
init_bus(device_node* node, void** cookie)
{
	uint8 pathID;
	if (gDeviceManager->get_attr_uint8(node, I2C_BUS_PATH_ID_ITEM, &pathID,
			false) != B_OK) {
		return B_BAD_VALUE;
	}

	I2CBus* bus = new(std::nothrow) I2CBus(node, pathID);
	if (bus == NULL)
		return B_NO_MEMORY;

	status_t status = bus->InitCheck();
	if (status != B_OK) {
		delete bus;
		return status;
	}

	*cookie = bus;

	char path[B_DEV_NAME_LENGTH];
	snprintf(path, sizeof(path), "bus/i2c/%u/bus_raw", pathID);
	return gDeviceManager->publish_device(node, path,
		I2C_BUS_RAW_MODULE_NAME);
}


static void
uninit_bus(void* cookie)
{
	delete (I2CBus*)cookie;
}


static status_t
scan_bus(void* cookie)
{
	return ((I2CBus*)cookie)->Scan();
}


static status_t
exec_command(void* cookie, i2c_op op, i2c_addr slaveAddress,
	const void* cmdBuffer, size_t cmdLength, void* dataBuffer,
	size_t dataLength)
{
	return ((I2CBus*)cookie)->ExecCommand(op, slaveAddress, cmdBuffer,
		cmdLength, dataBuffer, dataLength);
}


static status_t
acquire_bus(void* cookie)
{
	return ((I2CBus*)cookie)->AcquireBus();
}


static void
release_bus(void* cookie)
{
	((I2CBus*)cookie)->ReleaseBus();
}


static status_t
std_ops(int32 op, ...)
{
	switch (op) {
		case B_MODULE_INIT:
		case B_MODULE_UNINIT:
			return B_OK;
		default:
			return B_ERROR;
	}
}


i2c_bus_interface gI2CBusModule = {
	{
		{
			I2C_BUS_MODULE_NAME,
			0,
			std_ops
		},
		NULL,
		NULL,
		init_bus,
		uninit_bus,
		scan_bus,
		NULL,
	},
	exec_command,
	acquire_bus,
	release_bus,
};
