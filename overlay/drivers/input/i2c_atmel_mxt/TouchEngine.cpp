// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with Qwen 3.6
// SPDX-FileContributor: Generated with GitHub Copilot

//! Stateful touch engine for Atmel maXTouch controllers.
// Tracks individual contacts, persistent button state, and classifies
// gestures (move, tap, two-finger tap) into well-formed mxt_touch_state events.


#include "TouchEngine.h"
#include "Driver.h"

#include <string.h>


TouchEngine::TouchEngine()
	:	fActiveCount(0),
		fMaxX(1023),
		fMaxY(1023),
		fLastButtonGpio(0xff),
		fHasButtonState(false)
{
	memset(fContacts, 0, sizeof(fContacts));
}


void
TouchEngine::SetResolution(uint16 maxX, uint16 maxY)
{
	fMaxX = maxX;
	fMaxY = maxY;
}


status_t
TouchEngine::ProcessMessage(const uint8* msg, size_t msgSize,
	uint8 t9ReportIDMin, uint8 t9ReportIDMax,
	uint8 t100ReportIDMin, uint8 t100ReportIDMax,
	uint8 t19ReportID, bool hasT9, bool hasT100)
{
	if (msg == NULL || msgSize < 2)
		return B_OK;

	uint8 reportID = msg[0];

	// Skip invalid/empty messages
	if (reportID == MXT_REPORT_ID_INVALID || reportID == 0)
		return B_OK;

	// T9 legacy touch message
	if (hasT9 && reportID >= t9ReportIDMin
		&& reportID <= t9ReportIDMax) {
		return _ParseT9Contact(msg, msgSize, t9ReportIDMin);
	}

	// T100 modern touch message
	if (hasT100 && reportID >= t100ReportIDMin
		&& reportID <= t100ReportIDMax) {
		return _ParseT100Contact(msg, msgSize, t100ReportIDMin);
	}

	// T19 is change-only, so button state persists between messages.
	if (t19ReportID != 0 && reportID == t19ReportID) {
		if (msgSize >= 2) {
			uint8 gpio = msg[1];
			TRACE("engine: T19 msg id=0x%02x gpio=0x%02x button=%s\n",
				(unsigned)reportID, (unsigned)gpio,
				!(gpio & 0x20) ? "PRESSED" : "RELEASED");
			fLastButtonGpio = gpio;
			fHasButtonState = true;
		}
		return B_OK;
	}

	// T6 command/status, T22 noise, etc. — trace for diagnostics
	if (reportID != 0xFF) {	// 0xFF is batch terminator, always silent
		TRACE("engine: UNKNOWN msg id=0x%02x payload="
			"(hasT9=%d,t9=%u-%u)(hasT100=%d,t100=%u-%u)(t19=%u)\n",
			(unsigned)reportID, hasT9, t9ReportIDMin, t9ReportIDMax,
			hasT100, t100ReportIDMin, t100ReportIDMax, t19ReportID);
	}
	return B_OK;
}


status_t
TouchEngine::Flush(mxt_touch_state* state)
{
	if (state == NULL)
		return B_BAD_VALUE;

	memset(state, 0, sizeof(*state));

	// Detect two-finger tap BEFORE cleanup. On this device, lift is signaled
	// by the contact stopping messages (not by a RELEASE flag). A contact
	// that is active but didn't receive a message this cycle has just lifted.
	for (int32 i = 0; i < MXT_MAX_CONTACTS; i++) {
		if (fContacts[i].active && !fContacts[i].press)
			fContacts[i].release = true;
	}

	// Run two-finger tap detection now that release flags are correct.
	bool twoFingerTap = _DetectTwoFingerTap(state);

	if (twoFingerTap) {
		// _DetectTwoFingerTap populated state with right-click signal.
		// Clear the tapped contacts from the table.
		for (int32 i = 0; i < MXT_MAX_CONTACTS; i++) {
			fContacts[i].active = false;
			fContacts[i].press = false;
			fContacts[i].release = false;
		}
		fActiveCount = 0;
		return B_OK;
	}

	// Cleanup: deactivate contacts that didn't receive a message this cycle.
	for (int32 i = 0; i < MXT_MAX_CONTACTS; i++) {
		if (fContacts[i].active && !fContacts[i].press) {
			TRACE("engine: contact[%d] expiry (no message this cycle)\n", i);
			fContacts[i].active = false;
		}
		fContacts[i].press = false;
		fContacts[i].release = false;
	}

	// Aggregate active contacts into centroid state.
	for (int32 i = 0; i < MXT_MAX_CONTACTS; i++) {
		if (!fContacts[i].active)
			continue;

		state->x += fContacts[i].x;
		state->y += fContacts[i].y;
		state->pressure += fContacts[i].pressure;
		state->contactCount++;
	}

	if (state->contactCount > 0) {
		state->x /= state->contactCount;
		state->y /= state->contactCount;
		state->pressure /= state->contactCount;
	}

	// Finger bitmask: set lowest N bits for N contacts.
	// The input server uses count_set_bits(fingers) for gesture detection.
	if (state->contactCount > 0)
		state->fingers = (uint8)((1u << MIN(state->contactCount, 32)) - 1);

	// Persistent physical button state from T19 GPIO (active-low, bit 5).
	// Carried forward across drain cycles — clicks are never missed even
	// when T19 change messages don't arrive in every cycle.
	if (fHasButtonState && !(fLastButtonGpio & 0x20))
		state->button = true;

	return B_OK;
}


bool
TouchEngine::ButtonPressed() const
{
	if (!fHasButtonState)
		return false;
	// T19 GPIO bit 5 is active-low: clear bit = pressed.
	return !(fLastButtonGpio & 0x20);
}


uint8
TouchEngine::_ContactIndex(uint8 reportID, uint8 reportIDMin) const
{
	// Contact-table indices are offsets from the object's minimum report ID.
	return MIN(reportID - reportIDMin, MXT_MAX_CONTACTS - 1);
}


status_t
TouchEngine::_ParseT9Contact(const uint8* msg, size_t msgSize,
	uint8 reportIDMin)
{
	// T9 message format (spec Section 10.3):
	// [0] report ID, [1] flags, [2] X high, [3] Y high,
	// [4] X lo[7:4] | Y lo[3:0], [5] area, [6] amplitude, [7] vector

	if (msgSize < 7)
		return B_OK;

	uint8 flags = msg[1];

	// Skip suppressed contacts (palm/grip rejected by device firmware)
	if (flags & MXT_T9_SUPPRESS) {
		TRACE("engine: T9 msg id=0x%02x SUPPRESSED\n", msg[0]);
		return B_OK;
	}

	// Reconstruct 12-bit coordinates from split high/low bytes
	uint16 x = (msg[2] << 4) | ((msg[4] >> 4) & 0xf);
	uint16 y = (msg[3] << 4) | (msg[4] & 0xf);

	// Downscale to 10-bit if range < 1024 (spec Section 10.3)
	if (fMaxX < 1024)
		x >>= 2;
	if (fMaxY < 1024)
		y >>= 2;

	// Clamp to hardware range
	if (x > fMaxX)
		x = fMaxX;
	if (y > fMaxY)
		y = fMaxY;

	uint8 area = msg[5];
	bool detect = (flags & MXT_CONTACT_DETECT) != 0;
	bool release = (flags & MXT_CONTACT_RELEASE) != 0;

	TRACE("engine: T9 msg id=0x%02x flags=0x%02x x=%u y=%u area=%u detect=%d release=%d\n",
		msg[0], flags, x, y, area, detect, release);

	if (!detect) {
		TRACE("engine: T9 msg id=0x%02x SKIPPED (detect=0)\n", msg[0]);
		return B_OK;
	}

	uint8 index = _ContactIndex(msg[0], reportIDMin);
	mxt_contact_state* contact = &fContacts[index];

	// Infer press from state transition: this device (WINKY) does not set
	// the T9 PRESS flag in its messages. A contact is "new" if it was not
	// active before this message arrived. This matches the T100 path which
	// has no explicit PRESS flag at all.
	bool isNew = !contact->active;

	if (isNew) {
		contact->initialX = x;
		contact->initialY = y;
		contact->deltaX = 0;
		contact->deltaY = 0;
	}

	// Flush() treats press as the per-cycle contact refresh marker.
	contact->press = true;

	contact->x = x;
	contact->y = y;
	contact->pressure = area;
	contact->active = true;
	contact->release = release;

	// Track cumulative movement from initial touch position
	contact->deltaX = (x > contact->initialX)
		? x - contact->initialX
		: contact->initialX - x;
	contact->deltaY = (y > contact->initialY)
		? y - contact->initialY
		: contact->initialY - y;

	return B_OK;
}


status_t
TouchEngine::_ParseT100Contact(const uint8* msg, size_t msgSize,
	uint8 reportIDMin)
{
	// T100 message format (spec Section 10.4):
	// [0] report ID, [1] flags, [2-3] X (LE), [4-5] Y (LE),
	// [6+] auxiliary bytes per aux-enable field

	if (msgSize < 6)
		return B_OK;

	uint8 flags = msg[1];
	uint8 type = (flags & 0x70) >> 4;
	bool detect = (flags & MXT_CONTACT_DETECT) != 0;

	// Skip non-active contact types (hover, large/suppressed)
	if (type == MXT_T100_TYPE_HOVER_FINGER
		|| type == MXT_T100_TYPE_LARGE) {
		return B_OK;
	}

	// Only track finger, stylus, and glove contacts
	if (type != MXT_T100_TYPE_FINGER
		&& type != MXT_T100_TYPE_PASSIVE_STYLUS
		&& type != MXT_T100_TYPE_GLOVE) {
		return B_OK;
	}

	if (!detect)
		return B_OK;

	uint16 x = msg[2] | (msg[3] << 8);
	uint16 y = msg[4] | (msg[5] << 8);

	// Clamp to hardware range
	if (x > fMaxX)
		x = fMaxX;
	if (y > fMaxY)
		y = fMaxY;

	uint8 area = 0;
	if (msgSize >= 8)
		area = msg[7];

	// T100 contact arrival is inferred from the persistent contact table.
	uint8 index = _ContactIndex(msg[0], reportIDMin);
	mxt_contact_state* contact = &fContacts[index];

	bool isNew = !contact->active;

	if (isNew) {
		contact->initialX = x;
		contact->initialY = y;
		contact->deltaX = 0;
		contact->deltaY = 0;
	}

	// Mark the contact as refreshed for this drain cycle.
	contact->press = true;

	contact->x = x;
	contact->y = y;
	contact->pressure = area;
	contact->active = true;
	contact->release = false;	// will be set by Flush() expiry

	// Track cumulative movement from initial touch position
	contact->deltaX = (x > contact->initialX)
		? x - contact->initialX
		: contact->initialX - x;
	contact->deltaY = (y > contact->initialY)
		? y - contact->initialY
		: contact->initialY - y;

	return B_OK;
}


bool
TouchEngine::_DetectTwoFingerTap(mxt_touch_state* state)
{
	// Count contacts that lifted this cycle with minimal movement.
	// A two-finger tap requires exactly two such contacts.
	int liftCount = 0;

	for (int32 i = 0; i < MXT_MAX_CONTACTS; i++) {
		mxt_contact_state* c = &fContacts[i];
		if (!c->release)
			continue;
		if (c->deltaX > MXT_TAP_MAX_DELTA
			|| c->deltaY > MXT_TAP_MAX_DELTA) {
			continue;
		}
		liftCount++;
	}

	if (liftCount != 2)
		return false;

	// Two-finger tap confirmed. Emit a lift event with the right-click
	// signal. The input server's _ClickFingerButtonEmulator converts
	// buttons + two fingers into a right-button click.
	state->contactCount = 0;
	state->fingers = 0x03;		// two fingers (for emulator check)
	state->button = true;		// triggers emulator → right button

	// Centroid of the two lift positions (used by input server for
	// cursor placement on click).
	int idx[2];
	int found = 0;
	for (int32 i = 0; i < MXT_MAX_CONTACTS && found < 2; i++) {
		mxt_contact_state* c = &fContacts[i];
		if (c->release
			&& c->deltaX <= MXT_TAP_MAX_DELTA
			&& c->deltaY <= MXT_TAP_MAX_DELTA) {
			idx[found++] = i;
		}
	}

	if (found == 2) {
		state->x = (fContacts[idx[0]].x + fContacts[idx[1]].x) / 2;
		state->y = (fContacts[idx[0]].y + fContacts[idx[1]].y) / 2;
	}

	return true;
}
