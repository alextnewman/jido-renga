// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot
#pragma once

#include <common/GpioTypes.h>

#include <stddef.h>
#include <stdint.h>


namespace gpio::baytrail {

enum class Community : uint8 {
	Score,
	Ncore,
	Sus
};


constexpr size_t kRegisterWindowSize = 0x1000;
constexpr size_t kPadStride = 0x10;
constexpr size_t kConfig0 = 0x000;
constexpr size_t kConfig1 = 0x004;
constexpr size_t kValue = 0x008;
constexpr size_t kInterruptStatus = 0x800;
constexpr size_t kDirectIrqMap = 0x980;

constexpr uint32 kOpenDrain = uint32{1} << 31;
constexpr uint32 kDirectIrqEnable = uint32{1} << 27;
constexpr uint32 kTriggerNegative = uint32{1} << 26;
constexpr uint32 kTriggerPositive = uint32{1} << 25;
constexpr uint32 kTriggerLevel = uint32{1} << 24;
constexpr uint32 kTriggerMask
	= kTriggerNegative | kTriggerPositive | kTriggerLevel;
constexpr uint32 kGlitchFilterEnable = uint32{1} << 19;
constexpr uint32 kSlowGlitchFilter = uint32{1} << 17;
constexpr uint32 kFastGlitchFilter = uint32{1} << 16;
constexpr uint32 kPullStrengthMask = uint32{3} << 9;
constexpr uint32 kPullAssignmentMask = uint32{3} << 7;
constexpr uint32 kPullUp = uint32{1} << 7;
constexpr uint32 kPullDown = uint32{2} << 7;
constexpr uint32 kMuxMask = 0x7;

constexpr uint32 kInputDisable = uint32{1} << 2;
constexpr uint32 kOutputDisable = uint32{1} << 1;
constexpr uint32 kLevel = uint32{1} << 0;
constexpr uint32 kDirectionMask = kInputDisable | kOutputDisable;
constexpr uint32 kInputDirection = kOutputDisable;
constexpr uint32 kOutputDirection = kInputDisable;


inline constexpr uint8 kScorePadMap[] = {
	85, 89, 93, 96, 99, 102, 98, 101, 34, 37, 36, 38, 39, 35, 40, 84,
	62, 61, 64, 59, 54, 56, 60, 55, 63, 57, 51, 50, 53, 47, 52, 49,
	48, 43, 46, 41, 45, 42, 58, 44, 95, 105, 70, 68, 67, 66, 69, 71,
	65, 72, 86, 90, 88, 92, 103, 77, 79, 83, 78, 81, 80, 82, 13, 12,
	15, 14, 17, 18, 19, 16, 2, 1, 0, 4, 6, 7, 9, 8, 33, 32,
	31, 30, 29, 27, 25, 28, 26, 23, 21, 20, 24, 22, 5, 3, 10, 11,
	106, 87, 91, 104, 97, 100
};

inline constexpr uint8 kNcorePadMap[] = {
	19, 18, 17, 20, 21, 22, 24, 25, 23, 16, 14, 15, 12, 26,
	27, 1, 4, 8, 11, 0, 3, 6, 10, 13, 2, 5, 9, 7
};

inline constexpr uint8 kSusPadMap[] = {
	29, 33, 30, 31, 32, 34, 36, 35, 38, 37, 18, 7, 11, 20, 17, 1,
	8, 10, 19, 12, 0, 2, 23, 39, 28, 27, 22, 21, 24, 25, 26, 51,
	56, 54, 49, 55, 48, 57, 50, 58, 52, 53, 59, 40
};


constexpr uint32
PinCount(Community community)
{
	switch (community) {
		case Community::Score:
			return sizeof(kScorePadMap);
		case Community::Ncore:
			return sizeof(kNcorePadMap);
		case Community::Sus:
			return sizeof(kSusPadMap);
	}
	return 0;
}


constexpr uint8
PadForPin(Community community, uint16 pin)
{
	switch (community) {
		case Community::Score:
			return pin < sizeof(kScorePadMap) ? kScorePadMap[pin] : 0xff;
		case Community::Ncore:
			return pin < sizeof(kNcorePadMap) ? kNcorePadMap[pin] : 0xff;
		case Community::Sus:
			return pin < sizeof(kSusPadMap) ? kSusPadMap[pin] : 0xff;
	}
	return 0xff;
}


constexpr size_t
PadRegisterOffset(Community community, uint16 pin, size_t reg)
{
	const uint8 pad = PadForPin(community, pin);
	return pad == 0xff ? SIZE_MAX : size_t{pad} * kPadStride + reg;
}


constexpr size_t
InterruptStatusOffset(uint16 pin)
{
	return kInterruptStatus + (pin / 32) * sizeof(uint32);
}


constexpr uint32
InterruptStatusBit(uint16 pin)
{
	return uint32{1} << (pin % 32);
}


constexpr uint32
GpioMux(Community community, uint16 pin)
{
	if (community == Community::Score && pin >= 92 && pin <= 93)
		return 1;
	if (community == Community::Sus && pin >= 11 && pin <= 21)
		return 1;
	return 0;
}


constexpr uint32
TriggerBits(Edge edge)
{
	switch (edge) {
		case Edge::Rising:
			return kTriggerPositive;
		case Edge::Falling:
			return kTriggerNegative;
		case Edge::Both:
			return kTriggerPositive | kTriggerNegative;
	}
	return 0;
}


constexpr bool
EdgeMatches(Edge edge, Level before, Level after)
{
	if (before == after)
		return false;
	if (before == Level::Low && after == Level::High)
		return edge == Edge::Rising || edge == Edge::Both;
	return edge == Edge::Falling || edge == Edge::Both;
}


constexpr uint32
PullStrengthBits(uint16 ohms)
{
	switch (ohms) {
		case 0:
		case 2000:
			return 0;
		case 10000:
			return uint32{1} << 9;
		case 20000:
			return uint32{2} << 9;
		case 40000:
			return uint32{3} << 9;
		default:
			return UINT32_MAX;
	}
}

} // namespace gpio::baytrail
