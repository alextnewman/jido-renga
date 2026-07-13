// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot

#include "framework/jr_test.h"

#include "Matcher.h"

#include <cstring>

using namespace jr::sdhci;


JR_TEST(matcher, baytrail_emmc_matches_hid)
{
	const MatchProfile* p = MatchProfileFor("80860F14");
	JR_CHECK(p != nullptr);
	if (p != nullptr) {
		JR_CHECK(p->dialect == CardDialect::Mmc);
		JR_CHECK(!p->removable);	// soldered eMMC: no hot-plug watcher
		JR_CHECK(p->dma == DmaStrategy::Adma2);
		JR_CHECK(p->personality == PersonalityKind::BayTrail);
		JR_CHECK(Has(p->quirks, Quirk::EmmcHardwareReset));
		JR_CHECK(Has(p->quirks, Quirk::NeedsIosfOcpFixup));
		JR_CHECK(Has(p->quirks, Quirk::PowerOnDelay));
		JR_CHECK(Has(p->quirks, Quirk::ClockPllSequence));
		JR_CHECK(Has(p->quirks, Quirk::Fixed1MHzTimeoutClock));
		JR_CHECK(!Has(p->quirks, Quirk::CardOnNeedsBusOn));
		JR_CHECK(std::strstr(p->prettyName, "eMMC") != nullptr);
	}
}


JR_TEST(matcher, baytrail_controller_roles_come_from_hid)
{
	const MatchProfile* emmc = MatchProfileFor("80860F14");
	const MatchProfile* sd = MatchProfileFor("80860F16");
	JR_CHECK(emmc != nullptr);
	JR_CHECK(sd != nullptr);
	if (emmc != nullptr && sd != nullptr)
		JR_CHECK(emmc->dialect != sd->dialect);
}


JR_TEST(matcher, baytrail_sd_matches_hid)
{
	const MatchProfile* profile = MatchProfileFor("80860F16");
	JR_CHECK(profile != nullptr);
	if (profile != nullptr) {
		JR_CHECK(profile->dialect == CardDialect::Sd);
		JR_CHECK(profile->removable);	// SD slot: hot-plug watcher runs
		JR_CHECK(profile->dma == DmaStrategy::Sdma);
		JR_CHECK(!Has(profile->quirks, Quirk::EmmcHardwareReset));
		JR_CHECK(Has(profile->quirks, Quirk::PowerOnDelay));
		JR_CHECK(Has(profile->quirks, Quirk::ClockPllSequence));
		JR_CHECK(Has(profile->quirks, Quirk::CardOnNeedsBusOn));
		JR_CHECK(!Has(profile->quirks, Quirk::Fixed1MHzTimeoutClock));
		JR_CHECK(std::strstr(profile->prettyName, "SD") != nullptr);
	}
}


JR_TEST(matcher, unknown_hid_and_null_do_not_match)
{
	JR_CHECK(MatchProfileFor("PNP0A03") == nullptr);
	JR_CHECK(MatchProfileFor("80860F15") == nullptr);	// SDIO, unsupported
	JR_CHECK(MatchProfileFor(nullptr) == nullptr);
}


JR_TEST(matcher, table_is_populated)
{
	JR_CHECK_EQ(ProfileCount(), 2u);
	JR_CHECK(ProfileTable() != nullptr);
}
