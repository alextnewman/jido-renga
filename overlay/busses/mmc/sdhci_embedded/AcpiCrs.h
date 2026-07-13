// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot
#pragma once

#include <SupportDefs.h>
#include <device_manager.h>

// Isolated ACPI _CRS reader.
//
// The only translation unit in the stack that speaks ACPICA's ACPI_RESOURCE
// layout. That type shares its struct tag (`acpi_resource`) with the Haiku-
// native, lowercase-field one in <private/kernel/acpi.h>, so the two can never
// be included in the same TU without a redefinition clash. We quarantine the
// ACPICA side here and hand the rest of the driver a plain POD, so nothing above
// this file has to care which `acpi.h` won the include race.

namespace jr::sdhci {


// The controller resources decoded from an ACPI device's _CRS: a single MMIO
// window and an interrupt line. Bay Trail SDHCI hosts advertise exactly one of
// each (a FixedMemory32 descriptor plus an Irq/ExtendedIrq descriptor).
struct AcpiCrsResources {
	phys_addr_t	base = 0;
	size_t		length = 0;
	uint8_t		irq = 0;
	bool		haveIrq = false;
};


// Walk the _CRS method of `acpiNode` (the ACPI device node -- our controller's
// parent in the flattened topology) and fill `out`. Returns B_OK only when a
// usable MMIO window was found; the caller maps it and installs the IRQ.
status_t AcpiReadCrs(device_node* acpiNode, AcpiCrsResources& out);

// Evaluate Intel's Bay Trail SDHCI _DSM and decode an integer or up-to-32-bit
// buffer result. The caller enforces the function bitmap returned by function 0.
status_t AcpiEvaluateBytDsm(device_node* acpiNode, uint32 function,
	uint32& result);


} // namespace jr::sdhci
