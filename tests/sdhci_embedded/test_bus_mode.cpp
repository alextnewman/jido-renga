// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot

#include "framework/jr_test.h"

#include "BusMode.h"

using namespace jr::sdhci;


JR_TEST(bus_mode, decodes_winky_host_capabilities)
{
	const HostCapabilities host
		= DecodeHostCapabilities(0x446cc8b2, 0x00000807, 0x0f);
	JR_CHECK_EQ(host.baseClockKHz, 200000u);
	JR_CHECK(host.adma2);
	JR_CHECK(host.highSpeed);
	JR_CHECK(host.width8);
	JR_CHECK(host.voltage1v8);
	JR_CHECK(!host.voltage3v3);
	JR_CHECK(host.sdr50);
	JR_CHECK(host.sdr104);
	JR_CHECK(host.ddr50);
}


JR_TEST(bus_mode, clock_divider_never_exceeds_target)
{
	ClockSetting setting = ComputeClockSetting(200000, 52000);
	JR_CHECK_EQ(setting.divider, 4);
	JR_CHECK_EQ(setting.actualKHz, 50000u);

	setting = ComputeClockSetting(200000, 208000);
	JR_CHECK_EQ(setting.divider, 1);
	JR_CHECK_EQ(setting.actualKHz, 200000u);

	setting = ComputeClockSetting(200000, 400);
	JR_CHECK_EQ(setting.divider, 500);
	JR_CHECK_EQ(setting.actualKHz, 400u);
}


JR_TEST(bus_mode, mmc_ocr_never_sets_ready_bit)
{
	HostCapabilities host;
	host.voltage1v8 = true;
	const uint32_t selected = SelectOcrWindow(0x00ff8080, MmcHostOcrMask(host));
	JR_CHECK_EQ(selected, 0x80u);
	const uint32_t request = BuildMmcOcrArgument(selected);
	JR_CHECK((request & (1u << 30)) != 0);
	JR_CHECK((request & (1u << 31)) == 0);
}


JR_TEST(bus_mode, ext_csd_drives_hs200_first)
{
	uint8_t raw[512] = {};
	raw[192] = 8;
	raw[196] = (1u << 4) | (1u << 2) | (1u << 1) | 1u;
	raw[212] = 0x00;
	raw[213] = 0x90;
	raw[214] = 0xd5;
	raw[215] = 0x01;
	raw[236] = 0xa0;
	const EmmcExtCsd card = DecodeExtCsd(raw);

	HostCapabilities host;
	host.width8 = true;
	host.voltage1v8 = true;
	host.sdr104 = true;
	host.ddr50 = true;
	host.highSpeed = true;
	JR_CHECK(Supports(card, host, EmmcMode::Hs200));
	JR_CHECK_EQ(PowerClass8Bit(card, EmmcMode::Hs200), 0xau);
	JR_CHECK_EQ(Describe(EmmcMode::Hs200).clockKHz, 200000u);
}


JR_TEST(bus_mode, scr_and_sd_switch_decode)
{
	uint8_t scr[8] = {0x02, 0x85, 0x80, 0, 0, 0, 0, 0};
	const SdScr decoded = DecodeScr(scr);
	JR_CHECK(decoded.valid);
	JR_CHECK_EQ(decoded.spec, 2);
	JR_CHECK(decoded.spec3);
	JR_CHECK((decoded.busWidths & (1u << 2)) != 0);

	uint8_t status[64] = {};
	status[13] = (1u << 3) | (1u << 1);
	status[16] = 3;
	JR_CHECK(SdSwitchSupports(status, 3));
	JR_CHECK(SdSwitchSelected(status, 3));
	JR_CHECK_EQ(BuildSdSwitchArgument(true, 0, 3), 0x80fffff3u);
}
