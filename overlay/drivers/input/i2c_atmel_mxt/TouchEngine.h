// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with Qwen 3.6
// SPDX-FileContributor: Generated with GitHub Copilot
#ifndef _I2C_ATMEL_MXT_TOUCH_ENGINE_H
#define _I2C_ATMEL_MXT_TOUCH_ENGINE_H


#include <SupportDefs.h>

#define MXT_MAX_CONTACTS		10
#define MXT_MAX_FRAME_BATCH		8
#define MXT_NO_CONTACT			0xff

enum {
	MXT_CONTACT_SUPPRESS	= (1 << 1),
	MXT_CONTACT_RELEASE	= (1 << 5),
	MXT_CONTACT_DETECT	= (1 << 7),
};

enum {
	MXT_T100_TYPE_FINGER			= 1,
	MXT_T100_TYPE_PASSIVE_STYLUS	= 2,
	MXT_T100_TYPE_HOVER_FINGER		= 4,
	MXT_T100_TYPE_GLOVE				= 5,
	MXT_T100_TYPE_LARGE				= 6,
};


struct mxt_contact_state {
	uint16		x;
	uint16		y;
	uint8		pressure;
	bool		active;
};


struct mxt_touch_state {
	int			contactCount;
	uint16		x;
	uint16		y;
	uint8		pressure;
	bool		button;
	uint8		fingers;
};


struct mxt_touch_batch {
	uint8				count;
	mxt_touch_state		frames[MXT_MAX_FRAME_BATCH];
};


// Applies ordered controller slot deltas, then adapts them to Haiku's single
// representative-point touchpad protocol.
class TouchEngine {
public:
								TouchEngine();

			void			SetResolution(uint16 maxX, uint16 maxY);
			void			Reset();

			status_t		ProcessMessage(const uint8* msg, size_t msgSize,
									uint8 t9ReportIDMin,
									uint8 t9ReportIDMax,
									uint8 t100ReportIDMin,
									uint8 t100ReportIDMax,
									uint8 t19ReportID,
									bool hasT9,
									bool hasT100,
									bigtime_t timestamp);

			status_t		Flush(mxt_touch_batch* batch);
			bool			ButtonPressed() const;

private:
			uint8			_ContactIndex(uint8 reportID,
									uint8 reportIDMin) const;
			status_t		_ParseT9Contact(const uint8* msg, size_t msgSize,
									uint8 reportIDMin,
									bigtime_t timestamp);
			status_t		_ParseT100Contact(const uint8* msg, size_t msgSize,
									uint8 reportIDMin,
									bigtime_t timestamp);

			status_t		_ReleaseContact(uint8 index, bigtime_t timestamp);
			void			_ActivateContact(uint8 index);
			void			_ExpireClickCandidate(bigtime_t timestamp);
			void			_StartButtonSnapshot(bigtime_t timestamp);
			void			_BuildOutputState(mxt_touch_state* state,
									bool commitPrimary);
			void			_BuildCurrentContacts(mxt_touch_state* state,
									bool commitPrimary);
			status_t		_QueueCurrentOutput();
			status_t		_QueueNeutral();
			status_t		_QueueState(const mxt_touch_state& state);
			bool			_StatesEqual(const mxt_touch_state& a,
									const mxt_touch_state& b) const;
			int32			_ActiveContactCount() const;
			void			_ClearContacts();
			void			_TraceState(const mxt_touch_state& state);

private:
			mxt_contact_state		fContacts[MXT_MAX_CONTACTS];
			uint8				fPrimarySlot;

			uint16			fMaxX;
			uint16			fMaxY;

			bool				fButtonPressed;
			bool				fButtonSnapshotActive;
			mxt_touch_state		fButtonSnapshot;

			bool				fLastStableValid;
			bigtime_t			fLastStableTime;
			mxt_touch_state		fLastStableState;

			bool				fClickCandidateValid;
			bigtime_t			fClickCandidateTime;
			mxt_touch_state		fClickCandidate;

			bigtime_t			fLastMessageTime;

			uint8				fPendingFrameCount;
			mxt_touch_state		fPendingFrames[MXT_MAX_FRAME_BATCH];

			uint8				fTraceFingers;
			bool				fTraceButton;
			uint8				fTraceSample;
};


#endif	// _I2C_ATMEL_MXT_TOUCH_ENGINE_H
