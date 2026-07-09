// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot

#include "SdhciController.h"

#include <new>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ACPI.h>
#include <common/iosf_mbi.h>

#include "AcpiCrs.h"
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


// Bay Trail SCC (Storage Control Cluster) over-current-protection register,
// reached through the IOSF-MBI sideband. Clearing the timeout-base field
// (bits [10:8]) of the SCCEP net-control register stops OCP from firing on any
// command that takes more than a few microseconds. Values are the BYT-specific
// magic; the MBI transport is the generic bus manager.
namespace {
constexpr uint8_t  kSccepUnit       = 0x63;
constexpr uint32_t kOcpNetCtrl0Reg  = 0x1078;
constexpr uint32_t kOcpTimeoutMask  = 0x00000700;	// bits [10:8]
} // unnamed namespace


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
	// Remove the ISR before tearing the engine down so no late meow can pulse a
	// half-destroyed condition variable.
	if (fInterruptInstalled) {
		remove_io_interrupt_handler(fIrq, _InterruptHandler, this);
		fInterruptInstalled = false;
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

	// Bay Trail only: clear the SCC over-current-protection timeout via IOSF-MBI
	// BEFORE we touch a single SDHCI register. If OCP is left armed, the reset
	// and every long (R1b / data) command trips it and the controller wedges.
	// This runs on a different bus (PCI sideband on the host bridge), so it is
	// independent of our MMIO mapping and must precede fEngine.Init()'s reset.
	status = _ApplyOcpFixup();
	if (status != B_OK)
		return status;

	status = fEngine.Init(fRegs, fPersonality, fLabel);
	if (status != B_OK)
		return status;

	// The engine's worker is now running (with its idle-drain), so it is safe to
	// route the hardware line into the "meow". We install after Init -- never
	// before -- so an early pulse can never reach an unbuilt condition variable;
	// until this point the worker relied on its recheck timer, which is only a
	// latency difference, not a correctness one.
	status = _InstallInterrupt();
	if (status != B_OK)
		return status;

	// Init() has already raised VDD and dropped the bus to the 400 kHz
	// identification clock (mirroring the reference constructor), so probing can
	// begin immediately.
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
	fQuirks = profile->quirks;
	fRemovable = profile->removable;

	// Clean interim label from the dialect (e.g. sdhci_emb:eMMC); _IdentifyCard
	// refines it to the concrete card once probed.
	snprintf(fLabel.text, sizeof(fLabel.text), "sdhci_emb:%s",
		DialectLabel(profile->dialect));
	return B_OK;
}


// Bay Trail OCP fixup (quirk-gated). A SOFT dependency: if the iosf_mbi bus
// manager is absent (any non-BYT host), we log and continue -- the SD path the
// reference proved does not itself require the fixup, so a missing module must
// never fail boot. The module is only "available" once it has actually bound
// the host bridge, so get_module succeeding means the sideband is usable.
status_t
SdhciController::_ApplyOcpFixup()
{
	if (!Has(fQuirks, Quirk::NeedsIosfOcpFixup))
		return B_OK;

	iosf_mbi_module_info* mbi = nullptr;
	if (get_module(B_IOSF_MBI_MODULE_NAME, (module_info**)&mbi) != B_OK) {
		JR_TRACE_ALWAYS(fLabel,
			"IOSF-MBI unavailable; skipping OCP fixup (soft dependency)\n");
		return B_OK;
	}

	uint32_t val = 0;
	status_t status = mbi->read(kSccepUnit, IOSF_MBI_CR_READ, kOcpNetCtrl0Reg,
		&val);
	if (status != B_OK) {
		JR_ERROR(fLabel, "IOSF-MBI: SCCEP 0x%" B_PRIx32 " read failed: %s\n",
			kOcpNetCtrl0Reg, strerror(status));
	} else if ((val & kOcpTimeoutMask) == 0) {
		JR_TRACE_ALWAYS(fLabel, "IOSF-MBI: OCP timeout already clear\n");
	} else {
		val &= ~kOcpTimeoutMask;
		status = mbi->write(kSccepUnit, IOSF_MBI_CR_WRITE, kOcpNetCtrl0Reg, val);
		if (status != B_OK) {
			JR_ERROR(fLabel, "IOSF-MBI: SCCEP 0x%" B_PRIx32 " write failed: %s\n",
				kOcpNetCtrl0Reg, strerror(status));
		} else {
			JR_TRACE_ALWAYS(fLabel,
				"IOSF-MBI: OCP timeout cleared (0x%08" B_PRIx32 ")\n", val);
		}
	}

	put_module(B_IOSF_MBI_MODULE_NAME);
	// The fixup is best-effort: a sideband hiccup should not abort a boot that
	// may still work, so we always report success and let the card probe judge.
	return B_OK;
}


// ACPI _CRS walk: FixedMemory32 -> MMIO base/len, (Extended)Irq -> line. The
// ACPICA-typed walk itself lives in the isolated AcpiCrs TU; here we just map the
// register window it found and remember the interrupt line for _InstallInterrupt.
status_t
SdhciController::_MapResources()
{
	// Flattened topology: register_node() bound us directly under the ACPI
	// device, so our immediate parent *is* the node carrying _CRS (one level up,
	// unlike the reference driver, which has an extra SDHC-bus meta-node).
	device_node* parent = gDeviceManager->get_parent_node(fNode);
	if (parent == nullptr)
		return B_ERROR;

	AcpiCrsResources crs;
	status_t status = AcpiReadCrs(parent, crs);
	gDeviceManager->put_node(parent);
	if (status != B_OK) {
		JR_ERROR(fLabel, "could not read ACPI _CRS: %s\n", strerror(status));
		return status;
	}
	if (!crs.haveIrq) {
		JR_ERROR(fLabel, "ACPI _CRS carried no interrupt line\n");
		return B_DEV_RESOURCE_CONFLICT;
	}

	fRegisterArea = map_physical_memory("sdhci_emb regs", crs.base, crs.length,
		B_ANY_KERNEL_BLOCK_ADDRESS, B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA,
		(void**)&fRegs);
	if (fRegisterArea < B_OK) {
		JR_ERROR(fLabel, "could not map %" B_PRIuSIZE " register bytes at %#"
			B_PRIxPHYSADDR ": %s\n", crs.length, crs.base,
			strerror(fRegisterArea));
		fRegs = nullptr;
		return fRegisterArea;
	}

	fIrq = crs.irq;
	JR_TRACE_ALWAYS(fLabel, "mapped regs %#" B_PRIxPHYSADDR " (%" B_PRIuSIZE
		" bytes), irq %u\n", crs.base, crs.length, fIrq);
	return B_OK;
}


// The interrupt handler runs in interrupt context and does the bare minimum: it
// forwards to the engine's "meow" (a lossy CV pulse, no register access) and
// reports the line handled. Bay Trail gives each controller its own line, so we
// never need to read status just to disown a shared interrupt.
int32
SdhciController::_InterruptHandler(void* self)
{
	static_cast<SdhciController*>(self)->fEngine.HandleInterruptMeow();
	return B_HANDLED_INTERRUPT;
}


status_t
SdhciController::_InstallInterrupt()
{
	status_t status = install_io_interrupt_handler(fIrq, _InterruptHandler, this,
		0);
	if (status != B_OK) {
		JR_ERROR(fLabel, "could not install irq %u handler: %s\n", fIrq,
			strerror(status));
		return status;
	}
	fInterruptInstalled = true;
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
	fEngine.SetClock(25000);	// step up to 25 MHz after identification (fork parity)
	return B_OK;
}


status_t
SdhciController::_PublishDisk()
{
	// Strategy follows card dialect, mirroring the working fork: SD cards ride
	// the proven single-buffer SDMA path, while eMMC uses ADMA2 scatter/gather.
	// (ADMA2 is the known-suspect path -- but eMMC is the only caller, and eMMC
	// bring-up is itself still unproven, so this keeps the SD path on the code
	// that actually works.) The devfs node itself is created by
	// register_child_devices in the glue.
	const DmaStrategy strategy = (fCard->Dialect() == CardDialect::Mmc)
		? DmaStrategy::Adma2
		: DmaStrategy::Sdma;

	fDisk = Disk::Create(strategy, *this, *fCard, fEngine);
	if (fDisk == nullptr)
		return B_NO_MEMORY;

	fCardPublished = true;
	JR_TRACE_ALWAYS(fLabel, "disk ready via %s\n", fDisk->StrategyName());
	return B_OK;
}


void
SdhciController::_StartWatcher()
{
	// The full thread topology, made explicit: boot is a single ACTIVE INIT on
	// the device_manager thread (already done by the time we get here), and this
	// watcher covers only FUTURE insert/remove events. A soldered eMMC part has
	// no card-detect line and can never change, so we spawn NOTHING for it -- the
	// controller then runs with exactly one long-lived thread (the engine
	// worker). A removable SD slot adds this one poller. Nothing hot-plug-related
	// ever runs at boot.
	if (!fRemovable) {
		JR_TRACE_ALWAYS(fLabel, "non-removable slot: no hot-plug watcher\n");
		return;
	}

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
