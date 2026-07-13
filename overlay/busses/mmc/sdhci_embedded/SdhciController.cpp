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
#include <util/AutoLock.h>

#include "AcpiCrs.h"
#include "BytAcpi.h"
#include "Card.h"
#include "Disk.h"
#include "Matcher.h"
#include "Personality.h"

extern iosf_mbi_module_info* gIosfMbi;

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
	delete fDisk;
	fDisk = nullptr;
	delete fCard;
	fCard = nullptr;
	fEngine.Uninit();
	if (fRegisterArea >= 0)
		delete_area(fRegisterArea);
}


bool
SdhciController::MediaPresent() const
{
	return fDisk != nullptr && fDisk->IsOnline();
}


status_t
SdhciController::RecoverCard()
{
	MutexLocker locker(fRecoveryLock);
	if (fCard == nullptr)
		return B_NO_INIT;
	if (fDisk != nullptr) {
		fDisk->SetOnline(false);
		fDisk->LockMediaIo();
	}

	status_t status = fCard->Reidentify(fEngine);
	if (status == B_OK && fDisk != nullptr)
		fDisk->SetOnline(true);
	if (fDisk != nullptr)
		fDisk->UnlockMediaIo();
	return status;
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
	_InitPlatformControl();

	// Bay Trail only: clear the SCC over-current-protection timeout via IOSF-MBI
	// BEFORE we touch a single SDHCI register. If OCP is left armed, the reset
	// and every long (R1b / data) command trips it and the controller wedges.
	// This runs on a different bus (PCI sideband on the host bridge), so it is
	// independent of our MMIO mapping and must precede fEngine.Init()'s reset.
	status = _ApplyOcpFixup();
	if (status != B_OK)
		return status;

	fEngine.SetPlatformControl(this);
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


void
SdhciController::_InitPlatformControl()
{
	device_node* parent = gDeviceManager->get_parent_node(fNode);
	if (parent == nullptr)
		return;

	uint32 functions = 0;
	if (AcpiEvaluateBytDsm(parent, 0, functions) == B_OK) {
		fDsmFunctions = functions;
		if ((functions & (1u << 8)) != 0)
			AcpiEvaluateBytDsm(parent, 8, fDsmUhsCapabilities);
		JR_TRACE_ALWAYS(fLabel, "Intel DSM functions %#" B_PRIx32
			", UHS mask %#" B_PRIx32 "\n", fDsmFunctions,
			fDsmUhsCapabilities);
	} else {
		JR_TRACE_ALWAYS(fLabel, "Intel SDHCI DSM unavailable; UHS disabled\n");
	}

	gDeviceManager->put_node(parent);
}


status_t
SdhciController::SwitchSignalVoltage(bool to1v8)
{
	const uint32 function = to1v8 ? 3 : 4;
	if ((fDsmFunctions & (1u << function)) == 0) {
		// The eMMC rail is fixed at 1.8 V on Winky and does not need a board
		// mux operation. A removable SD slot must advertise the DSM switch.
		return !fRemovable && to1v8 ? B_OK : B_NOT_SUPPORTED;
	}

	device_node* parent = gDeviceManager->get_parent_node(fNode);
	if (parent == nullptr)
		return B_ERROR;
	uint32 result = 0;
	status_t status = AcpiEvaluateBytDsm(parent, function, result);
	gDeviceManager->put_node(parent);
	if (status != B_OK) {
		JR_ERROR(fLabel, "Intel DSM voltage switch fn %" B_PRIu32
			" failed: %s\n", function, strerror(status));
	}
	return status;
}


// Resolve the personality, quirks, and pretty label through the same BYT ACPI
// classifier that claimed the parent node.
status_t
SdhciController::_SelectPersonality()
{
	device_node* parent = gDeviceManager->get_parent_node(fNode);
	if (parent == nullptr)
		return B_ERROR;

	const MatchProfile* profile = ProfileForBytAcpiNode(parent);
	gDeviceManager->put_node(parent);

	if (profile == nullptr)
		return B_NAME_NOT_FOUND;

	fPersonality = &GetPersonality(profile->personality);
	fEngine.SetQuirks(profile->quirks);
	fQuirks = profile->quirks;
	fRemovable = profile->removable;
	fDialect = profile->dialect;
	fDmaStrategy = profile->dma;

	// Clean interim label from the dialect (e.g. sdhci_emb:eMMC); _IdentifyCard
	// refines it to the concrete card once probed.
	snprintf(fLabel.text, sizeof(fLabel.text), "sdhci_emb:%s",
		DialectLabel(profile->dialect));
	return B_OK;
}


// Bay Trail OCP fixup (quirk-gated). This BSP owns the complete controller
// contract, so IOSF access and a verified clear are prerequisites rather than
// firmware assumptions.
status_t
SdhciController::_ApplyOcpFixup()
{
	if (!Has(fQuirks, Quirk::NeedsIosfOcpFixup))
		return B_OK;

	if (gIosfMbi == nullptr) {
		JR_ERROR(fLabel, "IOSF-MBI dependency unavailable\n");
		return B_NO_INIT;
	}

	uint32_t val = 0;
	status_t status = gIosfMbi->read(kSccepUnit, IOSF_MBI_CR_READ, kOcpNetCtrl0Reg,
		&val);
	if (status != B_OK) {
		JR_ERROR(fLabel, "IOSF-MBI: SCCEP 0x%" B_PRIx32 " read failed: %s\n",
			kOcpNetCtrl0Reg, strerror(status));
		return status;
	}
	if ((val & kOcpTimeoutMask) == 0) {
		JR_TRACE_ALWAYS(fLabel, "IOSF-MBI: OCP timeout already clear\n");
		return B_OK;
	}

	const uint32_t requested = val & ~kOcpTimeoutMask;
	status = gIosfMbi->write(kSccepUnit, IOSF_MBI_CR_WRITE, kOcpNetCtrl0Reg,
		requested);
	if (status != B_OK) {
		JR_ERROR(fLabel, "IOSF-MBI: SCCEP 0x%" B_PRIx32 " write failed: %s\n",
			kOcpNetCtrl0Reg, strerror(status));
		return status;
	}

	uint32_t verified = 0;
	status = gIosfMbi->read(kSccepUnit, IOSF_MBI_CR_READ, kOcpNetCtrl0Reg,
		&verified);
	if (status != B_OK) {
		JR_ERROR(fLabel, "IOSF-MBI: SCCEP 0x%" B_PRIx32
			" verification read failed: %s\n", kOcpNetCtrl0Reg,
			strerror(status));
		return status;
	}
	if ((verified & kOcpTimeoutMask) != 0) {
		JR_ERROR(fLabel, "IOSF-MBI: OCP timeout clear did not stick"
			" (requested 0x%08" B_PRIx32 ", read 0x%08" B_PRIx32 ")\n",
			requested, verified);
		return B_IO_ERROR;
	}

	JR_TRACE_ALWAYS(fLabel,
		"IOSF-MBI: OCP timeout cleared and verified (0x%08" B_PRIx32 ")\n",
		verified);
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


// The interrupt handler runs in interrupt context and delegates the minimal raw
// source filter plus lossy wake hint to the engine. Command interpretation and
// interrupt acknowledgement remain worker-only.
int32
SdhciController::_InterruptHandler(void* self)
{
	return static_cast<SdhciController*>(self)->fEngine.HandleInterruptMeow();
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
	fCard = Card::Create(fEngine, fDialect);
	if (fCard == nullptr) {
		if (fRemovable && fDialect == CardDialect::Sd) {
			fCard = new(std::nothrow) SdCard(false);
			if (fCard == nullptr)
				return B_NO_MEMORY;
			fEngine.SetDialect(CardDialect::Sd);
			JR_TRACE_ALWAYS(fLabel,
				"empty removable slot; publishing an offline disk\n");
			return B_OK;
		}
		JR_ERROR(fLabel, "no card responded during identification\n");
		return B_DEVICE_NOT_FOUND;
	}

	// Relabel now that we know exactly what this is (e.g. sdhci_emb:eMMC).
	snprintf(fLabel.text, sizeof(fLabel.text), "sdhci_emb:%s",
		fCard->PrettyName());
	fEngine.SetDialect(fCard->Dialect());
	return B_OK;
}


status_t
SdhciController::_PublishDisk()
{
	fDisk = Disk::Create(fDmaStrategy, *this, *fCard, fEngine);
	if (fDisk == nullptr)
		return B_NO_MEMORY;
	if (fCard->SectorCount() == 0)
		fDisk->SetOnline(false);

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
		if (!now && present) {
			JR_TRACE_ALWAYS(fLabel, "card removed\n");
			if (fDisk != nullptr)
				fDisk->SetOnline(false);
			present = now;
			continue;
		}
		if (now && (!present || (fDisk != nullptr && !fDisk->IsOnline()))) {
			JR_TRACE_ALWAYS(fLabel, "card inserted; identifying\n");
			if (fDisk != nullptr)
				fDisk->SetOnline(false);
			status_t status = RecoverCard();
			if (status == B_OK) {
				fDisk->SetOnline(true);
				JR_TRACE_ALWAYS(fLabel, "card online: %llu sectors\n",
					(unsigned long long)fCard->SectorCount());
			} else {
				JR_ERROR(fLabel, "card re-identification failed: %s\n",
					strerror(status));
			}
			present = now;
		}
	}
	return 0;
}


} // namespace jr::sdhci
