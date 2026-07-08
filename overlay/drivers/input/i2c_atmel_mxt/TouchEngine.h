/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _I2C_ATMEL_MXT_TOUCH_ENGINE_H
#define _I2C_ATMEL_MXT_TOUCH_ENGINE_H


#include <SupportDefs.h>

#include "MxtDevice.h"


// Maximum tracked contacts (matches T9/T100 instance counts on WINKY)
#define MXT_MAX_CONTACTS	10

// Tap detection: max movement delta per contact before a tap is cancelled.
// Slightly generous to account for 12-bit coordinate noise; the input server
// uses 15 pixels for its own tap gate, so we stay compatible.
#define MXT_TAP_MAX_DELTA	20

// T9/T100 message flags (mirrored from MxtDevice.h for engine use)
enum {
	MXT_CONTACT_PRESS	= (1 << 6),
	MXT_CONTACT_RELEASE	= (1 << 5),
	MXT_CONTACT_MOVE	= (1 << 4),
	MXT_CONTACT_DETECT	= (1 << 7),
};


// Per-contact state tracked across drain cycles.
struct mxt_contact_state {
	uint16		initialX;		// X at PRESS (tap origin)
	uint16		initialY;		// Y at PRESS
	uint16		x;			// latest position
	uint16		y;			// latest position
	uint8		pressure;			// latest area/pressure
	bool		active;			// currently detected
	bool		press;			// PRESS flag seen this cycle
	bool		release;		// RELEASE flag seen this cycle
	uint16		deltaX;			// movement since initial touch
	uint16		deltaY;			// movement since initial touch
};


// Stateful touch engine. Parses raw maXTouch messages, maintains a contact
// table and persistent button state, and classifies gestures (move, tap,
// two-finger tap) into well-formed mxt_touch_state events for the ring buffer.
class TouchEngine {
public:
								TouchEngine();

			// Configure touch resolution (from T9/T100 range registers).
			// Called once during device initialization before messages arrive.
			void			SetResolution(uint16 maxX, uint16 maxY);

			// Process a single OBP message. Dispatch by report ID.
			status_t		ProcessMessage(const uint8* msg, size_t msgSize,
									uint8 t9ReportIDMin,
									uint8 t9ReportIDMax,
									uint8 t100ReportIDMin,
									uint8 t100ReportIDMax,
									uint8 t19ReportID,
									bool hasT9,
									bool hasT100);

			// Flush engine state into one mxt_touch_state. Call once per
			// drain cycle after all messages are processed.
			status_t		Flush(mxt_touch_state* state);

			// Return the current persistent button state (for stanza tracking).
			bool		ButtonPressed() const;

private:
			// T9/T100 contact index from report ID.
			uint8			_ContactIndex(uint8 reportID,
									uint8 reportIDMin) const;

			// Parse a T9 (legacy multitouch) message into contact table.
			status_t		_ParseT9Contact(const uint8* msg, size_t msgSize,
									uint8 reportIDMin);

			// Parse a T100 (modern multitouch) message into contact table.
			status_t		_ParseT100Contact(const uint8* msg, size_t msgSize,
										uint8 reportIDMin);

			// Classify two-finger tap gesture. Returns true if both contacts
			// lifted with minimal movement, producing a right-click event.
			bool			_DetectTwoFingerTap(mxt_touch_state* state);

private:
			mxt_contact_state		fContacts[MXT_MAX_CONTACTS];
			int32				fActiveCount;

			// Touch resolution (from device range registers)
			uint16			fMaxX;
			uint16			fMaxY;

			// Persistent T19 GPIO button state (active-low).
			// Updated by T19 messages; carried forward across drain cycles.
			uint8				fLastButtonGpio;
			bool			fHasButtonState;
};


#endif	// _I2C_ATMEL_MXT_TOUCH_ENGINE_H
