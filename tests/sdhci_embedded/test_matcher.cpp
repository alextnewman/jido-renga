// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot

#include "framework/jr_test.h"

#include "Matcher.h"

#include <cstring>

using namespace jr::sdhci;


JR_TEST(matcher, baytrail_emmc_matches_hid_and_uid)
{
	const MatchProfile* p = MatchProfileFor("80860F14", 1);
	JR_CHECK(p != nullptr);
	if (p != nullptr) {
		JR_CHECK(p->dialect == CardDialect::Mmc);
		JR_CHECK(p->dma == DmaStrategy::Adma2);
		JR_CHECK(p->personality == PersonalityKind::BayTrail);
		JR_CHECK(Has(p->quirks, Quirk::EmmcHardwareReset));
		JR_CHECK(Has(p->quirks, Quirk::NeedsIosfOcpFixup));
		JR_CHECK(std::strstr(p->prettyName, "eMMC") != nullptr);
	}
}


JR_TEST(matcher, baytrail_emmc_rejects_wrong_uid)
{
	// The eMMC controller is UID 1 specifically; UID 0 must not match.
	JR_CHECK(MatchProfileFor("80860F14", 0) == nullptr);
	JR_CHECK(MatchProfileFor("80860F14", 2) == nullptr);
}


JR_TEST(matcher, baytrail_sd_matches_any_uid)
{
	const MatchProfile* a = MatchProfileFor("80860F16", 0);
	const MatchProfile* b = MatchProfileFor("80860F16", 7);
	JR_CHECK(a != nullptr);
	JR_CHECK(a == b);
	if (a != nullptr) {
		JR_CHECK(a->dialect == CardDialect::Sd);
		JR_CHECK(!Has(a->quirks, Quirk::EmmcHardwareReset));
		JR_CHECK(std::strstr(a->prettyName, "SD") != nullptr);
	}
}


JR_TEST(matcher, unknown_hid_and_null_do_not_match)
{
	JR_CHECK(MatchProfileFor("PNP0A03", 0) == nullptr);
	JR_CHECK(MatchProfileFor("80860F15", 0) == nullptr);	// SDIO, unsupported
	JR_CHECK(MatchProfileFor(nullptr, 0) == nullptr);
}


JR_TEST(matcher, score_beats_upstream_on_match_zero_otherwise)
{
	// The whole point: 0.9 > upstream's 0.8, 0.0 on a miss.
	JR_CHECK_EQ(ScoreFor("80860F14", 1), 0.9f);
	JR_CHECK_EQ(ScoreFor("80860F16", 3), 0.9f);
	JR_CHECK_EQ(ScoreFor("PNP0A03", 0), 0.0f);
	JR_CHECK(kMatchScore > 0.8f);
}


JR_TEST(matcher, table_is_populated)
{
	JR_CHECK_EQ(ProfileCount(), 2u);
	JR_CHECK(ProfileTable() != nullptr);
}
