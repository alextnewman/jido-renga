// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GPT-5.6 Sol

#include "framework/jr_test.h"

#include "HotplugState.h"

using namespace jr::sdhci;


JR_TEST(hotplug_state, insertion_requests_identification)
{
	HotplugState state(false);

	JR_CHECK_EQ(state.Observe(true, false), HotplugAction::Identify);
	JR_CHECK_EQ(state.Observe(true, true), HotplugAction::None);
}


JR_TEST(hotplug_state, eject_suppresses_reidentification)
{
	HotplugState state(true);

	state.MarkEjected();
	JR_CHECK(state.IsEjected());
	JR_CHECK_EQ(state.Observe(true, false), HotplugAction::None);
	JR_CHECK_EQ(state.Observe(true, false), HotplugAction::None);
}


JR_TEST(hotplug_state, physical_cycle_rearms_an_ejected_slot)
{
	HotplugState state(true);

	state.MarkEjected();
	JR_CHECK_EQ(state.Observe(false, false), HotplugAction::Remove);
	JR_CHECK(!state.IsEjected());
	JR_CHECK_EQ(state.Observe(true, false), HotplugAction::Identify);
}


JR_TEST(hotplug_state, failed_eject_can_resume_the_present_card)
{
	HotplugState state(true);

	state.MarkEjected();
	state.CancelEject();
	JR_CHECK_EQ(state.Observe(true, false), HotplugAction::Identify);
}
