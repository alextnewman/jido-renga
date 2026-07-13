// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot

#include "AcpiCrs.h"

// <ACPI.h> is the Haiku device-manager ACPI module interface: it forward-
// declares `struct acpi_resource` and types the walk_resources callback in
// terms of it. "acpi.h" is the ACPICA umbrella, which *completes* that same
// struct tag with the capital-field ACPI_RESOURCE layout the bus manager
// actually delivers at runtime. Including both here (and nowhere else) is what
// lets the callback below read res->Type / res->Data with the real layout while
// keeping the incompatible Haiku-native definition out of this TU. The overlay
// Jamfile places acpica/include ahead of the private kernel headers so "acpi.h"
// resolves to ACPICA; do not include <private/kernel/acpi.h> here.
#include <ACPI.h>
#include "acpi.h"

#include <stdlib.h>
#include <string.h>

extern device_manager_info* gDeviceManager;


namespace jr::sdhci {

namespace {


// walk_resources callback. Runs once per resource descriptor in _CRS. Reads the
// ACPICA layout (capital Type/Data); the anonymous union in ACPI_RESOURCE_IRQ /
// _EXTENDED_IRQ exposes the first line as the singular `.Interrupt`.
acpi_status
ReadCrsCallback(ACPI_RESOURCE* res, void* context)
{
	AcpiCrsResources* out = static_cast<AcpiCrsResources*>(context);

	switch (res->Type) {
		case ACPI_RESOURCE_TYPE_FIXED_MEMORY32:
			out->base = res->Data.FixedMemory32.Address;
			out->length = res->Data.FixedMemory32.AddressLength;
			break;
		case ACPI_RESOURCE_TYPE_IRQ:
			out->irq = res->Data.Irq.Interrupt;
			out->haveIrq = true;
			break;
		case ACPI_RESOURCE_TYPE_EXTENDED_IRQ:
			out->irq = static_cast<uint8_t>(res->Data.ExtendedIrq.Interrupt);
			out->haveIrq = true;
			break;
		default:
			break;
	}

	return B_OK;
}


} // namespace


status_t
AcpiReadCrs(device_node* acpiNode, AcpiCrsResources& out)
{
	acpi_device_module_info* acpi = nullptr;
	acpi_device device = nullptr;
	status_t status = gDeviceManager->get_driver(acpiNode,
		(driver_module_info**)&acpi, (void**)&device);
	if (status != B_OK)
		return status;

	if (acpi->walk_resources(device, const_cast<char*>("_CRS"),
			ReadCrsCallback, &out) != B_OK) {
		return B_IO_ERROR;
	}

	if (out.base == 0 || out.length == 0)
		return B_IO_ERROR;

	return B_OK;
}


status_t
AcpiEvaluateBytDsm(device_node* acpiNode, uint32 function, uint32& result)
{
	static const uint8 kIntelDsmGuid[16] = {
		0xa5, 0x3e, 0xc1, 0xf6, 0xcd, 0x65, 0x1f, 0x46,
		0xab, 0x7a, 0x29, 0xf7, 0xe8, 0xd5, 0xbd, 0x61
	};

	acpi_device_module_info* acpi = nullptr;
	acpi_device device = nullptr;
	status_t status = gDeviceManager->get_driver(acpiNode,
		(driver_module_info**)&acpi, (void**)&device);
	if (status != B_OK)
		return status;

	acpi_object_type arguments[4] = {};
	arguments[0].object_type = ACPI_TYPE_BUFFER;
	arguments[0].buffer.length = sizeof(kIntelDsmGuid);
	arguments[0].buffer.buffer = const_cast<uint8*>(kIntelDsmGuid);
	arguments[1].object_type = ACPI_TYPE_INTEGER;
	arguments[1].integer.integer = 0;
	arguments[2].object_type = ACPI_TYPE_INTEGER;
	arguments[2].integer.integer = function;
	arguments[3].object_type = ACPI_TYPE_PACKAGE;
	arguments[3].package.count = 0;
	arguments[3].package.objects = nullptr;
	acpi_objects args = {4, arguments};

	acpi_data response = {ACPI_ALLOCATE_BUFFER, nullptr};
	status = acpi->evaluate_method(device, "_DSM", &args, &response);
	if (status != B_OK || response.pointer == nullptr)
		return status != B_OK ? status : B_BAD_DATA;

	acpi_object_type* object = static_cast<acpi_object_type*>(response.pointer);
	result = 0;
	if (object->object_type == ACPI_TYPE_INTEGER) {
		result = static_cast<uint32>(object->integer.integer);
	} else if (object->object_type == ACPI_TYPE_BUFFER
		&& object->buffer.buffer != nullptr && object->buffer.length > 0) {
		const size_t bytes = object->buffer.length < sizeof(result)
			? object->buffer.length : sizeof(result);
		memcpy(&result, object->buffer.buffer, bytes);
	} else {
		status = B_BAD_DATA;
	}

	free(response.pointer);
	return status;
}


} // namespace jr::sdhci
