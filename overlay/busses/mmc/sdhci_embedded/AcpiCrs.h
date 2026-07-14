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
// native type in <private/kernel/acpi.h>; including both in one translation unit
// causes a redefinition. This boundary exports only plain resource data.

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


// Decode the controller's parent ACPI node. B_OK requires a usable MMIO window.
status_t AcpiReadCrs(device_node* acpiNode, AcpiCrsResources& out);

// Evaluate Intel's Bay Trail SDHCI _DSM and decode an integer or up-to-32-bit
// buffer result. The caller enforces the function bitmap returned by function 0.
status_t AcpiEvaluateBytDsm(device_node* acpiNode, uint32 function,
	uint32& result);


} // namespace jr::sdhci
