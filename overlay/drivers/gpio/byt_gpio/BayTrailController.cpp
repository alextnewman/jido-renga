// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot

#include "BayTrailController.h"

#include "Trace.h"

#include <arch/int.h>
#include <new>
#include <stdio.h>
#include <string.h>
#include <util/AutoLock.h>

#include "acpi.h"


extern device_manager_info* gDeviceManager;
extern acpi_module_info* gAcpi;
extern gpio::module_info* gGpio;


namespace gpio::baytrail {

namespace {

constexpr bigtime_t kMaximumDebounce = 5000000;
constexpr uint32 kGlitchFilters
	= kGlitchFilterEnable | kSlowGlitchFilter | kFastGlitchFilter;


struct AcpiResources {
	phys_addr_t	base = 0;
	size_t		length = 0;
	uint32		irq = 0;
	uint32		irqFlags = 0;
	bool		haveMemory = false;
	bool		haveIrq = false;
};


acpi_status
ReadResources(acpi_resource* resource, void* context)
{
	AcpiResources* resources = static_cast<AcpiResources*>(context);
	switch (resource->Type) {
		case ACPI_RESOURCE_TYPE_FIXED_MEMORY32:
			resources->base = resource->Data.FixedMemory32.Address;
			resources->length = resource->Data.FixedMemory32.AddressLength;
			resources->haveMemory = true;
			break;

		case ACPI_RESOURCE_TYPE_MEMORY32:
			resources->base = resource->Data.Memory32.Minimum;
			resources->length = resource->Data.Memory32.AddressLength;
			resources->haveMemory = true;
			break;

		case ACPI_RESOURCE_TYPE_IRQ:
			if (resource->Data.Irq.InterruptCount == 0)
				break;
			resources->irq = resource->Data.Irq.Interrupts[0];
			resources->irqFlags
				= resource->Data.Irq.Triggering == ACPI_EDGE_SENSITIVE
					? B_EDGE_TRIGGERED : B_LEVEL_TRIGGERED;
			resources->irqFlags
				|= resource->Data.Irq.Polarity == ACPI_ACTIVE_LOW
					? B_LOW_ACTIVE_POLARITY : B_HIGH_ACTIVE_POLARITY;
			resources->haveIrq = true;
			break;

		case ACPI_RESOURCE_TYPE_EXTENDED_IRQ:
			if (resource->Data.ExtendedIrq.InterruptCount == 0)
				break;
			resources->irq = resource->Data.ExtendedIrq.Interrupts[0];
			resources->irqFlags
				= resource->Data.ExtendedIrq.Triggering == ACPI_EDGE_SENSITIVE
					? B_EDGE_TRIGGERED : B_LEVEL_TRIGGERED;
			resources->irqFlags
				|= resource->Data.ExtendedIrq.Polarity == ACPI_ACTIVE_LOW
					? B_LOW_ACTIVE_POLARITY : B_HIGH_ACTIVE_POLARITY;
			resources->haveIrq = true;
			break;
	}
	return AE_OK;
}


uint32
PullAssignment(Bias bias)
{
	switch (bias) {
		case Bias::None:
			return 0;
		case Bias::PullUp:
			return kPullUp;
		case Bias::PullDown:
			return kPullDown;
		case Bias::Firmware:
			return UINT32_MAX;
	}
	return UINT32_MAX;
}

} // namespace


BayTrailController::BayTrailController(device_node* node)
	:
	fNode(node)
{
	mutex_init(&fLock, "Bay Trail GPIO");
	fDispatchIdle.Init(this, "Bay Trail GPIO dispatch");
}


BayTrailController::~BayTrailController()
{
	if (fRegistered) {
		gGpio->unregister_controller(this);
		fRegistered = false;
	}

	for (uint32 pin = 0; pin < PinCount(); pin++)
		StopWatching(pin);

	if (fInterruptInstalled) {
		remove_io_interrupt_handler(fIrq, _InterruptEntry, this);
		fInterruptInstalled = false;
	}

	{
		MutexLocker locker(fLock);
		fRunning = false;
	}
	if (fEventSem >= B_OK)
		release_sem(fEventSem);
	if (fWorker >= B_OK) {
		status_t ignored;
		wait_for_thread(fWorker, &ignored);
	}
	if (fEventSem >= B_OK)
		delete_sem(fEventSem);
	if (fRegisterArea >= B_OK)
		delete_area(fRegisterArea);
	mutex_destroy(&fLock);
}


status_t
BayTrailController::Start()
{
	status_t status = _SelectCommunity();
	if (status != B_OK)
		return status;
	status = _MapResources();
	if (status != B_OK)
		return status;
	status = _InitializeHardware();
	if (status != B_OK)
		return status;

	fEventSem = create_sem(0, "Bay Trail GPIO events");
	if (fEventSem < B_OK)
		return fEventSem;

	{
		MutexLocker locker(fLock);
		fRunning = true;
	}
	fWorker = spawn_kernel_thread(_WorkerEntry, "Bay Trail GPIO dispatch",
		B_REAL_TIME_DISPLAY_PRIORITY, this);
	if (fWorker < B_OK)
		return fWorker;
	resume_thread(fWorker);

	status = _InstallInterrupt();
	if (status != B_OK)
		return status;
	status = _RegisterController();
	if (status != B_OK)
		return status;

	TRACE("%s community ready: %" B_PRIu32 " pins, IRQ %" B_PRIu32 "\n",
		fCommunityName, PinCount(), fIrq);
	return B_OK;
}


uint32
BayTrailController::PinCount() const
{
	return baytrail::PinCount(fCommunity);
}


status_t
BayTrailController::_SelectCommunity()
{
	device_node* parent = gDeviceManager->get_parent_node(fNode);
	if (parent == nullptr)
		return B_ERROR;

	const char* uid = nullptr;
	const char* path = nullptr;
	status_t status = gDeviceManager->get_attr_string(parent,
		ACPI_DEVICE_UID_ITEM, &uid, false);
	if (status == B_OK) {
		status = gDeviceManager->get_attr_string(parent, ACPI_DEVICE_PATH_ITEM,
			&path, false);
	}
	if (status != B_OK || uid == nullptr || path == nullptr) {
		gDeviceManager->put_node(parent);
		return status != B_OK ? status : B_BAD_VALUE;
	}

	if (strcmp(uid, "1") == 0) {
		fCommunity = Community::Score;
		fCommunityName = "SCORE";
	} else if (strcmp(uid, "2") == 0) {
		fCommunity = Community::Ncore;
		fCommunityName = "NCORE";
	} else if (strcmp(uid, "3") == 0) {
		fCommunity = Community::Sus;
		fCommunityName = "SUS";
	} else {
		gDeviceManager->put_node(parent);
		return B_NOT_SUPPORTED;
	}

	status = gAcpi->get_handle(nullptr, path, &fAcpiHandle);
	gDeviceManager->put_node(parent);
	return status;
}


status_t
BayTrailController::_MapResources()
{
	device_node* parent = gDeviceManager->get_parent_node(fNode);
	if (parent == nullptr)
		return B_ERROR;

	acpi_device_module_info* acpi = nullptr;
	acpi_device device = nullptr;
	status_t status = gDeviceManager->get_driver(parent,
		reinterpret_cast<driver_module_info**>(&acpi),
		reinterpret_cast<void**>(&device));
	gDeviceManager->put_node(parent);
	if (status != B_OK)
		return status;

	AcpiResources resources;
	status = acpi->walk_resources(device, const_cast<char*>("_CRS"),
		ReadResources, &resources);
	if (status != B_OK)
		return status;
	if (!resources.haveMemory || resources.length < kRegisterWindowSize)
		return B_DEV_RESOURCE_CONFLICT;
	if (!resources.haveIrq)
		return B_DEV_RESOURCE_CONFLICT;

	void* mappedRegisters = nullptr;
	fRegisterArea = map_physical_memory("Bay Trail GPIO registers",
		resources.base, kRegisterWindowSize, B_ANY_KERNEL_BLOCK_ADDRESS,
		B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA,
		&mappedRegisters);
	if (fRegisterArea < B_OK)
		return fRegisterArea;
	fRegisters = static_cast<volatile uint8*>(mappedRegisters);

	fIrq = resources.irq;
	fIrqFlags = resources.irqFlags;
	return B_OK;
}


status_t
BayTrailController::_InitializeHardware()
{
	MutexLocker locker(fLock);
	for (uint16 pin = 0; pin < PinCount(); pin++) {
		volatile uint32* config = _PadRegister(pin, kConfig0);
		if (config == nullptr)
			return B_BAD_VALUE;

		uint32 value = *config;
		if ((value & kDirectIrqEnable) != 0)
			continue;
		if ((value & kMuxMask) == GpioMux(fCommunity, pin)) {
			value &= ~kTriggerMask;
			*config = value;
		}
	}

	const uint32 wordCount = (PinCount() + 31) / 32;
	for (uint32 word = 0; word < wordCount; word++)
		*_StatusRegister(word) = UINT32_MAX;
	return B_OK;
}


status_t
BayTrailController::_InstallInterrupt()
{
	arch_int_configure_io_interrupt(fIrq, fIrqFlags);
	status_t status = install_io_interrupt_handler(fIrq, _InterruptEntry, this,
		0);
	if (status == B_OK)
		fInterruptInstalled = true;
	return status;
}


status_t
BayTrailController::_RegisterController()
{
	status_t status = gGpio->register_controller(gpio::FirmwareNode::Acpi(
		reinterpret_cast<uintptr_t>(fAcpiHandle)), this);
	if (status == B_OK)
		fRegistered = true;
	return status;
}


volatile uint32*
BayTrailController::_PadRegister(uint16 pin, size_t reg) const
{
	if (!_ValidPin(pin) || fRegisters == nullptr)
		return nullptr;
	const size_t offset = PadRegisterOffset(fCommunity, pin, reg);
	if (offset == SIZE_MAX || offset + sizeof(uint32) > kRegisterWindowSize)
		return nullptr;
	return reinterpret_cast<volatile uint32*>(fRegisters + offset);
}


volatile uint32*
BayTrailController::_StatusRegister(uint32 word) const
{
	const size_t offset = kInterruptStatus + word * sizeof(uint32);
	if (fRegisters == nullptr
		|| offset + sizeof(uint32) > kRegisterWindowSize) {
		return nullptr;
	}
	return reinterpret_cast<volatile uint32*>(fRegisters + offset);
}


bool
BayTrailController::_ValidPin(uint16 pin) const
{
	return pin < PinCount();
}


bool
BayTrailController::_Claimed(uint16 pin) const
{
	return _ValidPin(pin) && fSlots[pin].claimed;
}


status_t
BayTrailController::Claim(uint16 pin)
{
	if (!_ValidPin(pin))
		return B_BAD_VALUE;

	MutexLocker locker(fLock);
	if (fSlots[pin].claimed)
		return B_BUSY;
	fSlots[pin].claimed = true;
	return B_OK;
}


void
BayTrailController::Release(uint16 pin)
{
	if (!_ValidPin(pin))
		return;
	StopWatching(pin);

	MutexLocker locker(fLock);
	fSlots[pin].claimed = false;
}


status_t
BayTrailController::_ConfigureInputLocked(uint16 pin,
	const InputConfig& config)
{
	volatile uint32* config0 = _PadRegister(pin, kConfig0);
	volatile uint32* value = _PadRegister(pin, kValue);
	if (config0 == nullptr || value == nullptr)
		return B_BAD_VALUE;

	const uint32 pullStrength = PullStrengthBits(config.pullStrengthOhms);
	if (pullStrength == UINT32_MAX)
		return B_BAD_VALUE;
	const uint32 pullAssignment = PullAssignment(config.bias);

	uint32 configValue = *config0;
	configValue &= ~kMuxMask;
	configValue |= GpioMux(fCommunity, pin);
	configValue |= kGlitchFilters;
	if (pullAssignment != UINT32_MAX) {
		configValue &= ~(kPullAssignmentMask | kPullStrengthMask);
		configValue |= pullAssignment | pullStrength;
	}
	*config0 = configValue;

	uint32 pinValue = *value;
	pinValue &= ~kDirectionMask;
	pinValue |= kInputDirection;
	*value = pinValue;
	return B_OK;
}


status_t
BayTrailController::ConfigureInput(uint16 pin, const InputConfig& config)
{
	MutexLocker locker(fLock);
	if (!_Claimed(pin))
		return B_NOT_ALLOWED;
	return _ConfigureInputLocked(pin, config);
}


status_t
BayTrailController::ConfigureOutput(uint16 pin, const OutputConfig& config)
{
	MutexLocker locker(fLock);
	if (!_Claimed(pin))
		return B_NOT_ALLOWED;
	if (fSlots[pin].watching)
		return B_BUSY;

	volatile uint32* config0 = _PadRegister(pin, kConfig0);
	volatile uint32* value = _PadRegister(pin, kValue);
	if (config0 == nullptr || value == nullptr)
		return B_BAD_VALUE;
	if ((*config0 & kDirectIrqEnable) != 0)
		return B_NOT_ALLOWED;

	uint32 configValue = *config0;
	configValue &= ~(kMuxMask | kTriggerMask | kOpenDrain);
	configValue |= GpioMux(fCommunity, pin);
	if (config.openDrain)
		configValue |= kOpenDrain;
	*config0 = configValue;

	uint32 pinValue = *value;
	if (config.initialLevel == Level::High)
		pinValue |= kLevel;
	else
		pinValue &= ~kLevel;
	*value = pinValue;

	pinValue &= ~kDirectionMask;
	pinValue |= kOutputDirection;
	*value = pinValue;
	return B_OK;
}


status_t
BayTrailController::_ReadLevelLocked(uint16 pin, Level& level) const
{
	volatile uint32* value = _PadRegister(pin, kValue);
	if (value == nullptr)
		return B_BAD_VALUE;
	level = (*value & kLevel) != 0 ? Level::High : Level::Low;
	return B_OK;
}


status_t
BayTrailController::Read(uint16 pin, Level& level) const
{
	MutexLocker locker(fLock);
	if (!_Claimed(pin))
		return B_NOT_ALLOWED;
	return _ReadLevelLocked(pin, level);
}


status_t
BayTrailController::Write(uint16 pin, Level level)
{
	MutexLocker locker(fLock);
	if (!_Claimed(pin))
		return B_NOT_ALLOWED;

	volatile uint32* value = _PadRegister(pin, kValue);
	if (value == nullptr)
		return B_BAD_VALUE;
	if ((*value & kDirectionMask) != kOutputDirection)
		return B_NOT_ALLOWED;

	uint32 pinValue = *value;
	if (level == Level::High)
		pinValue |= kLevel;
	else
		pinValue &= ~kLevel;
	*value = pinValue;
	return B_OK;
}


void
BayTrailController::_ClearTriggerLocked(uint16 pin)
{
	volatile uint32* config = _PadRegister(pin, kConfig0);
	if (config == nullptr)
		return;
	uint32 value = *config;
	if ((value & kDirectIrqEnable) == 0) {
		value &= ~kTriggerMask;
		*config = value;
	}
}


void
BayTrailController::_AcknowledgeLocked(uint16 pin)
{
	volatile uint32* status = _StatusRegister(pin / 32);
	if (status != nullptr)
		*status = InterruptStatusBit(pin);
}


status_t
BayTrailController::Watch(uint16 pin, const InterruptConfig& config,
	EventHandler handler, void* context)
{
	if (handler == nullptr || config.debounce < 0
		|| config.debounce > kMaximumDebounce) {
		return B_BAD_VALUE;
	}

	MutexLocker locker(fLock);
	if (!_Claimed(pin))
		return B_NOT_ALLOWED;
	Slot& slot = fSlots[pin];
	if (slot.watching)
		return B_BUSY;

	volatile uint32* config0 = _PadRegister(pin, kConfig0);
	if (config0 == nullptr)
		return B_BAD_VALUE;
	if ((*config0 & kDirectIrqEnable) != 0)
		return B_NOT_SUPPORTED;

	status_t status = _ConfigureInputLocked(pin, {});
	if (status != B_OK)
		return status;

	Level level;
	status = _ReadLevelLocked(pin, level);
	if (status != B_OK)
		return status;

	_ClearTriggerLocked(pin);
	_AcknowledgeLocked(pin);

	slot.config = config;
	slot.handler = handler;
	slot.context = context;
	slot.lastLevel = level;
	slot.haveLevel = true;
	slot.pending = false;
	slot.watching = true;
	atomic_or(&fWatched[pin / 32],
		static_cast<int32>(InterruptStatusBit(pin)));

	uint32 value = *config0;
	value &= ~(kDirectIrqEnable | kTriggerMask);
	value |= TriggerBits(config.edge);
	*config0 = value;
	return B_OK;
}


void
BayTrailController::StopWatching(uint16 pin)
{
	if (!_ValidPin(pin))
		return;

	mutex_lock(&fLock);
	Slot& slot = fSlots[pin];
	if (!slot.watching) {
		mutex_unlock(&fLock);
		return;
	}

	_ClearTriggerLocked(pin);
	atomic_and(&fWatched[pin / 32],
		~static_cast<int32>(InterruptStatusBit(pin)));
	_AcknowledgeLocked(pin);
	slot.watching = false;
	slot.pending = false;
	slot.handler = nullptr;
	slot.context = nullptr;
	while (slot.dispatching > 0 && find_thread(nullptr) != fWorker)
		fDispatchIdle.Wait(&fLock);
	mutex_unlock(&fLock);
}


int32
BayTrailController::_InterruptEntry(void* context)
{
	return static_cast<BayTrailController*>(context)->_HandleInterrupt();
}


int32
BayTrailController::_HandleInterrupt()
{
	bool handled = false;
	const uint32 wordCount = (PinCount() + 31) / 32;
	for (uint32 word = 0; word < wordCount; word++) {
		volatile uint32* statusRegister = _StatusRegister(word);
		if (statusRegister == nullptr)
			continue;

		const uint32 watched = static_cast<uint32>(atomic_get(&fWatched[word]));
		const uint32 pending = *statusRegister & watched;
		if (pending == 0)
			continue;

		*statusRegister = pending;
		atomic_or(&fPending[word], static_cast<int32>(pending));
		handled = true;
	}

	if (!handled)
		return B_UNHANDLED_INTERRUPT;
	release_sem_etc(fEventSem, 1, B_DO_NOT_RESCHEDULE);
	return B_HANDLED_INTERRUPT;
}


int32
BayTrailController::_WorkerEntry(void* context)
{
	return static_cast<BayTrailController*>(context)->_WorkerLoop();
}


void
BayTrailController::_CollectPending()
{
	const bigtime_t now = system_time();
	const uint32 wordCount = (PinCount() + 31) / 32;

	MutexLocker locker(fLock);
	for (uint32 word = 0; word < wordCount; word++) {
		uint32 pending = static_cast<uint32>(
			atomic_get_and_set(&fPending[word], 0));
		while (pending != 0) {
			const uint32 bit = __builtin_ctz(pending);
			const uint16 pin = word * 32 + bit;
			pending &= pending - 1;
			if (!_ValidPin(pin))
				continue;

			Slot& slot = fSlots[pin];
			if (!slot.watching)
				continue;
			slot.pending = true;
			slot.due = now + slot.config.debounce;
		}
	}
}


void
BayTrailController::_DispatchDue()
{
	while (true) {
		EventHandler handler = nullptr;
		void* context = nullptr;
		Event event = { Level::Low, 0 };
		uint16 dispatchedPin = UINT16_MAX;

		mutex_lock(&fLock);
		const bigtime_t now = system_time();
		for (uint16 pin = 0; pin < PinCount(); pin++) {
			Slot& slot = fSlots[pin];
			if (!slot.watching || !slot.pending || slot.due > now)
				continue;

			slot.pending = false;
			Level level;
			if (_ReadLevelLocked(pin, level) != B_OK)
				continue;

			const bool matches = !slot.haveLevel
				|| EdgeMatches(slot.config.edge, slot.lastLevel, level);
			slot.lastLevel = level;
			slot.haveLevel = true;
			if (!matches || slot.handler == nullptr)
				continue;

			handler = slot.handler;
			context = slot.context;
			event = { level, now };
			dispatchedPin = pin;
			slot.dispatching++;
			break;
		}
		mutex_unlock(&fLock);

		if (handler == nullptr)
			return;
		handler(context, event);

		mutex_lock(&fLock);
		Slot& slot = fSlots[dispatchedPin];
		slot.dispatching--;
		if (slot.dispatching == 0)
			fDispatchIdle.NotifyAll();
		mutex_unlock(&fLock);
	}
}


bigtime_t
BayTrailController::_NextDeadline() const
{
	MutexLocker locker(fLock);
	bigtime_t deadline = B_INFINITE_TIMEOUT;
	for (uint16 pin = 0; pin < PinCount(); pin++) {
		const Slot& slot = fSlots[pin];
		if (slot.watching && slot.pending && slot.due < deadline)
			deadline = slot.due;
	}
	return deadline;
}


int32
BayTrailController::_WorkerLoop()
{
	while (true) {
		{
			MutexLocker locker(fLock);
			if (!fRunning)
				break;
		}

		_CollectPending();
		_DispatchDue();

		const bigtime_t deadline = _NextDeadline();
		if (deadline == B_INFINITE_TIMEOUT) {
			acquire_sem(fEventSem);
		} else {
			const bigtime_t delay = deadline > system_time()
				? deadline - system_time() : 0;
			acquire_sem_etc(fEventSem, 1, B_RELATIVE_TIMEOUT, delay);
		}
	}
	return B_OK;
}

} // namespace gpio::baytrail
