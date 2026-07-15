// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot

//! Host-side contact-frame tests for the maXTouch touch engine.


#include "framework/jr_test.h"

#include "TouchEngine.h"


namespace {

const uint8 kT9ReportIDMin = 10;
const uint8 kT9ReportIDMax = 19;
const uint8 kT19ReportID = 30;


void
ProcessT9(TouchEngine& engine, uint8 reportID, uint8 flags, uint16 x,
	uint16 y, uint8 area, bigtime_t timestamp = 0)
{
	uint8 message[] = {
		reportID,
		flags,
		(uint8)(x >> 4),
		(uint8)(y >> 4),
		(uint8)(((x & 0x0f) << 4) | (y & 0x0f)),
		area,
		0,
	};
	JR_CHECK_EQ(engine.ProcessMessage(message, sizeof(message), kT9ReportIDMin,
		kT9ReportIDMax, 0, 0, kT19ReportID, true, false, timestamp), B_OK);
}


void
ProcessT19(TouchEngine& engine, uint8 gpio, bigtime_t timestamp = 0)
{
	uint8 message[] = { kT19ReportID, gpio };
	JR_CHECK_EQ(engine.ProcessMessage(message, sizeof(message), kT9ReportIDMin,
		kT9ReportIDMax, 0, 0, kT19ReportID, true, false, timestamp), B_OK);
}


mxt_touch_batch
Flush(TouchEngine& engine)
{
	mxt_touch_batch batch;
	JR_CHECK_EQ(engine.Flush(&batch), B_OK);
	return batch;
}


const mxt_touch_state&
Last(const mxt_touch_batch& batch)
{
	JR_CHECK(batch.count > 0);
	return batch.frames[batch.count - 1];
}

}	// namespace


JR_TEST(touch_engine, retains_delta_slots_and_tracks_primary)
{
	TouchEngine engine;
	engine.SetResolution(4095, 4095);

	ProcessT9(engine, kT9ReportIDMin, MXT_CONTACT_DETECT, 1000, 2000, 20);
	ProcessT9(engine, kT9ReportIDMin + 1, MXT_CONTACT_DETECT, 3000, 2500, 40);
	mxt_touch_batch batch = Flush(engine);
	JR_CHECK_EQ(batch.count, (uint8)1);
	JR_CHECK_EQ(Last(batch).contactCount, 2);
	JR_CHECK_EQ(Last(batch).fingers, (uint8)0x03);
	JR_CHECK_EQ(Last(batch).x, (uint16)1000);

	ProcessT9(engine, kT9ReportIDMin + 1, MXT_CONTACT_DETECT, 3500, 2800, 40);
	batch = Flush(engine);
	JR_CHECK_EQ(Last(batch).x, (uint16)1000);

	ProcessT9(engine, kT9ReportIDMin, MXT_CONTACT_DETECT, 1200, 2200, 20);
	batch = Flush(engine);
	JR_CHECK_EQ(Last(batch).x, (uint16)1200);
	JR_CHECK_EQ(Last(batch).y, (uint16)2200);
}


JR_TEST(touch_engine, secondary_release_keeps_primary_continuity)
{
	TouchEngine engine;
	engine.SetResolution(4095, 4095);

	ProcessT9(engine, kT9ReportIDMin, MXT_CONTACT_DETECT, 1000, 2000, 20);
	ProcessT9(engine, kT9ReportIDMin + 1, MXT_CONTACT_DETECT, 3000, 2500, 40);
	Flush(engine);

	ProcessT9(engine, kT9ReportIDMin + 1, MXT_CONTACT_RELEASE, 0, 0, 0);
	mxt_touch_batch batch = Flush(engine);
	JR_CHECK_EQ(batch.count, (uint8)1);
	JR_CHECK_EQ(Last(batch).contactCount, 1);
	JR_CHECK_EQ(Last(batch).fingers, (uint8)0x01);
	JR_CHECK_EQ(Last(batch).x, (uint16)1000);
}


JR_TEST(touch_engine, primary_release_queues_reset_and_replacement)
{
	TouchEngine engine;
	engine.SetResolution(4095, 4095);

	ProcessT9(engine, kT9ReportIDMin, MXT_CONTACT_DETECT, 1000, 2000, 20);
	ProcessT9(engine, kT9ReportIDMin + 1, MXT_CONTACT_DETECT, 3000, 2500, 40);
	Flush(engine);

	ProcessT9(engine, kT9ReportIDMin, MXT_CONTACT_RELEASE, 0, 0, 0);
	mxt_touch_batch batch = Flush(engine);
	JR_CHECK_EQ(batch.count, (uint8)2);
	JR_CHECK_EQ(batch.frames[0].contactCount, 0);
	JR_CHECK_EQ(batch.frames[1].contactCount, 1);
	JR_CHECK_EQ(batch.frames[1].fingers, (uint8)0x02);
	JR_CHECK_EQ(batch.frames[1].x, (uint16)3000);
}


JR_TEST(touch_engine, release_detect_queues_reset_and_new_lifecycle)
{
	TouchEngine engine;
	engine.SetResolution(4095, 4095);

	ProcessT9(engine, kT9ReportIDMin, MXT_CONTACT_DETECT, 1000, 2000, 20);
	ProcessT9(engine, kT9ReportIDMin + 1, MXT_CONTACT_DETECT, 3000, 2500, 40);
	Flush(engine);

	ProcessT9(engine, kT9ReportIDMin,
		MXT_CONTACT_RELEASE | MXT_CONTACT_DETECT, 1400, 2300, 20);
	mxt_touch_batch batch = Flush(engine);
	JR_CHECK_EQ(batch.count, (uint8)2);
	JR_CHECK_EQ(batch.frames[0].contactCount, 0);
	JR_CHECK_EQ(batch.frames[1].contactCount, 2);
	JR_CHECK_EQ(batch.frames[1].x, (uint16)1400);
}


JR_TEST(touch_engine, captures_and_holds_two_finger_click_snapshot)
{
	TouchEngine engine;
	engine.SetResolution(4095, 4095);

	ProcessT9(engine, kT9ReportIDMin, MXT_CONTACT_DETECT, 1000, 2000, 20, 1000);
	ProcessT9(engine, kT9ReportIDMin + 1, MXT_CONTACT_DETECT, 3000, 2500, 40,
		1000);
	Flush(engine);

	ProcessT9(engine, kT9ReportIDMin, MXT_CONTACT_RELEASE, 0, 0, 0, 2000);
	mxt_touch_batch batch = Flush(engine);
	JR_CHECK_EQ(batch.count, (uint8)2);

	ProcessT19(engine, 0x00, 3000);
	batch = Flush(engine);
	JR_CHECK_EQ(batch.count, (uint8)1);
	JR_CHECK_EQ(Last(batch).contactCount, 2);
	JR_CHECK_EQ(Last(batch).fingers, (uint8)0x03);
	JR_CHECK_EQ(Last(batch).x, (uint16)1000);
	JR_CHECK(Last(batch).button);

	ProcessT9(engine, kT9ReportIDMin + 1, MXT_CONTACT_DETECT, 3800, 3200, 40,
		4000);
	batch = Flush(engine);
	JR_CHECK_EQ(Last(batch).x, (uint16)1000);
	JR_CHECK_EQ(Last(batch).y, (uint16)2000);
	JR_CHECK(Last(batch).button);

	ProcessT19(engine, 0x20, 5000);
	batch = Flush(engine);
	JR_CHECK_EQ(batch.count, (uint8)2);
	JR_CHECK_EQ(batch.frames[0].contactCount, 0);
	JR_CHECK(!batch.frames[0].button);
	JR_CHECK_EQ(batch.frames[1].contactCount, 1);
	JR_CHECK_EQ(batch.frames[1].x, (uint16)3800);
}


JR_TEST(touch_engine, captures_two_finger_click_before_first_flush)
{
	TouchEngine engine;
	engine.SetResolution(4095, 4095);

	ProcessT9(engine, kT9ReportIDMin, MXT_CONTACT_DETECT, 1000, 2000, 20, 1000);
	ProcessT9(engine, kT9ReportIDMin + 1, MXT_CONTACT_DETECT, 3000, 2500, 40,
		1000);
	ProcessT9(engine, kT9ReportIDMin, MXT_CONTACT_RELEASE, 0, 0, 0, 2000);
	ProcessT9(engine, kT9ReportIDMin + 1, MXT_CONTACT_RELEASE, 0, 0, 0, 2000);
	ProcessT19(engine, 0x00, 3000);

	mxt_touch_batch batch = Flush(engine);
	JR_CHECK_EQ(Last(batch).contactCount, 2);
	JR_CHECK_EQ(Last(batch).fingers, (uint8)0x03);
	JR_CHECK_EQ(Last(batch).x, (uint16)1000);
	JR_CHECK(Last(batch).button);
}


JR_TEST(touch_engine, preserves_button_edges_in_one_drain)
{
	TouchEngine engine;
	engine.SetResolution(4095, 4095);

	ProcessT9(engine, kT9ReportIDMin, MXT_CONTACT_DETECT, 1000, 2000, 20);
	Flush(engine);

	ProcessT19(engine, 0x00);
	ProcessT19(engine, 0x20);
	mxt_touch_batch batch = Flush(engine);
	JR_CHECK_EQ(batch.count, (uint8)2);
	JR_CHECK(batch.frames[0].button);
	JR_CHECK(!batch.frames[1].button);
}


JR_TEST(touch_engine, preserves_release_press_order_in_one_drain)
{
	TouchEngine engine;
	engine.SetResolution(4095, 4095);

	ProcessT9(engine, kT9ReportIDMin, MXT_CONTACT_DETECT, 1000, 2000, 20);
	ProcessT19(engine, 0x00);
	Flush(engine);

	ProcessT19(engine, 0x20);
	ProcessT19(engine, 0x00);
	mxt_touch_batch batch = Flush(engine);
	JR_CHECK_EQ(batch.count, (uint8)2);
	JR_CHECK(!batch.frames[0].button);
	JR_CHECK(batch.frames[1].button);
}


JR_TEST(touch_engine, invalidates_click_candidate_on_new_gesture)
{
	TouchEngine engine;
	engine.SetResolution(4095, 4095);

	ProcessT9(engine, kT9ReportIDMin, MXT_CONTACT_DETECT, 1000, 2000, 20, 1000);
	ProcessT9(engine, kT9ReportIDMin + 1, MXT_CONTACT_DETECT, 3000, 2500, 40,
		1000);
	Flush(engine);

	ProcessT9(engine, kT9ReportIDMin, MXT_CONTACT_RELEASE, 0, 0, 0, 2000);
	ProcessT9(engine, kT9ReportIDMin + 1, MXT_CONTACT_RELEASE, 0, 0, 0, 2000);
	Flush(engine);

	ProcessT9(engine, kT9ReportIDMin, MXT_CONTACT_DETECT, 1700, 2300, 20, 3000);
	ProcessT19(engine, 0x00, 4000);
	mxt_touch_batch batch = Flush(engine);
	JR_CHECK_EQ(Last(batch).contactCount, 1);
	JR_CHECK_EQ(Last(batch).fingers, (uint8)0x01);
	JR_CHECK_EQ(Last(batch).x, (uint16)1700);
	JR_CHECK(Last(batch).button);
}


JR_TEST(touch_engine, expires_old_click_snapshot)
{
	TouchEngine engine;
	engine.SetResolution(4095, 4095);

	ProcessT9(engine, kT9ReportIDMin, MXT_CONTACT_DETECT, 1000, 2000, 20, 1000);
	ProcessT9(engine, kT9ReportIDMin + 1, MXT_CONTACT_DETECT, 3000, 2500, 40,
		1000);
	Flush(engine);

	ProcessT9(engine, kT9ReportIDMin, MXT_CONTACT_RELEASE, 0, 0, 0, 2000);
	Flush(engine);

	ProcessT19(engine, 0x00, 200000);
	mxt_touch_batch batch = Flush(engine);
	JR_CHECK_EQ(Last(batch).contactCount, 1);
	JR_CHECK_EQ(Last(batch).fingers, (uint8)0x02);
	JR_CHECK_EQ(Last(batch).x, (uint16)3000);
	JR_CHECK(Last(batch).button);
}


JR_TEST(touch_engine, ignores_duplicate_inactive_slot_reports)
{
	TouchEngine engine;
	engine.SetResolution(4095, 4095);

	ProcessT9(engine, kT9ReportIDMin, MXT_CONTACT_DETECT, 1000, 2000, 20);
	Flush(engine);

	ProcessT9(engine, kT9ReportIDMin + 1, MXT_CONTACT_RELEASE, 0, 0, 0);
	mxt_touch_batch batch = Flush(engine);
	JR_CHECK_EQ(batch.count, (uint8)1);
	JR_CHECK_EQ(Last(batch).contactCount, 1);
	JR_CHECK_EQ(Last(batch).x, (uint16)1000);
}


JR_TEST(touch_engine, keeps_one_finger_click_drag_live)
{
	TouchEngine engine;
	engine.SetResolution(4095, 4095);

	ProcessT9(engine, kT9ReportIDMin, MXT_CONTACT_DETECT, 1000, 2000, 20);
	ProcessT19(engine, 0x00);
	mxt_touch_batch batch = Flush(engine);
	JR_CHECK_EQ(Last(batch).contactCount, 1);
	JR_CHECK(Last(batch).button);

	ProcessT9(engine, kT9ReportIDMin, MXT_CONTACT_DETECT, 1600, 2400, 20);
	batch = Flush(engine);
	JR_CHECK_EQ(Last(batch).x, (uint16)1600);
	JR_CHECK_EQ(Last(batch).y, (uint16)2400);
	JR_CHECK(Last(batch).button);
}


JR_TEST(touch_engine, clears_state_after_incomplete_drain)
{
	TouchEngine engine;
	engine.SetResolution(4095, 4095);

	ProcessT9(engine, kT9ReportIDMin, MXT_CONTACT_DETECT, 1000, 2000, 20);
	ProcessT19(engine, 0x00);
	Flush(engine);

	engine.Reset();
	mxt_touch_batch batch = Flush(engine);
	JR_CHECK_EQ(batch.count, (uint8)1);
	JR_CHECK_EQ(Last(batch).contactCount, 0);
	JR_CHECK(!Last(batch).button);
}


JR_TEST(touch_engine, skips_t100_status_and_releases_slots)
{
	TouchEngine engine;
	engine.SetResolution(4095, 4095);

	uint8 status[] = { 40, 0, 0, 0, 0, 0 };
	JR_CHECK_EQ(engine.ProcessMessage(status, sizeof(status), 0, 0, 40, 45, 0,
		false, true, 0), B_OK);
	mxt_touch_batch batch = Flush(engine);
	JR_CHECK_EQ(Last(batch).contactCount, 0);

	uint8 contact[] = { 42, 0x90, 0xd0, 0x07, 0xb8, 0x0b, 0, 36 };
	JR_CHECK_EQ(engine.ProcessMessage(contact, sizeof(contact), 0, 0, 40, 45, 0,
		false, true, 0), B_OK);
	batch = Flush(engine);
	JR_CHECK_EQ(Last(batch).contactCount, 1);
	JR_CHECK_EQ(Last(batch).x, (uint16)2000);
	JR_CHECK_EQ(Last(batch).y, (uint16)3000);

	uint8 release[] = { 42, 0, 0, 0, 0, 0 };
	JR_CHECK_EQ(engine.ProcessMessage(release, sizeof(release), 0, 0, 40, 45, 0,
		false, true, 0), B_OK);
	batch = Flush(engine);
	JR_CHECK_EQ(Last(batch).contactCount, 0);
}
