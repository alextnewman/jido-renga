// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot

//! Host-side maXTouch message-read transaction tests.


#include "framework/jr_test.h"

#include "MessageProtocol.h"


JR_TEST(message_protocol, reads_count_and_first_message_from_t44)
{
	mxt_message_read_step step = MxtMessageReadStep(0, 0x120, 0x121, 7);

	JR_CHECK_EQ(step.address, (uint16)0x120);
	JR_CHECK_EQ(step.length, (size_t)8);
	JR_CHECK(step.includesCount);
}


JR_TEST(message_protocol, readdresses_t5_for_every_followup_message)
{
	for (uint8 index = 1; index < 32; index++) {
		mxt_message_read_step step
			= MxtMessageReadStep(index, 0x120, 0x121, 7);

		JR_CHECK_EQ(step.address, (uint16)0x121);
		JR_CHECK_EQ(step.length, (size_t)7);
		JR_CHECK(!step.includesCount);
	}
}
