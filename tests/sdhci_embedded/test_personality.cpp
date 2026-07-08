// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot

#include "framework/jr_test.h"

#include "Personality.h"

#include <cstring>

using namespace jr::sdhci;


// Mock target: records how a personality's PostResetInit poked the controller,
// with no real MMIO. This is the seed pattern for driver hardware mocks.
namespace {
class FakeQuirkTarget final : public IHostQuirkTarget {
public:
	int presetDisableCount = 0;
	void DisablePresetValueMode() override { presetDisableCount++; }
};
}


JR_TEST(personality, generic_is_permissive_and_default)
{
	const HostPersonality& p = GetPersonality(PersonalityKind::Generic);
	JR_CHECK(std::strcmp(p.Name(), "generic") == 0);
	JR_CHECK(p.ValidateOcr(0x40ff8080, CardDialect::Sd));
	JR_CHECK(!p.ValidateOcr(0, CardDialect::Sd));

	ReplyType out = ReplyType::None;
	JR_CHECK(!p.OverrideReplyType(Cmd::StopTransmission, CardDialect::Sd, out));
	JR_CHECK_EQ(p.TimeoutClockKHz(), 0u);
	JR_CHECK(!p.TimeoutClockUsesSdClock());

	FakeQuirkTarget target;
	p.PostResetInit(target);
	JR_CHECK_EQ(target.presetDisableCount, 0);	// generic touches nothing
}


JR_TEST(personality, baytrail_rejects_garbage_ocr)
{
	const HostPersonality& p = GetPersonality(PersonalityKind::BayTrail);
	JR_CHECK(!p.ValidateOcr(0x00000000, CardDialect::Mmc));
	JR_CHECK(!p.ValidateOcr(0xffffffff, CardDialect::Mmc));
	JR_CHECK(!p.ValidateOcr(0xcccccccc, CardDialect::Mmc));
	JR_CHECK(!p.ValidateOcr(0x55555555, CardDialect::Mmc));
	JR_CHECK(p.ValidateOcr(0x40ff8080, CardDialect::Mmc));	// a real OCR
}


JR_TEST(personality, baytrail_overrides_cmd12_to_r1b)
{
	const HostPersonality& p = GetPersonality(PersonalityKind::BayTrail);
	ReplyType out = ReplyType::None;
	JR_CHECK(p.OverrideReplyType(Cmd::StopTransmission, CardDialect::Mmc, out));
	JR_CHECK(out == ReplyType::R1b);
	// Other commands are not overridden.
	JR_CHECK(!p.OverrideReplyType(Cmd::GoIdleState, CardDialect::Mmc, out));
}


JR_TEST(personality, baytrail_timeout_and_reset_hooks)
{
	const HostPersonality& p = GetPersonality(PersonalityKind::BayTrail);
	JR_CHECK_EQ(p.TimeoutClockKHz(), 1000u);
	JR_CHECK(p.TimeoutClockUsesSdClock());

	FakeQuirkTarget target;
	p.PostResetInit(target);
	JR_CHECK_EQ(target.presetDisableCount, 1);	// disabled broken preset mode
}
