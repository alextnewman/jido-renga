// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot

#include "framework/jr_test.h"

#include "BayTrailRegisters.h"


using namespace gpio;
using namespace gpio::baytrail;


JR_TEST(byt_gpio, community_layout_is_exact)
{
	JR_CHECK_EQ(PinCount(Community::Score), (uint32)102);
	JR_CHECK_EQ(PinCount(Community::Ncore), (uint32)28);
	JR_CHECK_EQ(PinCount(Community::Sus), (uint32)44);

	JR_CHECK_EQ(PadForPin(Community::Score, 14), (uint8)40);
	JR_CHECK_EQ(PadForPin(Community::Score, 15), (uint8)84);
	JR_CHECK_EQ(PadForPin(Community::Sus, 9), (uint8)37);
	JR_CHECK_EQ(PadForPin(Community::Score, 102), (uint8)0xff);

	JR_CHECK_EQ(PadRegisterOffset(Community::Score, 14, kConfig0),
		(size_t)0x280);
	JR_CHECK_EQ(PadRegisterOffset(Community::Score, 15, kValue),
		(size_t)0x548);
}


JR_TEST(byt_gpio, interrupt_register_math_is_exact)
{
	JR_CHECK_EQ(InterruptStatusOffset(0), (size_t)0x800);
	JR_CHECK_EQ(InterruptStatusOffset(31), (size_t)0x800);
	JR_CHECK_EQ(InterruptStatusOffset(32), (size_t)0x804);
	JR_CHECK_EQ(InterruptStatusOffset(96), (size_t)0x80c);
	JR_CHECK_EQ(InterruptStatusBit(14), (uint32)0x00004000);
	JR_CHECK_EQ(InterruptStatusBit(47), (uint32)0x00008000);

	JR_CHECK_EQ(TriggerBits(Edge::Rising), kTriggerPositive);
	JR_CHECK_EQ(TriggerBits(Edge::Falling), kTriggerNegative);
	JR_CHECK_EQ(TriggerBits(Edge::Both),
		kTriggerPositive | kTriggerNegative);
	JR_CHECK((TriggerBits(Edge::Both) & kTriggerLevel) == 0);
}


JR_TEST(byt_gpio, gpio_mux_and_edge_policy_are_explicit)
{
	JR_CHECK_EQ(GpioMux(Community::Score, 14), (uint32)0);
	JR_CHECK_EQ(GpioMux(Community::Score, 92), (uint32)1);
	JR_CHECK_EQ(GpioMux(Community::Sus, 10), (uint32)0);
	JR_CHECK_EQ(GpioMux(Community::Sus, 11), (uint32)1);
	JR_CHECK_EQ(GpioMux(Community::Sus, 21), (uint32)1);
	JR_CHECK_EQ(GpioMux(Community::Sus, 22), (uint32)0);

	JR_CHECK(EdgeMatches(Edge::Rising, Level::Low, Level::High));
	JR_CHECK(!EdgeMatches(Edge::Rising, Level::High, Level::Low));
	JR_CHECK(EdgeMatches(Edge::Falling, Level::High, Level::Low));
	JR_CHECK(EdgeMatches(Edge::Both, Level::Low, Level::High));
	JR_CHECK(EdgeMatches(Edge::Both, Level::High, Level::Low));
	JR_CHECK(!EdgeMatches(Edge::Both, Level::Low, Level::Low));
}


JR_TEST(byt_gpio, direction_and_pull_encoding_are_exact)
{
	JR_CHECK_EQ(kInputDirection, kOutputDisable);
	JR_CHECK_EQ(kOutputDirection, kInputDisable);
	JR_CHECK_EQ(PullStrengthBits(2000), (uint32)0);
	JR_CHECK_EQ(PullStrengthBits(10000), (uint32)(1u << 9));
	JR_CHECK_EQ(PullStrengthBits(20000), (uint32)(2u << 9));
	JR_CHECK_EQ(PullStrengthBits(40000), (uint32)(3u << 9));
	JR_CHECK_EQ(PullStrengthBits(1234), UINT32_MAX);
}
