// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot
#pragma once

#include <KernelExport.h>
#include <device_manager.h>

#include "HotplugState.h"
#include "Personality.h"
#include "SdhciEngine.h"
#include "SdhciRegisters.h"
#include "Trace.h"
#include "Types.h"

namespace jr::sdhci {

class Card;
class Disk;


// device_manager controller node. It owns ACPI resources, the engine,
// personality, card, and disk for one hardware slot.
//
// Boot is serialized:
//   1. Map resources, reset, apply personality fixups.
//   2. Power the bus and *synchronously* identify the card.
//   3. Publish the Disk node -> devfs, so boot-from-SD wins the RAMDisk race.
//   4. Only then start the async insert/remove watcher thread.
class SdhciController : public IPlatformControl {
public:
	explicit SdhciController(device_node* node);
	virtual ~SdhciController();

	SdhciController(const SdhciController&) = delete;
	SdhciController& operator=(const SdhciController&) = delete;

	// Steps 1-3 above. Returns B_OK only once the Disk is published.
	status_t InitCheck() const { return fInitStatus; }
	status_t Boot();

	SdhciEngine& Engine() { return fEngine; }
	Card* ActiveCard() const { return fCard; }
	Disk* ActiveDisk() const { return fDisk; }
	const TraceLabel& Label() const { return fLabel; }
	bool IsRemovable() const { return fRemovable; }
	bool CardPresent() const { return fEngine.CardPresent(); }
	status_t RecoverCard();
	status_t EjectMedia();
	status_t SwitchSignalVoltage(bool to1v8) override;
	uint8_t UhsCapabilities() const override
	{
		return static_cast<uint8_t>(fDsmUhsCapabilities & 0x0f);
	}

	device_node* Node() const { return fNode; }

private:
	status_t _MapResources();
	status_t _SelectPersonality();
	status_t _ApplyOcpFixup();		// IOSF-MBI, before any SDHCI register access
	void _InitPlatformControl();
	status_t _InstallInterrupt();	// after Engine.Init, before identify
	status_t _IdentifyCard();		// synchronous
	status_t _PublishDisk();
	void _StartWatcher();			// lazy, after publish; only if removable

	// ISR trampoline -> Engine.HandleInterruptMeow(). The engine performs only a
	// raw-source filter before forwarding a content-free, lossy CV pulse.
	static int32 _InterruptHandler(void* self);

	static int32 _WatcherEntry(void* self);
	int32 _WatcherLoop();

	device_node*			fNode = nullptr;
	TraceLabel				fLabel;
	status_t				fInitStatus = B_NO_INIT;

	area_id					fRegisterArea = -1;
	volatile RegisterBlock*	fRegs = nullptr;
	uint8_t					fIrq = 0;
	bool					fInterruptInstalled = false;
	uint32					fDsmFunctions = 0;
	uint32					fDsmUhsCapabilities = 0;

	const HostPersonality*	fPersonality = nullptr;
	Quirk					fQuirks = Quirk::None;
	SdhciEngine				fEngine;
	Card*					fCard = nullptr;
	Disk*					fDisk = nullptr;

	thread_id				fWatcher = -1;
	volatile bool			fWatcherRunning = false;
	bool					fRemovable = false;		// gates the hot-plug watcher
	HotplugState			fHotplug;
	CardDialect				fDialect = CardDialect::Unknown;
	DmaStrategy				fDmaStrategy = DmaStrategy::None;
	bool					fCardPublished = false;
	mutex					fRecoveryLock
		= MUTEX_INITIALIZER("sdhci_emb recovery");
};


} // namespace jr::sdhci
