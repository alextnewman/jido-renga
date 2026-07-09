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


} // namespace jr::sdhci
