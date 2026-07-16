// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot
#pragma once

#include <common/GpioTypes.h>

#include <KernelExport.h>
#include <device_manager.h>

#include <stdint.h>


#define B_GPIO_MODULE_NAME "bus_managers/gpio/v1"


namespace gpio {

struct FirmwareNode {
	enum class Kind : uint8 {
		Acpi
	};

	Kind		kind;
	uintptr_t	value;

	static FirmwareNode Acpi(uintptr_t handle)
	{
		return { Kind::Acpi, handle };
	}

	bool operator==(const FirmwareNode& other) const
	{
		return kind == other.kind && value == other.value;
	}
};


struct ConnectionInfo {
	uint16	pin = 0;
	Bias	firmwareBias = Bias::Firmware;
	Access	access = Access::Preserve;
	bool	interruptResource = false;
	bool	activeLow = false;
	bool	wakeCapable = false;
};


struct InputConfig {
	Bias	bias = Bias::Firmware;
	uint16	pullStrengthOhms = 0;
};


struct OutputConfig {
	Level	initialLevel = Level::Low;
	bool	openDrain = false;
};


struct InterruptConfig {
	Edge		edge = Edge::Both;
	bigtime_t	debounce = 0;
};


struct Event {
	Level		level;
	bigtime_t	when;
};


using EventHandler = void (*)(void* context, const Event& event);


class Controller {
public:
	virtual ~Controller() = default;

	virtual uint32 PinCount() const = 0;
	virtual status_t Claim(uint16 pin) = 0;
	virtual void Release(uint16 pin) = 0;

	virtual status_t ConfigureInput(uint16 pin,
		const InputConfig& config) = 0;
	virtual status_t ConfigureOutput(uint16 pin,
		const OutputConfig& config) = 0;
	virtual status_t Read(uint16 pin, Level& level) const = 0;
	virtual status_t Write(uint16 pin, Level level) = 0;

	virtual status_t Watch(uint16 pin, const InterruptConfig& config,
		EventHandler handler, void* context) = 0;
	virtual void StopWatching(uint16 pin) = 0;
};


using pin_handle = void*;


struct module_info {
	::module_info	info;

	status_t (*register_controller)(FirmwareNode node, Controller* controller);
	void (*unregister_controller)(Controller* controller);

	status_t (*acquire_acpi)(device_node* consumer, uint32 resourceIndex,
		uint32 pinIndex, pin_handle* handle, ConnectionInfo* info);
	void (*release)(pin_handle handle);

	status_t (*configure_input)(pin_handle handle, const InputConfig* config);
	status_t (*configure_output)(pin_handle handle, const OutputConfig* config);
	status_t (*read)(pin_handle handle, Level* level);
	status_t (*write)(pin_handle handle, Level level);
	status_t (*watch)(pin_handle handle, const InterruptConfig* config,
		EventHandler handler, void* context);
	void (*stop_watching)(pin_handle handle);
};


class Pin {
public:
	Pin() = default;
	~Pin() { Reset(); }

	Pin(const Pin&) = delete;
	Pin& operator=(const Pin&) = delete;

	Pin(Pin&& other) noexcept
		:
		fModule(other.fModule),
		fHandle(other.fHandle),
		fInfo(other.fInfo)
	{
		other.fModule = nullptr;
		other.fHandle = nullptr;
	}

	Pin& operator=(Pin&& other) noexcept
	{
		if (this != &other) {
			Reset();
			fModule = other.fModule;
			fHandle = other.fHandle;
			fInfo = other.fInfo;
			other.fModule = nullptr;
			other.fHandle = nullptr;
		}
		return *this;
	}

	status_t AcquireAcpi(module_info* module, device_node* consumer,
		uint32 resourceIndex, uint32 pinIndex = 0)
	{
		if (module == nullptr || consumer == nullptr)
			return B_BAD_VALUE;
		Reset();

		pin_handle handle = nullptr;
		ConnectionInfo info;
		status_t status = module->acquire_acpi(consumer, resourceIndex,
			pinIndex, &handle, &info);
		if (status != B_OK)
			return status;

		fModule = module;
		fHandle = handle;
		fInfo = info;
		return B_OK;
	}

	void Reset()
	{
		if (fModule != nullptr && fHandle != nullptr)
			fModule->release(fHandle);
		fModule = nullptr;
		fHandle = nullptr;
		fInfo = {};
	}

	bool IsValid() const { return fHandle != nullptr; }
	const ConnectionInfo& Connection() const { return fInfo; }

	status_t ConfigureInput(const InputConfig& config = {})
	{
		return IsValid()
			? fModule->configure_input(fHandle, &config) : B_NO_INIT;
	}

	status_t ConfigureOutput(const OutputConfig& config)
	{
		return IsValid()
			? fModule->configure_output(fHandle, &config) : B_NO_INIT;
	}

	status_t Read(Level& level) const
	{
		return IsValid() ? fModule->read(fHandle, &level) : B_NO_INIT;
	}

	status_t Write(Level level)
	{
		return IsValid() ? fModule->write(fHandle, level) : B_NO_INIT;
	}

	status_t Watch(const InterruptConfig& config, EventHandler handler,
		void* context)
	{
		return IsValid()
			? fModule->watch(fHandle, &config, handler, context) : B_NO_INIT;
	}

	void StopWatching()
	{
		if (IsValid())
			fModule->stop_watching(fHandle);
	}

private:
	module_info*	fModule = nullptr;
	pin_handle	fHandle = nullptr;
	ConnectionInfo	fInfo;
};

} // namespace gpio
