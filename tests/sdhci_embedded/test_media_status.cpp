// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GPT-5.6 Sol

#include "framework/jr_test.h"

#include <atomic>
#include <thread>

#include "MediaStatus.h"

using namespace jr::sdhci;


JR_TEST(media_status, initial_state_does_not_report_a_change)
{
	MediaStatusTracker online;
	MediaStatusTracker offline(false);

	JR_CHECK_EQ(online.ConsumeStatus(), B_OK);
	JR_CHECK_EQ(online.ConsumeStatus(), B_OK);
	JR_CHECK_EQ(offline.ConsumeStatus(), B_DEV_NO_MEDIA);
}


JR_TEST(media_status, insertion_reports_exactly_one_change)
{
	MediaStatusTracker media(false);

	media.SetOnline(true);
	const int32 epoch = media.State();

	JR_CHECK_EQ(media.ConsumeStatus(), B_DEV_MEDIA_CHANGED);
	JR_CHECK_EQ(media.ConsumeStatus(), B_OK);
	JR_CHECK_EQ(media.State(), epoch);
}


JR_TEST(media_status, removal_and_reinsertion_advance_the_epoch)
{
	MediaStatusTracker media;
	const int32 initialEpoch = media.State();

	media.SetOnline(false);
	const int32 removedEpoch = media.State();
	JR_CHECK_NE(removedEpoch, initialEpoch);
	JR_CHECK_EQ(media.ConsumeStatus(), B_DEV_NO_MEDIA);

	media.SetOnline(true);
	JR_CHECK_NE(media.State(), removedEpoch);
	JR_CHECK_EQ(media.ConsumeStatus(), B_DEV_MEDIA_CHANGED);
	JR_CHECK_EQ(media.ConsumeStatus(), B_OK);
}


JR_TEST(media_status, failed_delivery_can_restore_the_change)
{
	MediaStatusTracker media(false);
	media.SetOnline(true);

	JR_CHECK_EQ(media.ConsumeStatus(), B_DEV_MEDIA_CHANGED);
	media.RestoreChange();
	JR_CHECK_EQ(media.ConsumeStatus(), B_DEV_MEDIA_CHANGED);
	JR_CHECK_EQ(media.ConsumeStatus(), B_OK);
}


JR_TEST(media_status, concurrent_pollers_consume_one_change)
{
	MediaStatusTracker media(false);
	media.SetOnline(true);

	std::atomic<int> changed{0};
	std::atomic<int> unchanged{0};
	auto poll = [&]() {
		const status_t status = media.ConsumeStatus();
		if (status == B_DEV_MEDIA_CHANGED)
			changed++;
		else if (status == B_OK)
			unchanged++;
	};

	std::thread first(poll);
	std::thread second(poll);
	first.join();
	second.join();

	JR_CHECK_EQ(changed.load(), 1);
	JR_CHECK_EQ(unchanged.load(), 1);
}
