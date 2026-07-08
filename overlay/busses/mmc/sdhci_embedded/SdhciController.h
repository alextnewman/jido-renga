// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot
#pragma once

#include <KernelExport.h>
#include <device_manager.h>

#include "Personality.h"
#include "SdhciEngine.h"
#include "SdhciRegisters.h"
#include "Trace.h"
#include "Types.h"

namespace jr::sdhci {

class Card;
class Disk;


// SdhciController -- device_manager node #1 (our driver_module).
//
// This is the whole controller collapsed into one object: it maps the ACPI
// resources (MMIO + IRQ), owns the SdhciEngine, selects the HostPersonality,
// and holds the single Card and single Disk (container-of-1 -- no slot
// enumeration; the spec allows many, this hardware has one).
//
// Boot discipline (decision: serialized-at-boot, then lazy watcher):
//   1. Map resources, reset, apply personality fixups.
//   2. Power the bus and *synchronously* identify the card.
//   3. Publish the Disk node -> devfs, so boot-from-SD wins the RAMDisk race.
//   4. Only then start the async insert/remove watcher thread.
//
// A card yanked mid-boot may be missed; that is acceptable (there is no shell
// yet, and ejecting your boot disk is on you).
class SdhciController {
public:
	explicit SdhciController(device_node* node);
	~SdhciController();

	SdhciController(const SdhciController&) = delete;
	SdhciController& operator=(const SdhciController&) = delete;

	// Steps 1-3 above. Returns B_OK only once the Disk is published.
	status_t InitCheck() const { return fInitStatus; }
	status_t Boot();

	SdhciEngine& Engine() { return fEngine; }
	Card* ActiveCard() const { return fCard; }
	Disk* ActiveDisk() const { return fDisk; }
	const TraceLabel& Label() const { return fLabel; }

	device_node* Node() const { return fNode; }

private:
	status_t _MapResources();
	status_t _SelectPersonality();
	status_t _IdentifyCard();		// synchronous
	status_t _PublishDisk();
	void _StartWatcher();			// lazy, after publish

	static int32 _WatcherEntry(void* self);
	int32 _WatcherLoop();

	device_node*			fNode = nullptr;
	TraceLabel				fLabel;
	status_t				fInitStatus = B_NO_INIT;

	area_id					fRegisterArea = -1;
	volatile RegisterBlock*	fRegs = nullptr;
	uint8_t					fIrq = 0;

	const HostPersonality*	fPersonality = nullptr;
	SdhciEngine				fEngine;
	Card*					fCard = nullptr;
	Disk*					fDisk = nullptr;

	thread_id				fWatcher = -1;
	volatile bool			fWatcherRunning = false;
	bool					fCardPublished = false;
};


} // namespace jr::sdhci
