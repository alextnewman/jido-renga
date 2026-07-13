// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot
#pragma once

#include <stdint.h>

namespace jr::sdhci {


enum class BusTiming : uint8_t {
	Legacy,
	SdHighSpeed,
	MmcHighSpeed,
	MmcDdr52,
	MmcHs200,
	SdSdr12,
	SdSdr25,
	SdSdr50,
	SdSdr104,
	SdDdr50,
};


struct HostCapabilities {
	uint32_t	baseClockKHz = 0;
	bool		adma2 = false;
	bool		highSpeed = false;
	bool		width8 = false;
	bool		voltage3v3 = false;
	bool		voltage3v0 = false;
	bool		voltage1v8 = false;
	bool		sdr50 = false;
	bool		sdr104 = false;
	bool		ddr50 = false;
	bool		tuneSdr50 = false;
	uint8_t		platformUhsMask = 0;
};


struct ClockSetting {
	uint16_t	divider = 1;
	uint32_t	actualKHz = 0;
};


constexpr ClockSetting
ComputeClockSetting(uint32_t baseKHz, uint32_t targetKHz) noexcept
{
	if (baseKHz == 0 || targetKHz == 0)
		return {};
	uint32_t divider = (baseKHz + targetKHz - 1) / targetKHz;
	if (divider > 1 && (divider & 1) != 0)
		divider++;
	if (divider > 2046)
		divider = 2046;
	return {static_cast<uint16_t>(divider), baseKHz / divider};
}


constexpr HostCapabilities
DecodeHostCapabilities(uint32_t caps, uint32_t caps1,
	uint8_t platformUhsMask) noexcept
{
	HostCapabilities out;
	out.baseClockKHz = ((caps >> 8) & 0xff) * 1000;
	out.adma2 = (caps & (1u << 19)) != 0;
	out.highSpeed = (caps & (1u << 21)) != 0;
	out.width8 = (caps & (1u << 18)) != 0;
	out.voltage3v3 = (caps & (1u << 24)) != 0;
	out.voltage3v0 = (caps & (1u << 25)) != 0;
	out.voltage1v8 = (caps & (1u << 26)) != 0;
	out.sdr50 = (caps1 & (1u << 0)) != 0;
	out.sdr104 = (caps1 & (1u << 1)) != 0;
	out.ddr50 = (caps1 & (1u << 2)) != 0;
	out.tuneSdr50 = (caps1 & (1u << 13)) != 0;
	out.platformUhsMask = platformUhsMask;
	return out;
}


constexpr uint32_t
MmcHostOcrMask(const HostCapabilities& host) noexcept
{
	uint32_t mask = 0;
	if (host.voltage1v8)
		mask |= 1u << 7;			// 1.65-1.95 V
	if (host.voltage3v0 || host.voltage3v3)
		mask |= 0x00ff8000u;		// 2.7-3.6 V
	return mask;
}


constexpr uint32_t
SdHostOcrMask(const HostCapabilities& host) noexcept
{
	return (host.voltage3v0 || host.voltage3v3) ? 0x00ff8000u : 0;
}


constexpr uint32_t
SelectOcrWindow(uint32_t cardOcr, uint32_t hostMask) noexcept
{
	return cardOcr & hostMask & 0x00ffff80u;
}


constexpr uint32_t
BuildMmcOcrArgument(uint32_t selectedWindow, bool sectorMode = true) noexcept
{
	return selectedWindow | (sectorMode ? (1u << 30) : 0);
}


struct EmmcExtCsd {
	uint8_t		revision = 0;
	uint8_t		cardType = 0;
	uint32_t	sectorCount = 0;
	uint32_t	cacheSizeKiB = 0;
	uint8_t		powerClassHs26_1v8 = 0;
	uint8_t		powerClassHs52_1v8 = 0;
	uint8_t		powerClassHs200_1v8 = 0;
	uint8_t		powerClassDdr52_1v8 = 0;
	uint8_t		genericCmd6Time10Ms = 0;
};


constexpr uint32_t
LoadLe32(const uint8_t* p) noexcept
{
	return static_cast<uint32_t>(p[0])
		| (static_cast<uint32_t>(p[1]) << 8)
		| (static_cast<uint32_t>(p[2]) << 16)
		| (static_cast<uint32_t>(p[3]) << 24);
}


constexpr EmmcExtCsd
DecodeExtCsd(const uint8_t ext[512]) noexcept
{
	EmmcExtCsd out;
	out.revision = ext[192];
	out.cardType = ext[196];
	out.sectorCount = LoadLe32(ext + 212);
	out.cacheSizeKiB = LoadLe32(ext + 249);
	out.powerClassHs26_1v8 = ext[201];
	out.powerClassHs52_1v8 = ext[200];
	out.powerClassHs200_1v8 = ext[236];
	out.powerClassDdr52_1v8 = ext[238];
	out.genericCmd6Time10Ms = ext[248];
	return out;
}


enum class EmmcMode : uint8_t {
	Hs200,
	Ddr52,
	Hs52,
	Hs26,
	Legacy,
};


struct BusMode {
	BusTiming	timing = BusTiming::Legacy;
	uint32_t	clockKHz = 400;
	uint8_t		width = 1;
	bool		signal1v8 = false;
	bool		tuning = false;
};


constexpr BusMode
Describe(EmmcMode mode) noexcept
{
	switch (mode) {
		case EmmcMode::Hs200:
			return {BusTiming::MmcHs200, 200000, 8, true, true};
		case EmmcMode::Ddr52:
			return {BusTiming::MmcDdr52, 52000, 8, true, false};
		case EmmcMode::Hs52:
			return {BusTiming::MmcHighSpeed, 52000, 8, true, false};
		case EmmcMode::Hs26:
			return {BusTiming::MmcHighSpeed, 26000, 8, true, false};
		case EmmcMode::Legacy:
		default:
			return {BusTiming::Legacy, 20000, 1, true, false};
	}
}


constexpr bool
Supports(const EmmcExtCsd& card, const HostCapabilities& host,
	EmmcMode mode) noexcept
{
	switch (mode) {
		case EmmcMode::Hs200:
			return (card.cardType & (1u << 4)) != 0 && host.sdr104
				&& host.width8 && host.voltage1v8;
		case EmmcMode::Ddr52:
			return (card.cardType & (1u << 2)) != 0 && host.ddr50
				&& host.width8 && host.voltage1v8;
		case EmmcMode::Hs52:
			return (card.cardType & (1u << 1)) != 0 && host.highSpeed
				&& host.width8;
		case EmmcMode::Hs26:
			return (card.cardType & (1u << 0)) != 0 && host.highSpeed
				&& host.width8;
		case EmmcMode::Legacy:
			return true;
	}
	return false;
}


constexpr uint8_t
PowerClass8Bit(const EmmcExtCsd& card, EmmcMode mode) noexcept
{
	uint8_t packed = 0;
	switch (mode) {
		case EmmcMode::Hs200:
			packed = card.powerClassHs200_1v8;
			break;
		case EmmcMode::Ddr52:
			packed = card.powerClassDdr52_1v8;
			break;
		case EmmcMode::Hs52:
			packed = card.powerClassHs52_1v8;
			break;
		case EmmcMode::Hs26:
			packed = card.powerClassHs26_1v8;
			break;
		case EmmcMode::Legacy:
			break;
	}
	return static_cast<uint8_t>((packed >> 4) & 0xf);
}


constexpr uint32_t
BuildMmcSwitchArgument(uint8_t index, uint8_t value) noexcept
{
	return (3u << 24) | (static_cast<uint32_t>(index) << 16)
		| (static_cast<uint32_t>(value) << 8);
}


struct SdScr {
	bool	valid = false;
	uint8_t	spec = 0;
	uint8_t	busWidths = 0;
	bool	spec3 = false;
};


constexpr uint64_t
LoadBe64(const uint8_t* p) noexcept
{
	uint64_t value = 0;
	for (int i = 0; i < 8; i++)
		value = (value << 8) | p[i];
	return value;
}


constexpr SdScr
DecodeScr(const uint8_t bytes[8]) noexcept
{
	const uint64_t raw = LoadBe64(bytes);
	SdScr out;
	out.valid = ((raw >> 60) & 0xf) == 0;
	out.spec = static_cast<uint8_t>((raw >> 56) & 0xf);
	out.busWidths = static_cast<uint8_t>((raw >> 48) & 0xf);
	out.spec3 = out.spec == 2 && ((raw >> 47) & 1) != 0;
	return out;
}


enum class SdMode : uint8_t {
	Sdr104 = 3,
	Ddr50 = 4,
	Sdr50 = 2,
	Sdr25 = 1,
	Sdr12 = 0,
	HighSpeed = 5,
	Default = 6,
};


constexpr BusMode
Describe(SdMode mode) noexcept
{
	switch (mode) {
		case SdMode::Sdr104:
			return {BusTiming::SdSdr104, 208000, 4, true, true};
		case SdMode::Ddr50:
			return {BusTiming::SdDdr50, 50000, 4, true, false};
		case SdMode::Sdr50:
			return {BusTiming::SdSdr50, 100000, 4, true, true};
		case SdMode::Sdr25:
			return {BusTiming::SdSdr25, 50000, 4, true, false};
		case SdMode::Sdr12:
			return {BusTiming::SdSdr12, 25000, 4, true, false};
		case SdMode::HighSpeed:
			return {BusTiming::SdHighSpeed, 50000, 4, false, false};
		case SdMode::Default:
		default:
			return {BusTiming::Legacy, 25000, 4, false, false};
	}
}


constexpr uint8_t
SdFunction(SdMode mode) noexcept
{
	switch (mode) {
		case SdMode::Sdr104:	return 3;
		case SdMode::Ddr50:	return 4;
		case SdMode::Sdr50:	return 2;
		case SdMode::Sdr25:	return 1;
		case SdMode::Sdr12:	return 0;
		case SdMode::HighSpeed: return 1;
		case SdMode::Default: return 0;
	}
	return 0;
}


constexpr bool
SdSwitchSupports(const uint8_t status[64], uint8_t function) noexcept
{
	return function < 8 && (status[13] & (1u << function)) != 0;
}


constexpr bool
SdSwitchSelected(const uint8_t status[64], uint8_t function) noexcept
{
	return (status[16] & 0xf) == function;
}


constexpr bool
Supports(const uint8_t switchStatus[64], const HostCapabilities& host,
	SdMode mode, bool at1v8) noexcept
{
	const uint8_t fn = SdFunction(mode);
	switch (mode) {
		case SdMode::Sdr104:
			return at1v8 && host.sdr104
				&& (host.platformUhsMask & (1u << 3)) != 0
				&& SdSwitchSupports(switchStatus, fn);
		case SdMode::Ddr50:
			return at1v8 && host.ddr50
				&& (host.platformUhsMask & (1u << 1)) != 0
				&& SdSwitchSupports(switchStatus, fn);
		case SdMode::Sdr50:
			return at1v8 && (host.sdr50 || host.sdr104)
				&& (host.platformUhsMask & (1u << 2)) != 0
				&& SdSwitchSupports(switchStatus, fn);
		case SdMode::Sdr25:
			return at1v8 && (host.platformUhsMask & (1u << 0)) != 0
				&& SdSwitchSupports(switchStatus, fn);
		case SdMode::Sdr12:
			return at1v8 && SdSwitchSupports(switchStatus, fn);
		case SdMode::HighSpeed:
			return host.highSpeed && SdSwitchSupports(switchStatus, fn);
		case SdMode::Default:
			return true;
	}
	return false;
}


constexpr bool
RequiresTuning(SdMode mode, const HostCapabilities& host) noexcept
{
	return mode == SdMode::Sdr104
		|| (mode == SdMode::Sdr50 && host.tuneSdr50);
}


constexpr uint32_t
BuildSdSwitchArgument(bool set, uint8_t group, uint8_t value) noexcept
{
	uint32_t argument = (set ? (1u << 31) : 0) | 0x00ffffffu;
	argument &= ~(0xfu << (group * 4));
	argument |= static_cast<uint32_t>(value & 0xf) << (group * 4);
	return argument;
}


} // namespace jr::sdhci
