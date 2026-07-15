// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with Qwen 3.6
// SPDX-FileContributor: Generated with GitHub Copilot
#ifndef _I2C_ATMEL_MXT_DEVICE_H
#define _I2C_ATMEL_MXT_DEVICE_H


#include <ACPI.h>
#include <condition_variable.h>
#include <i2c.h>
#include <keyboard_mouse_driver.h>
#include <lock.h>

#include "ObjectTable.h"
#include "TouchEngine.h"

#define TRANSFER_BUFFER_SIZE	128
#define MXT_MSG_SIZE			64


// Object types
#define MXT_OBJECT_TYPE_T5		5		// Message Processor
#define MXT_OBJECT_TYPE_T6		6		// Command Processor
#define MXT_OBJECT_TYPE_T7		7		// Power Configuration
#define MXT_OBJECT_TYPE_T9		9		// Multi-Touch Touchscreen (legacy)
#define MXT_OBJECT_TYPE_T18		18	// Communications Config
#define MXT_OBJECT_TYPE_T19		19	// GPIO/PWM Support
#define MXT_OBJECT_TYPE_T44		44	// Message Count
#define MXT_OBJECT_TYPE_T100	100	// Multi-Touch Touchscreen (modern)

// T6 commands
#define MXT_T6_CMD_SOFT_RESET	0x01

// Report ID terminator
#define MXT_REPORT_ID_INVALID	0xFF

// Default power config values
// Active = measurement interval in ms: round(1000 / target_rate_hz)
// 8 ms → 125 Hz, matching the input server's fixed poll rate.
#define MXT_DEFAULT_T7_ACTIVE	8
#define MXT_DEFAULT_T7_IDLE		100

// T9 (Multi-Touch) status flags
#define MXT_T9_UNGRIP		(1 << 0)
#define MXT_T9_SUPPRESS		(1 << 1)
#define MXT_T9_AMP		(1 << 2)
#define MXT_T9_VECTOR		(1 << 3)
#define MXT_T9_MOVE		(1 << 4)
#define MXT_T9_RELEASE	(1 << 5)
#define MXT_T9_PRESS	(1 << 6)
#define MXT_T9_DETECT		(1 << 7)

// Single contact data extracted from a T9 or T100 message
struct mxt_contact {
	uint16	x;
	uint16	y;
	uint8		area;
	uint8		amplitude;
	bool		detect;
	bool		press;
};


// SPSC ring buffer. MutexLocker protects all buffer access and makes the empty
// check plus condition-variable wait atomic with respect to writers.
#define MXT_RING_CAPACITY	16

class MxtRingBuffer {
public:
								MxtRingBuffer();
			void				Write(const mxt_touch_state& state);
			status_t		ReadOrWait(mxt_touch_state& state);

private:
			mxt_touch_state		fSamples[MXT_RING_CAPACITY];
			int32			fReadPos;
			int32			fWritePos;
			mutex			fMutex;
			ConditionVariable	fDataCV;
};





class MxtDevice {
public:
								MxtDevice(device_node* parent,
									i2c_device_interface* i2c,
									i2c_device i2cCookie,
									acpi_module_info* acpi,
									acpi_handle acpiHandle);
								~MxtDevice();

			status_t			InitCheck() const { return fStatus; }

			bool				IsOpen() const { return fOpenCount > 0; }
			status_t			Open(uint32 flags);
			status_t			Close();
			int32				OpenCount() const { return fOpenCount; }

			void				Removed();
			bool				IsRemoved() const;

			void				SetPublishPath(char *publishPath);
			const char *		PublishPath() { return fPublishPath; }
			status_t			Control(uint32 op, void *buffer, size_t length);
			device_node*		Parent() { return fParent; }

			// Full initialization sequence (called from Driver.cpp)
			status_t			Initialize();
			void				Uninitialize();

private:
			// I2C transport
			status_t			_ExecCommand(i2c_op op, uint8* cmd,
									size_t cmdLength, void* buffer,
									size_t bufferLength);
			status_t			_FetchBuffer(uint8* cmd, size_t cmdLength,
									void* buffer, size_t bufferLength);
			void				_EncodeAddr(uint16 addr, uint8* out);

			// OBP discovery
			status_t			_ReadInfoBlock();
			status_t			_ParseObjectTable();
			status_t			_ReadTouchResolution();
			uint8				_ReportIDMinAt(size_t index) const;
			uint8				_ReportIDMaxAt(size_t index) const;

			// Configuration
			status_t			_WriteObjectVerify(uint16 addr,
									const uint8* data, size_t len,
									const char* label);
			status_t			_InitializePowerConfig();
			status_t			_WriteEssentialConfig();
			status_t			_ApplyDefaultConfig();

			// Message queue — read and aggregate
			status_t			_DrainInitMessages();
			status_t			_DrainAndAggregate(mxt_touch_batch& batch);
			status_t			_ProcessMessage(const uint8* msg, size_t msgSize,
									mxt_touch_state& state);
			status_t			_ParseT9Message(const uint8* msg, size_t msgSize,
									mxt_touch_state& state);
			status_t			_ParseT100Message(const uint8* msg, size_t msgSize,
									mxt_touch_state& state);

			// Interrupt / worker thread
			static int32		_MxtInterrupt(void* data);
			int32				_MxtInterruptInt();
			static int32		_WorkerThread(void* data);
			int32				_WorkerThreadInt();
			status_t			_StartWorker();
			void				_StopWorker();

private:
			status_t			fStatus;

			int32				fOpenCount;
			bool				fRemoved;

			char *				fPublishPath;

			device_node*			fParent;

			i2c_device_interface*	fI2C;
			i2c_device			fI2CCookie;

			// ACPI — for IRQ discovery via _CRS walk
			acpi_module_info*		fACPI;
			acpi_handle			fACPIHandle;

			// Interrupt (from ACPI _CRS)
			uint8				fIRQ;
			uint32				fIRQFlags;
			bool				fIRQInstalled;

			touchpad_specs			fHardwareSpecs;

			// OBP state
			ObjectTable			fObjectTable;
			bool				fHasT9;
			bool				fHasT100;

			// T9 (legacy multitouch)
			uint16				fT9Address;
			uint8				fT9ReportIDMin;
			uint8				fT9ReportIDMax;
			uint8				fT9Orientation;

			// T100 (modern multitouch)
			uint16				fT100Address;
			uint8				fT100ReportIDMin;
			uint8				fT100ReportIDMax;

			// Message queue
			uint16				fT5Address;
			uint8				fT5MsgSize;
			uint16				fT44Address;

			// Command / power / comm
			uint16				fT6Address;
			uint16				fT7Address;
			uint8				fT7Idle;
			uint8				fT7Active;
			uint16				fT18Address;
			uint8				fT19ReportID;

			// Touch resolution (from T9/T100 range registers)
			uint16				fTouchXRange;
			uint16				fTouchYRange;
			uint16				fTouchXSize;
			uint16				fTouchYSize;

			// ISR → worker: condvar pulse wakes drain thread
			ConditionVariable		fEventCV;

			// Worker thread — ISR-scheduled I2C pump
			thread_id			fWorkerThread;

			// Ring buffer — worker writes, input server drains
			MxtRingBuffer			fBuffer;

			// Stateful touch engine — contact tracking, gesture detection
			TouchEngine*			fTouchEngine;
};


#endif	// _I2C_ATMEL_MXT_DEVICE_H
