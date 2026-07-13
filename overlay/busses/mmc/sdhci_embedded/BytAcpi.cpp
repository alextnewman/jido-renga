// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot

#include "BytAcpi.h"

#include <string.h>

#include <ACPI.h>
#include <KernelExport.h>
#include <device_manager.h>


extern device_manager_info* gDeviceManager;


namespace jr::sdhci {


const MatchProfile*
ProfileForBytAcpiNode(device_node* node) noexcept
{
	if (node == nullptr)
		return nullptr;

	uint32 type;
	if (gDeviceManager->get_attr_uint32(node, ACPI_DEVICE_TYPE_ITEM, &type,
			false) != B_OK
		|| type != ACPI_TYPE_DEVICE) {
		return nullptr;
	}

	const char* hid;
	if (gDeviceManager->get_attr_string(node, ACPI_DEVICE_HID_ITEM, &hid, false)
			!= B_OK) {
		return nullptr;
	}

	const char* uid;
	if (gDeviceManager->get_attr_string(node, ACPI_DEVICE_UID_ITEM, &uid, false)
			!= B_OK) {
		return nullptr;
	}

	const char* cid;
	if (gDeviceManager->get_attr_string(node, ACPI_DEVICE_CID_ITEM, &cid, false)
			!= B_OK
		|| strcmp(cid, "PNP0D40") != 0) {
		return nullptr;
	}

	return MatchProfileFor(hid);
}


} // namespace jr::sdhci
