/*
 * Copyright 2025 Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		B Krishnan Iyer, krishnaniyer97@gmail.com
 */
#ifndef _IOSF_MBI_H
#define _IOSF_MBI_H


#include <device_manager.h>


/*
 * Intel On-Chip System Fabric Message Buffer Interface (IOSF-MBI).
 *
 * Provides register access to sideband units on BayTrail and related Atom
 * platforms via PCI configuration space. Used by other drivers (SDHCI,
 * audio, thermal) to apply chipset errata workarounds during probe.
 */

#define B_IOSF_MBI_MODULE_NAME	"bus_managers/iosf_mbi/v1"

/* IOSF-MBI opcodes */
#define IOSF_MBI_CR_READ		0x06
#define IOSF_MBI_CR_WRITE		0x07

/* BayTrail unit ports */
#define IOSF_MBI_UNIT_AUNIT		0x00
#define IOSF_MBI_UNIT_SMC			0x01
#define IOSF_MBI_UNIT_CPU			0x02
#define IOSF_MBI_UNIT_BUNIT		0x03
#define IOSF_MBI_UNIT_PMC			0x04
#define IOSF_MBI_UNIT_GFX			0x06
#define IOSF_MBI_UNIT_SMI			0x0c
#define IOSF_MBI_UNIT_CCK			0x14
#define IOSF_MBI_UNIT_USB			0x43
#define IOSF_MBI_UNIT_SATA		0xa3
#define IOSF_MBI_UNIT_PCIE		0xa6


/*
 * Module interface struct -- resolved at runtime via get_module().
 * Consumer drivers obtain this pointer and call through the function
 * pointers rather than linking directly (modules are separate .so files).
 */
typedef struct iosf_mbi_module_info {
	module_info			info;

	status_t	(*read)(uint8_t port, uint8_t opcode, uint32_t offset,
				uint32_t* value);
	status_t	(*write)(uint8_t port, uint8_t opcode, uint32_t offset,
				uint32_t value);

} iosf_mbi_module_info;


#endif /* _IOSF_MBI_H */
