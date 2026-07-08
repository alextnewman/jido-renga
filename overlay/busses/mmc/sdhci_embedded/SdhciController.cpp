// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot

#include "SdhciController.h"

#include <new>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ACPI.h>

#include "Card.h"
#include "Disk.h"
#include "Matcher.h"
#include "Personality.h"

// The controller collapses the whole upstream chain into node #1. Boot() runs
// the serialized bring-up (map -> personality -> reset -> power -> identify ->
// publish) so a boot-from-SD card is ready before the RAMDisk race is decided,
// then starts the lazy insert/remove watcher.

extern device_manager_info* gDeviceManager;


namespace jr::sdhci {


SdhciController::SdhciController(device_node* node)
	:
	fNode(node)
{
}


SdhciController::~SdhciController()
{
	if (fWatcherRunning) {
		fWatcherRunning = false;
		status_t ignored;
		wait_for_thread(fWatcher, &ignored);
	}
	fEngine.Uninit();
	delete fDisk;
	delete fCard;
	if (fRegisterArea >= 0)
		delete_area(fRegisterArea);
}


status_t
SdhciController::Boot()
{
	status_t status = _SelectPersonality();
	if (status != B_OK)
		return status;

	status = _MapResources();
	if (status != B_OK)
		return status;

	status = fEngine.Init(fRegs, fPersonality, fLabel);
	if (status != B_OK)
		return status;

	// Power the bus at the identification clock (400 kHz), then step up once the
	// card is known. Bay Trail wants a settle delay after VDD comes up.
	fEngine.PowerOn(PowerControlReg::k3v3);
	snooze(10000);
	fEngine.SetClock(400);

	status = _IdentifyCard();
	if (status != B_OK)
		return status;

	status = _PublishDisk();
	if (status != B_OK)
		return status;

	_StartWatcher();

	fInitStatus = B_OK;
	JR_TRACE_ALWAYS(fLabel, "boot complete: %llu sectors of %" B_PRIu32 " bytes\n",
		(unsigned long long)fCard->SectorCount(), fCard->SectorSize());
	return B_OK;
}


// Resolve the personality, quirks, and pretty label from the ExplicitMatcher,
// keyed on the ACPI HID/UID of our parent node.
status_t
SdhciController::_SelectPersonality()
{
	device_node* parent = gDeviceManager->get_parent_node(fNode);
	if (parent == nullptr)
		return B_ERROR;

	const char* hid = nullptr;
	const char* uidString = nullptr;
	uint32_t uid = kAnyUid;
	gDeviceManager->get_attr_string(parent, ACPI_DEVICE_HID_ITEM, &hid, false);
	if (gDeviceManager->get_attr_string(parent, ACPI_DEVICE_UID_ITEM, &uidString,
			false) == B_OK) {
		char* end = nullptr;
		unsigned long parsed = strtoul(uidString, &end, 10);
		if (end != uidString)
			uid = static_cast<uint32_t>(parsed);
	}
	gDeviceManager->put_node(parent);

	const MatchProfile* profile = MatchProfileFor(hid, uid);
	if (profile == nullptr)
		return B_NAME_NOT_FOUND;

	fPersonality = &GetPersonality(profile->personality);
	fEngine.SetQuirks(profile->quirks);

	// Clean interim label from the dialect (e.g. sdhci_emb:eMMC); _IdentifyCard
	// refines it to the concrete card once probed.
	snprintf(fLabel.text, sizeof(fLabel.text), "sdhci_emb:%s",
		DialectLabel(profile->dialect));
	return B_OK;
}


// ACPI _CRS walk: FixedMemory32 -> MMIO base/len, (Extended)Irq -> line.
// Implementation point: wire acpi->walk_resources with a parse callback exactly
// as the reference driver does, then map_physical_memory the register block.
status_t
SdhciController::_MapResources()
{
	// device_node* parent = gDeviceManager->get_parent_node(fNode);
	// acpi_device_module_info* acpi; acpi_device device;
	// gDeviceManager->get_driver(parent, (driver_module_info**)&acpi,
	//     (void**)&device);
	// acpi->walk_resources(device, "_CRS", parse_crs, &crs);
	// fRegisterArea = map_physical_memory("sdhci_emb regs", crs.base, crs.len,
	//     B_ANY_KERNEL_BLOCK_ADDRESS, B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA,
	//     (void**)&fRegs);
	// fIrq = crs.irq;

	if (fRegs == nullptr)
		return B_DEV_RESOURCE_CONFLICT;
	return B_OK;
}


status_t
SdhciController::_IdentifyCard()
{
	fCard = Card::Probe(fEngine);
	if (fCard == nullptr) {
		JR_ERROR(fLabel, "no card responded during identification\n");
		return B_DEVICE_NOT_FOUND;
	}

	// Relabel now that we know exactly what this is (e.g. sdhci_emb:eMMC).
	snprintf(fLabel.text, sizeof(fLabel.text), "sdhci_emb:%s",
		fCard->PrettyName());
	fEngine.SetDialect(fCard->Dialect());
	fEngine.SetClock(50000);	// step up to high speed after identification
	return B_OK;
}


status_t
SdhciController::_PublishDisk()
{
	// Pick the DMA strategy from the profile (Bay Trail eMMC -> ADMA2). The
	// devfs node itself is created by register_child_devices in the glue.
	fDisk = Disk::Create(DmaStrategy::Adma2, *this, *fCard, fEngine);
	if (fDisk == nullptr)
		return B_NO_MEMORY;

	fCardPublished = true;
	JR_TRACE_ALWAYS(fLabel, "disk ready via %s\n", fDisk->StrategyName());
	return B_OK;
}


void
SdhciController::_StartWatcher()
{
	fWatcherRunning = true;
	fWatcher = spawn_kernel_thread(_WatcherEntry, "sdhci_emb watcher",
		B_LOW_PRIORITY, this);
	if (fWatcher < 0) {
		fWatcherRunning = false;
		return;
	}
	resume_thread(fWatcher);
}


int32
SdhciController::_WatcherEntry(void* self)
{
	return static_cast<SdhciController*>(self)->_WatcherLoop();
}


// Lazy card insert/remove watcher. Deliberately started only AFTER the boot
// card is published, so it can never lose the boot-from-SD race. Polls slowly;
// a removal mid-boot may be missed, which is acceptable (see design doc).
int32
SdhciController::_WatcherLoop()
{
	bool present = fEngine.CardPresent();
	while (fWatcherRunning) {
		snooze(500000);	// 500ms; this is not a hot path
		const bool now = fEngine.CardPresent();
		if (now != present) {
			JR_TRACE_ALWAYS(fLabel, "card %s\n", now ? "inserted" : "removed");
			present = now;
			// Implementation point: re-identify on insert / tear down on remove.
		}
	}
	return 0;
}


} // namespace jr::sdhci
