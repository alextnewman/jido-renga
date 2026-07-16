// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot
#pragma once

#include <common/Gpio.h>

#include <ACPI.h>
#include <condition_variable.h>
#include <lock.h>

#include "BayTrailRegisters.h"


namespace gpio::baytrail {

class BayTrailController final : public Controller {
public:
	explicit BayTrailController(device_node* node);
	~BayTrailController() override;

	BayTrailController(const BayTrailController&) = delete;
	BayTrailController& operator=(const BayTrailController&) = delete;

	status_t Start();

	uint32 PinCount() const override;
	status_t Claim(uint16 pin) override;
	void Release(uint16 pin) override;

	status_t ConfigureInput(uint16 pin,
		const InputConfig& config) override;
	status_t ConfigureOutput(uint16 pin,
		const OutputConfig& config) override;
	status_t Read(uint16 pin, Level& level) const override;
	status_t Write(uint16 pin, Level level) override;

	status_t Watch(uint16 pin, const InterruptConfig& config,
		EventHandler handler, void* context) override;
	void StopWatching(uint16 pin) override;

private:
	static constexpr uint32 kMaximumPins = 102;
	static constexpr uint32 kMaximumStatusWords = 4;

	struct Slot {
		bool				claimed = false;
		bool				watching = false;
		bool				pending = false;
		bool				haveLevel = false;
		Level				lastLevel = Level::Low;
		bigtime_t			due = 0;
		InterruptConfig	config;
		EventHandler		handler = nullptr;
		void*				context = nullptr;
		int32				dispatching = 0;
	};

	status_t _SelectCommunity();
	status_t _MapResources();
	status_t _InitializeHardware();
	status_t _InstallInterrupt();
	status_t _RegisterController();

	volatile uint32* _PadRegister(uint16 pin, size_t reg) const;
	volatile uint32* _StatusRegister(uint32 word) const;
	bool _ValidPin(uint16 pin) const;
	bool _Claimed(uint16 pin) const;

	status_t _ConfigureInputLocked(uint16 pin, const InputConfig& config);
	status_t _ReadLevelLocked(uint16 pin, Level& level) const;
	void _ClearTriggerLocked(uint16 pin);
	void _AcknowledgeLocked(uint16 pin);

	static int32 _InterruptEntry(void* context);
	int32 _HandleInterrupt();

	static int32 _WorkerEntry(void* context);
	int32 _WorkerLoop();
	void _CollectPending();
	void _DispatchDue();
	bigtime_t _NextDeadline() const;

	device_node*			fNode = nullptr;
	Community				fCommunity = Community::Score;
	const char*				fCommunityName = "SCORE";
	acpi_handle				fAcpiHandle = nullptr;

	area_id					fRegisterArea = B_BAD_VALUE;
	volatile uint8*			fRegisters = nullptr;
	uint32					fIrq = 0;
	uint32					fIrqFlags = 0;
	bool					fInterruptInstalled = false;
	bool					fRegistered = false;

	mutable mutex			fLock;
	ConditionVariable		fDispatchIdle;
	Slot					fSlots[kMaximumPins];
	int32					fWatched[kMaximumStatusWords] = {};
	int32					fPending[kMaximumStatusWords] = {};

	sem_id					fEventSem = B_BAD_VALUE;
	thread_id				fWorker = B_BAD_VALUE;
	bool					fRunning = false;
};

} // namespace gpio::baytrail
