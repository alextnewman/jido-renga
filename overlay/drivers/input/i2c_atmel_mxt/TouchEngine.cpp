// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with Qwen 3.6
// SPDX-FileContributor: Generated with GitHub Copilot

//! Controller-slot tracking and Haiku touchpad frame adaptation.


#include "TouchEngine.h"
#include "Driver.h"

#include <string.h>


static const bigtime_t kClickCorrelation = 150000;


static bool
is_recent(bigtime_t now, bigtime_t then)
{
	return now <= 0 || then <= 0
		|| (now >= then && now - then <= kClickCorrelation);
}


TouchEngine::TouchEngine()
	:
	fPrimarySlot(MXT_NO_CONTACT),
	fMaxX(1023),
	fMaxY(1023),
	fButtonPressed(false),
	fButtonSnapshotActive(false),
	fLastStableValid(false),
	fLastStableTime(0),
	fClickCandidateValid(false),
	fClickCandidateTime(0),
	fLastMessageTime(0),
	fPendingFrameCount(0),
	fTraceFingers(0),
	fTraceButton(false),
	fTraceSample(0)
{
	memset(fContacts, 0, sizeof(fContacts));
	memset(&fButtonSnapshot, 0, sizeof(fButtonSnapshot));
	memset(&fLastStableState, 0, sizeof(fLastStableState));
	memset(&fClickCandidate, 0, sizeof(fClickCandidate));
	memset(fPendingFrames, 0, sizeof(fPendingFrames));
}


void
TouchEngine::SetResolution(uint16 maxX, uint16 maxY)
{
	fMaxX = maxX;
	fMaxY = maxY;
}


void
TouchEngine::Reset()
{
	_ClearContacts();
	fButtonPressed = false;
	fButtonSnapshotActive = false;
	fLastStableValid = false;
	fLastStableTime = 0;
	fClickCandidateValid = false;
	fClickCandidateTime = 0;
	fLastMessageTime = 0;
	fPendingFrameCount = 0;
	fTraceFingers = 0;
	fTraceButton = false;
	fTraceSample = 0;
	memset(&fButtonSnapshot, 0, sizeof(fButtonSnapshot));
	memset(&fLastStableState, 0, sizeof(fLastStableState));
	memset(&fClickCandidate, 0, sizeof(fClickCandidate));
	memset(fPendingFrames, 0, sizeof(fPendingFrames));
}


status_t
TouchEngine::ProcessMessage(const uint8* msg, size_t msgSize,
	uint8 t9ReportIDMin, uint8 t9ReportIDMax,
	uint8 t100ReportIDMin, uint8 t100ReportIDMax,
	uint8 t19ReportID, bool hasT9, bool hasT100, bigtime_t timestamp)
{
	if (msg == NULL || msgSize < 2)
		return B_OK;

	_ExpireClickCandidate(timestamp);
	fLastMessageTime = timestamp;

	uint8 reportID = msg[0];
	if (reportID == 0xff || reportID == 0)
		return B_OK;

	if (hasT9 && reportID >= t9ReportIDMin && reportID <= t9ReportIDMax)
		return _ParseT9Contact(msg, msgSize, t9ReportIDMin, timestamp);

	if (hasT100 && reportID >= t100ReportIDMin
		&& reportID <= t100ReportIDMax) {
		return _ParseT100Contact(msg, msgSize, t100ReportIDMin, timestamp);
	}

	if (t19ReportID != 0 && reportID == t19ReportID) {
		bool pressed = !(msg[1] & 0x20);
		TOUCH_TRACE("T19 button=%s\n", pressed ? "pressed" : "released");
		if (pressed == fButtonPressed)
			return B_OK;

		if (pressed) {
			_StartButtonSnapshot(timestamp);
			fButtonPressed = true;
			return _QueueCurrentOutput();
		}

		fButtonPressed = false;
		fClickCandidateValid = false;
		if (fButtonSnapshotActive) {
			fButtonSnapshotActive = false;
			fPrimarySlot = MXT_NO_CONTACT;
			fLastStableValid = false;
			return _QueueNeutral();
		}
		return _QueueCurrentOutput();
	}

	TRACE("engine: unknown msg id=0x%02x\n", (unsigned)reportID);
	return B_OK;
}


status_t
TouchEngine::Flush(mxt_touch_batch* batch)
{
	if (batch == NULL)
		return B_BAD_VALUE;

	mxt_touch_state finalState;
	_BuildOutputState(&finalState, true);
	status_t status = _QueueState(finalState);
	if (status != B_OK)
		return status;

	memset(batch, 0, sizeof(*batch));
	batch->count = fPendingFrameCount;
	for (uint8 i = 0; i < fPendingFrameCount; i++) {
		batch->frames[i] = fPendingFrames[i];
		_TraceState(batch->frames[i]);
	}

	if (!finalState.button) {
		if (finalState.contactCount > 0) {
			fLastStableState = finalState;
			fLastStableTime = fLastMessageTime;
			fLastStableValid = true;
			if (finalState.contactCount >= 2)
				fClickCandidateValid = false;
		} else {
			fLastStableValid = false;
		}
	}

	fPendingFrameCount = 0;
	return B_OK;
}


bool
TouchEngine::ButtonPressed() const
{
	return fButtonPressed;
}


uint8
TouchEngine::_ContactIndex(uint8 reportID, uint8 reportIDMin) const
{
	uint8 index = reportID - reportIDMin;
	return index < MXT_MAX_CONTACTS ? index : MXT_NO_CONTACT;
}


status_t
TouchEngine::_ParseT9Contact(const uint8* msg, size_t msgSize,
	uint8 reportIDMin, bigtime_t timestamp)
{
	if (msgSize < 7)
		return B_OK;

	uint8 index = _ContactIndex(msg[0], reportIDMin);
	if (index == MXT_NO_CONTACT)
		return B_BAD_DATA;

	mxt_contact_state& contact = fContacts[index];
	uint8 flags = msg[1];
	bool detect = (flags & MXT_CONTACT_DETECT) != 0;
	bool restart = detect && (flags & MXT_CONTACT_RELEASE) != 0
		&& contact.active;

	if ((flags & MXT_CONTACT_SUPPRESS) != 0 || !detect || restart) {
		status_t status = _ReleaseContact(index, timestamp);
		if (status != B_OK)
			return status;
	}

	if (!detect || (flags & MXT_CONTACT_SUPPRESS) != 0) {
		TOUCH_TRACE("T9 slot=%u inactive flags=0x%02x\n", index, flags);
		return B_OK;
	}

	uint16 x = (msg[2] << 4) | ((msg[4] >> 4) & 0xf);
	uint16 y = (msg[3] << 4) | (msg[4] & 0xf);
	if (fMaxX < 1024)
		x >>= 2;
	if (fMaxY < 1024)
		y >>= 2;
	if (x > fMaxX)
		x = fMaxX;
	if (y > fMaxY)
		y = fMaxY;

	_ActivateContact(index);
	contact.x = x;
	contact.y = y;
	contact.pressure = msg[5];
	TOUCH_TRACE("T9 slot=%u detected flags=0x%02x x=%u y=%u\n",
		index, flags, x, y);
	return B_OK;
}


status_t
TouchEngine::_ParseT100Contact(const uint8* msg, size_t msgSize,
	uint8 reportIDMin, bigtime_t timestamp)
{
	if (msgSize < 6 || msg[0] < reportIDMin + 2)
		return B_OK;

	uint8 index = _ContactIndex(msg[0] - 2, reportIDMin);
	if (index == MXT_NO_CONTACT)
		return B_BAD_DATA;

	uint8 flags = msg[1];
	uint8 type = (flags & 0x70) >> 4;
	bool accepted = type == MXT_T100_TYPE_FINGER
		|| type == MXT_T100_TYPE_PASSIVE_STYLUS
		|| type == MXT_T100_TYPE_GLOVE;

	if ((flags & MXT_CONTACT_DETECT) == 0 || !accepted)
		return _ReleaseContact(index, timestamp);

	_ActivateContact(index);
	uint16 x = msg[2] | (msg[3] << 8);
	uint16 y = msg[4] | (msg[5] << 8);
	fContacts[index].x = x > fMaxX ? fMaxX : x;
	fContacts[index].y = y > fMaxY ? fMaxY : y;
	fContacts[index].pressure = msgSize >= 8 ? msg[7] : 0;
	return B_OK;
}


status_t
TouchEngine::_ReleaseContact(uint8 index, bigtime_t timestamp)
{
	mxt_contact_state& contact = fContacts[index];
	if (!contact.active)
		return B_OK;

	if (_ActiveContactCount() >= 2) {
		if (fLastStableValid && fLastStableState.contactCount >= 2)
			fClickCandidate = fLastStableState;
		else
			_BuildCurrentContacts(&fClickCandidate, false);
		fClickCandidateTime = timestamp;
		fClickCandidateValid = true;
	}

	contact.active = false;
	if (index != fPrimarySlot)
		return B_OK;

	fPrimarySlot = MXT_NO_CONTACT;
	if (fButtonPressed && fButtonSnapshotActive)
		return B_OK;

	return _QueueNeutral();
}


void
TouchEngine::_ActivateContact(uint8 index)
{
	if (fContacts[index].active)
		return;

	if (_ActiveContactCount() == 0) {
		fClickCandidateValid = false;
		fLastStableValid = false;
	}
	fContacts[index].active = true;
}


void
TouchEngine::_ExpireClickCandidate(bigtime_t timestamp)
{
	if (!fClickCandidateValid || is_recent(timestamp, fClickCandidateTime))
		return;

	fClickCandidateValid = false;
	TOUCH_TRACE("expired click snapshot\n");
}


void
TouchEngine::_StartButtonSnapshot(bigtime_t timestamp)
{
	mxt_touch_state current;
	_BuildCurrentContacts(&current, false);

	const mxt_touch_state* source = NULL;
	if (fClickCandidateValid
		&& fClickCandidate.contactCount >= 2
		&& is_recent(timestamp, fClickCandidateTime)) {
		source = &fClickCandidate;
	} else if (fLastStableValid && fLastStableState.contactCount >= 2
		&& is_recent(timestamp, fLastStableTime)) {
		source = &fLastStableState;
	} else if (current.contactCount >= 2) {
		source = &current;
	}

	fClickCandidateValid = false;
	if (source == NULL) {
		fButtonSnapshotActive = false;
		return;
	}

	fButtonSnapshot = *source;
	fButtonSnapshot.button = true;
	fButtonSnapshotActive = true;
	TOUCH_TRACE("captured %d-finger button snapshot x=%u y=%u\n",
		fButtonSnapshot.contactCount, fButtonSnapshot.x, fButtonSnapshot.y);
}


void
TouchEngine::_BuildOutputState(mxt_touch_state* state, bool commitPrimary)
{
	if (fButtonPressed && fButtonSnapshotActive) {
		*state = fButtonSnapshot;
		state->button = true;
		return;
	}

	_BuildCurrentContacts(state, commitPrimary);
	state->button = fButtonPressed;
}


void
TouchEngine::_BuildCurrentContacts(mxt_touch_state* state, bool commitPrimary)
{
	memset(state, 0, sizeof(*state));

	for (int32 i = 0; i < MXT_MAX_CONTACTS; i++) {
		if (!fContacts[i].active)
			continue;
		state->contactCount++;
		if (i < 8)
			state->fingers |= 1u << i;
	}

	if (state->contactCount == 0)
		return;

	uint8 primary = fPrimarySlot;
	if (primary == MXT_NO_CONTACT || !fContacts[primary].active) {
		for (uint8 i = 0; i < MXT_MAX_CONTACTS; i++) {
			if (fContacts[i].active) {
				primary = i;
				break;
			}
		}
	}

	if (primary == MXT_NO_CONTACT)
		return;

	if (commitPrimary)
		fPrimarySlot = primary;
	state->x = fContacts[primary].x;
	state->y = fContacts[primary].y;
	state->pressure = fContacts[primary].pressure;
}


status_t
TouchEngine::_QueueCurrentOutput()
{
	mxt_touch_state state;
	_BuildOutputState(&state, false);
	return _QueueState(state);
}


status_t
TouchEngine::_QueueNeutral()
{
	mxt_touch_state state;
	memset(&state, 0, sizeof(state));
	state.button = fButtonPressed;
	return _QueueState(state);
}


status_t
TouchEngine::_QueueState(const mxt_touch_state& state)
{
	if (fPendingFrameCount > 0
		&& _StatesEqual(fPendingFrames[fPendingFrameCount - 1], state)) {
		return B_OK;
	}
	if (fPendingFrameCount >= MXT_MAX_FRAME_BATCH)
		return B_BUFFER_OVERFLOW;

	fPendingFrames[fPendingFrameCount++] = state;
	return B_OK;
}


bool
TouchEngine::_StatesEqual(const mxt_touch_state& a,
	const mxt_touch_state& b) const
{
	return a.contactCount == b.contactCount
		&& a.x == b.x && a.y == b.y
		&& a.pressure == b.pressure
		&& a.button == b.button
		&& a.fingers == b.fingers;
}


int32
TouchEngine::_ActiveContactCount() const
{
	int32 count = 0;
	for (int32 i = 0; i < MXT_MAX_CONTACTS; i++) {
		if (fContacts[i].active)
			count++;
	}
	return count;
}


void
TouchEngine::_ClearContacts()
{
	memset(fContacts, 0, sizeof(fContacts));
	fPrimarySlot = MXT_NO_CONTACT;
}


void
TouchEngine::_TraceState(const mxt_touch_state& state)
{
	bool transition = state.fingers != fTraceFingers
		|| state.button != fTraceButton;
	if (state.contactCount == 2)
		fTraceSample++;
	else
		fTraceSample = 0;
	if (transition || (state.contactCount == 2 && (fTraceSample & 0x0f) == 0)) {
		TOUCH_TRACE("frame contacts=%d fingers=0x%02x button=%d"
			" x=%u y=%u pressure=%u%s\n", state.contactCount,
			state.fingers, state.button, state.x, state.y,
			state.pressure, transition ? " transition" : "");
	}
	fTraceFingers = state.fingers;
	fTraceButton = state.button;
}
