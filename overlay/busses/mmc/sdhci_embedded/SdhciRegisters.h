// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot
#pragma once

#include <stddef.h>
#include <stdint.h>

// The SDHCI standard register block (spec v3.00/v4.10) expressed as clean,
// net-new accessor types. The field offsets are hardware-defined; the accessor
// methods are ours. Compile-time offset guards catch layout drift.
//
// Only the fields the engine actually drives carry rich accessors; the rest
// keep the block's offsets correct via typed padding.

namespace jr::sdhci {


// ---- 0x04 Block Size ------------------------------------------------------
class BlockSizeReg {
public:
	// transferBlockSize in bytes; dmaBoundary selects the SDMA buffer-boundary
	// (host DMA boundary field, bits 14:12).
	void Configure(uint16_t transferBlockSize, uint16_t dmaBoundary) volatile
	{
		fBits = static_cast<uint16_t>(transferBlockSize | (dmaBoundary << 12));
	}
	uint16_t Get() const volatile { return fBits; }

	static constexpr uint16_t kBoundary4K = 0;
	static constexpr uint16_t kBoundary512K = 7;

private:
	volatile uint16_t fBits;
} __attribute__((packed));


// ---- 0x0C Transfer Mode ---------------------------------------------------
class TransferModeReg {
public:
	void Set(uint16_t bits) volatile { fBits = bits; }
	uint16_t Get() const volatile { return fBits; }

private:
	volatile uint16_t fBits;
} __attribute__((packed));


// ---- 0x0E Command ---------------------------------------------------------
class CommandReg {
public:
	// Compose the command register: index in bits 13:8, flags in bits 5:0.
	void Send(uint8_t index, uint8_t flags) volatile
	{
		fBits = static_cast<uint16_t>((index << 8) | flags);
	}
	uint16_t Get() const volatile { return fBits; }

	// Command flags (bits 5:0).
	static constexpr uint8_t kDataPresent		= 0x20;
	static constexpr uint8_t kCheckIndex		= 0x10;
	static constexpr uint8_t kCheckCrc			= 0x08;
	static constexpr uint8_t kResp136			= 0x01;	// 136-bit response
	static constexpr uint8_t kResp48			= 0x02;	// 48-bit response
	static constexpr uint8_t kResp48Busy		= 0x03;	// 48-bit w/ busy

private:
	volatile uint16_t fBits;
} __attribute__((packed));


// ---- 0x24 Present State ---------------------------------------------------
class PresentStateReg {
public:
	bool CommandInhibit() const volatile { return (fBits & (1u << 0)) != 0; }
	bool DataInhibit() const volatile { return (fBits & (1u << 1)) != 0; }
	bool CardInserted() const volatile { return (fBits & (1u << 16)) != 0; }
	uint32_t Raw() const volatile { return fBits; }
	// Host Regulator Voltage Stable is defined for SDHCI 4.10+ at bit 25.
	bool RegulatorStable() const volatile { return (fBits & (1u << 25)) != 0; }
	uint8_t DataLineLevels() const volatile
	{
		return static_cast<uint8_t>((fBits >> 20) & 0xf);
	}

private:
	volatile uint32_t fBits;
} __attribute__((packed));


// ---- 0x28 Host Control 1 --------------------------------------------------
class HostControlReg {
public:
	void SetDmaMode(uint8_t mode) volatile
	{
		fBits = static_cast<uint8_t>((fBits & ~kDmaMask) | mode);
	}
	void SetBusWidth(uint8_t width) volatile
	{
		fBits = static_cast<uint8_t>((fBits & ~kWidthMask) | width);
	}
	void SetHighSpeed(bool on) volatile
	{
		fBits = static_cast<uint8_t>(on ? (fBits | (1u << 2)) : (fBits & ~(1u << 2)));
	}
	uint8_t Get() const volatile { return fBits; }

	static constexpr uint8_t kDmaMask	= 3u << 3;
	static constexpr uint8_t kSdma		= 0u << 3;
	static constexpr uint8_t kAdma32	= 2u << 3;

	static constexpr uint8_t kWidth1	= 0;
	static constexpr uint8_t kWidth4	= 1u << 1;
	static constexpr uint8_t kWidth8	= 1u << 5;
	static constexpr uint8_t kWidthMask	= kWidth4 | kWidth8;

private:
	volatile uint8_t fBits;
} __attribute__((packed));


// ---- 0x29 Power Control ---------------------------------------------------
class PowerControlReg {
public:
	void PowerOn(uint8_t voltage) volatile
	{
		fBits = static_cast<uint8_t>(voltage | kBusPower);
	}
	void PowerOff() volatile { fBits = static_cast<uint8_t>(fBits & ~kBusPower); }
	uint8_t Get() const volatile { return fBits; }

	void AssertEmmcReset() volatile { fBits = static_cast<uint8_t>(fBits | kEmmcReset); }
	void DeassertEmmcReset() volatile { fBits = static_cast<uint8_t>(fBits & ~kEmmcReset); }

	static constexpr uint8_t k3v3 = 7u << 1;
	static constexpr uint8_t k3v0 = 6u << 1;
	static constexpr uint8_t k1v8 = 5u << 1;

private:
	static constexpr uint8_t kBusPower	= 1u << 0;
	static constexpr uint8_t kEmmcReset	= 1u << 4;

	volatile uint8_t fBits;
} __attribute__((packed));


// ---- 0x2C Clock Control ---------------------------------------------------
class ClockControlReg {
public:
	// Program the 10-bit divider (SDCLK Frequency Select, bits 15:8 + 7:6).
	// Returns the effective divide factor applied.
	uint16_t SetDivider(uint16_t divider) volatile
	{
		uint16_t encoded = (divider <= 1) ? 0 : static_cast<uint16_t>(divider / 2);
		uint16_t bits = static_cast<uint16_t>(fBits & ~0xffc0);
		bits = static_cast<uint16_t>(bits | ((encoded & 0xff) << 8)
			| ((encoded & 0x300) >> 2));
		fBits = bits;
		return encoded == 0 ? 1 : static_cast<uint16_t>(encoded * 2);
	}

	void EnableInternal() volatile { fBits = static_cast<uint16_t>(fBits | (1u << 0)); }
	bool InternalStable() const volatile { return (fBits & (1u << 1)) != 0; }
	void EnableSdClock() volatile { fBits = static_cast<uint16_t>(fBits | (1u << 2)); }
	void DisableSdClock() volatile { fBits = static_cast<uint16_t>(fBits & ~(1u << 2)); }
	// PLL Enable (bit 3, SDHCI 4.10+). Bay Trail's programming sequence brings
	// the internal clock up, waits for stability, enables the PLL, waits again,
	// then gates SDCLK on -- skipping the PLL step leaves the card at an
	// unstable clock on this silicon.
	void EnablePll() volatile { fBits = static_cast<uint16_t>(fBits | (1u << 3)); }
	uint16_t Get() const volatile { return fBits; }

private:
	volatile uint16_t fBits;
} __attribute__((packed));


// ---- 0x2E Timeout Control -------------------------------------------------
class TimeoutControlReg {
public:
	// Pick the smallest data-timeout counter value (DTOCV) whose period covers
	// delay_ms at base_khz. Field is bits 3:0.
	void SetForDelay(uint32_t base_khz, uint32_t delay_ms) volatile
	{
		uint32_t i = 13;
		for (; i < 27; i++) {
			if (static_cast<uint64_t>(base_khz) * delay_ms <= (1ull << i))
				break;
		}
		fBits = static_cast<uint8_t>(i - 13);
	}
	void SetRaw(uint8_t divider) volatile { fBits = divider; }
	uint8_t Get() const volatile { return fBits; }

private:
	volatile uint8_t fBits;
} __attribute__((packed));


// ---- 0x2F Software Reset --------------------------------------------------
class SoftwareResetReg {
public:
	// Full reset (bit 0). Returns true if it cleared within the timeout.
	bool ResetAll() volatile
	{
		fBits = 1;
		for (int i = 0; i < 10; i++) {
			if ((fBits & 1u) == 0)
				return true;
			BusyWait10ms();
		}
		return false;
	}
	bool ResetCommandLine() volatile { return _ResetAndWait(1u << 1); }
	bool ResetDataLine() volatile { return _ResetAndWait(1u << 2); }
	bool ResetCommandAndDataLines() volatile
	{
		return _ResetAndWait((1u << 1) | (1u << 2));
	}

private:
	bool _ResetAndWait(uint8_t bit) volatile
	{
		fBits = static_cast<uint8_t>(fBits | bit);
		for (int i = 0; i < 10; i++) {
			if ((fBits & bit) == 0)
				return true;
			BusyWait10ms();
		}
		return false;
	}

	static void BusyWait10ms();		// defined in the engine TU (uses snooze)
	volatile uint8_t fBits;
} __attribute__((packed));


// ---- 0x3E Host Control 2 --------------------------------------------------
class HostControl2Reg {
public:
	uint16_t Get() const volatile { return fBits; }
	void Set(uint16_t bits) volatile { fBits = bits; }

	void SetUhsMode(uint16_t mode) volatile
	{
		fBits = static_cast<uint16_t>((fBits & ~kUhsMask) | (mode & kUhsMask));
	}
	void SetSignal1v8(bool enabled) volatile
	{
		fBits = static_cast<uint16_t>(enabled
			? (fBits | kSignal1v8) : (fBits & ~kSignal1v8));
	}
	void SetExecuteTuning(bool enabled) volatile
	{
		fBits = static_cast<uint16_t>(enabled
			? (fBits | kExecuteTuning) : (fBits & ~kExecuteTuning));
	}
	void ClearTuning() volatile
	{
		fBits = static_cast<uint16_t>(fBits & ~(kExecuteTuning | kTunedClock));
	}
	bool ExecuteTuning() const volatile { return (fBits & kExecuteTuning) != 0; }
	bool TunedClock() const volatile { return (fBits & kTunedClock) != 0; }
	void DisablePresetValues() volatile
	{
		fBits = static_cast<uint16_t>(fBits & ~kPresetValues);
	}
	void ClearReservedV4Bit() volatile
	{
		fBits = static_cast<uint16_t>(fBits & ~(1u << 12));
	}

	static constexpr uint16_t kUhsMask = 0x0007;
	static constexpr uint16_t kSdr12 = 0;
	static constexpr uint16_t kSdr25 = 1;
	static constexpr uint16_t kSdr50 = 2;
	static constexpr uint16_t kSdr104 = 3;
	static constexpr uint16_t kDdr50 = 4;

private:
	static constexpr uint16_t kSignal1v8 = 1u << 3;
	static constexpr uint16_t kExecuteTuning = 1u << 6;
	static constexpr uint16_t kTunedClock = 1u << 7;
	static constexpr uint16_t kPresetValues = 1u << 15;
	volatile uint16_t fBits;
} __attribute__((packed));


// ---- 0x40 Capabilities ----------------------------------------------------
class CapabilitiesReg {
public:
	uint32_t Raw() const volatile { return fBits; }
	uint32_t Raw1() const volatile { return fBits1; }
	uint8_t BaseClockMHz() const volatile { return (fBits >> 8) & 0xff; }
	bool SupportsAdma2() const volatile { return (fBits >> 19) & 1; }
	bool SupportsHighSpeed() const volatile { return (fBits >> 21) & 1; }
	uint32_t TimeoutClockKHz() const volatile
	{
		const bool mhz = (fBits >> 7) & 1;
		const uint32_t freq = fBits & 0x3f;
		return mhz ? freq * 1000 : freq;
	}

	// Supported bus-voltage support bits (24=3.3V, 25=3.0V, 26=1.8V).
	bool Supports3v3() const volatile { return (fBits >> 24) & 1; }
	bool Supports3v0() const volatile { return (fBits >> 25) & 1; }
	bool Supports1v8() const volatile { return (fBits >> 26) & 1; }

private:
	// These are two independently specified 32-bit MMIO registers. Keeping them
	// split prevents the compiler from issuing one 64-bit device access.
	volatile uint32_t fBits;
	volatile uint32_t fBits1;
} __attribute__((packed));


// ---- Full register block --------------------------------------------------
// Layout follows the SDHCI standard; typed padding keeps offsets exact through
// the UHS-II region we don't use.
struct RegisterBlock {
	volatile uint32_t	systemAddress;			// 0x00 SDMA address / ADMA use
	BlockSizeReg		blockSize;				// 0x04
	volatile uint16_t	blockCount;				// 0x06
	volatile uint32_t	argument;				// 0x08
	TransferModeReg		transferMode;			// 0x0C
	CommandReg			command;				// 0x0E
	volatile uint32_t	response[4];			// 0x10
	volatile uint32_t	bufferDataPort;			// 0x20
	PresentStateReg		presentState;			// 0x24
	HostControlReg		hostControl;			// 0x28
	PowerControlReg		powerControl;			// 0x29
	volatile uint8_t	blockGapControl;		// 0x2A
	volatile uint8_t	wakeupControl;			// 0x2B
	ClockControlReg		clockControl;			// 0x2C
	TimeoutControlReg	timeoutControl;			// 0x2E
	SoftwareResetReg	softwareReset;			// 0x2F
	volatile uint32_t	interruptStatus;		// 0x30
	volatile uint32_t	interruptStatusEnable;	// 0x34
	volatile uint32_t	interruptSignalEnable;	// 0x38
	volatile uint16_t	autoCmd12ErrorStatus;	// 0x3C
	HostControl2Reg		hostControl2;			// 0x3E
	CapabilitiesReg		capabilities;			// 0x40
	volatile uint64_t	maxCurrentCapabilities;	// 0x48
	volatile uint16_t	forceEventAcmdStatus;	// 0x50
	volatile uint16_t	forceEventErrorStatus;	// 0x52
	volatile uint8_t	admaErrorStatus;		// 0x54
	volatile uint8_t	padding0[3];			// 0x55
	volatile uint32_t	admaSystemAddress;		// 0x58, 32-bit ADMA address
	volatile uint32_t	admaSystemAddressHigh;	// 0x5C, only for 64-bit ADMA
	volatile uint64_t	presetValue[2];			// 0x60
	volatile uint8_t	padding1[0x9C - 0x70];	// 0x70..0x9B reserved/UHS-II
	volatile uint8_t	padding2[0xFC - 0x9C];	// 0x9C..0xFB reserved/UHS-II
	volatile uint16_t	slotInterruptStatus;	// 0xFC
	volatile uint16_t	hostControllerVersion;	// 0xFE
} __attribute__((packed));


static_assert(offsetof(RegisterBlock, presentState) == 0x24, "present state offset");
static_assert(offsetof(RegisterBlock, interruptStatus) == 0x30, "int status offset");
static_assert(offsetof(RegisterBlock, hostControl2) == 0x3E, "host control 2 offset");
static_assert(offsetof(RegisterBlock, capabilities) == 0x40, "capabilities offset");
static_assert(offsetof(RegisterBlock, admaSystemAddress) == 0x58, "adma addr offset");
static_assert(offsetof(RegisterBlock, admaSystemAddressHigh) == 0x5C,
	"adma high addr offset");
static_assert(offsetof(RegisterBlock, slotInterruptStatus) == 0xFC, "slot int offset");
static_assert(sizeof(RegisterBlock) == 0x100, "register block is 256 bytes");


} // namespace jr::sdhci
