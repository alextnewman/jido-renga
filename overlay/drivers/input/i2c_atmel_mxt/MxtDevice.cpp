// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with Qwen 3.6
// SPDX-FileContributor: Generated with GitHub Copilot

//! Driver for Atmel maXTouch touch controllers.
// Based on Object-Based Protocol (OBP) specification for WINKY Chromebook 2


#include "Driver.h"
#include "MxtDevice.h"
#include "TouchEngine.h"

#include <arch/int.h>
#include <kernel.h>
#include <util/AutoLock.h>

#include <new>
#include <stdlib.h>
#include <string.h>

// ACPICA full struct definitions (for ACPI_RESOURCE, ACPI_RESOURCE_TYPE_*, etc.)
#include "acpi.h"


// I2C bus guard that releases the bus on every exit path.
struct I2cBusGuard {
	i2c_device_interface*	bus;
	i2c_device			cookie;
	bool				acquired;

	I2cBusGuard(i2c_device_interface* b, i2c_device c)
		: bus(b), cookie(c), acquired(false) {}

	status_t Acquire()
	{
		status_t status = bus->acquire_bus(cookie);
		if (status == B_OK)
			acquired = true;
		return status;
	}

	~I2cBusGuard()
	{
		if (acquired)
			bus->release_bus(cookie);
	}
};


// ACPI _CRS walk context
struct mxt_crs_context {
	uint8		irq;
	uint32		irqFlags;
};


static acpi_status
_mxt_crs_callback(acpi_resource* res, void* context)
{
	mxt_crs_context* crs = static_cast<mxt_crs_context*>(context);

	if (res->Type == ACPI_RESOURCE_TYPE_EXTENDED_IRQ) {
		crs->irq = res->Data.ExtendedIrq.Interrupts[0];
		crs->irqFlags = 0;
		if (res->Data.ExtendedIrq.Triggering == 1)
			crs->irqFlags |= B_EDGE_TRIGGERED;
		else
			crs->irqFlags |= B_LEVEL_TRIGGERED;
		if (res->Data.ExtendedIrq.Polarity == 0)
			crs->irqFlags |= B_HIGH_ACTIVE_POLARITY;
		else
			crs->irqFlags |= B_LOW_ACTIVE_POLARITY;
	} else if (res->Type == ACPI_RESOURCE_TYPE_IRQ) {
		crs->irq = res->Data.Irq.Interrupts[0];
		crs->irqFlags = 0;
		if (res->Data.Irq.Triggering == 1)
			crs->irqFlags |= B_EDGE_TRIGGERED;
		else
			crs->irqFlags |= B_LEVEL_TRIGGERED;
		if (res->Data.Irq.Polarity == 0)
			crs->irqFlags |= B_HIGH_ACTIVE_POLARITY;
		else
			crs->irqFlags |= B_LOW_ACTIVE_POLARITY;
	}
	return AE_OK;
}



// ----------------------------------------------------------------#
// MxtRingBuffer -- SPSC ring, MutexLocker protects all access
// ----------------------------------------------------------------#

MxtRingBuffer::MxtRingBuffer()
	:	fReadPos(0),
		fWritePos(0)
{
	mutex_init(&fMutex, "MxtRing lock");
	fDataCV.Init(this, "MxtRingBuffer");
}


void
MxtRingBuffer::Write(const mxt_touch_state& state)
{
	{
		MutexLocker lock(&fMutex);

		int next = (fWritePos + 1) % MXT_RING_CAPACITY;
		if (next == fReadPos) {
			// Ring full — advance read pos to drop oldest sample.
			fReadPos = (fReadPos + 1) % MXT_RING_CAPACITY;
		}

		fSamples[fWritePos] = state;
		fWritePos = next;

		// Pulse on every write — reader may have missed a prior pulse
		// and will go back to sleep if it finds the ring empty.
		fDataCV.NotifyOne();
	}
}


status_t
MxtRingBuffer::Read(mxt_touch_state& state)
{
	MutexLocker lock(&fMutex);

	if (fReadPos == fWritePos)
		return B_TIMED_OUT;

	// Return one sample in FIFO order. With T7 active set to 8 ms
	// (125 Hz), the device produces events at the same rate the input
	// server consumes them — one sample per poll, no backlog.
	state = fSamples[fReadPos];
	fReadPos = (fReadPos + 1) % MXT_RING_CAPACITY;

	return B_OK;
}


status_t
MxtRingBuffer::ReadOrWait(mxt_touch_state& state)
{
	// Try non-blocking read first (mutex inside Read).
	status_t result = Read(state);
	if (result == B_OK)
		return result;

	// Ring was empty — endless cancelable wait for writer notification.
	// CV uses its own internal lock, no mutex needed here.
	fDataCV.Wait();

	// Notification received — try read again under mutex.
	return Read(state);
}

MxtDevice::MxtDevice(device_node* parent, i2c_device_interface* i2c,
	i2c_device i2cCookie, acpi_module_info* acpi, acpi_handle acpiHandle)
	:	fStatus(B_NO_INIT),
		fOpenCount(0),
		fRemoved(false),
		fPublishPath(NULL),
		fParent(parent),
		fI2C(i2c),
		fI2CCookie(i2cCookie),
		fACPI(acpi),
		fACPIHandle(acpiHandle),
		fIRQ(0),
		fIRQFlags(0),
		fIRQInstalled(false),
		fHasT9(false),
		fHasT100(false),
		fT9Address(0),
		fT9ReportIDMin(0),
		fT9ReportIDMax(0),
		fT9Orientation(0),
		fT100Address(0),
		fT100ReportIDMin(0),
		fT100ReportIDMax(0),
		fT5Address(0),
		fT5MsgSize(0),
		fT44Address(0),
		fT6Address(0),
		fT7Address(0),
		fT7Idle(MXT_DEFAULT_T7_IDLE),
		fT7Active(MXT_DEFAULT_T7_ACTIVE),
		fT18Address(0),
		fT19ReportID(0),
		fTouchXRange(0),
		fTouchYRange(0),
		fTouchXSize(0),
		fTouchYSize(0),
		fEventCV(),
		fWorkerThread(B_BAD_VALUE),
		fTouchEngine(NULL)
{
	TRACE("Creating MXT device\n");

	fEventCV.Init(this, "MxtDevice");

	// Initialize hardware specs defaults
	fHardwareSpecs.areaStartX = 0;
	fHardwareSpecs.areaStartY = 0;
	fHardwareSpecs.edgeMotionWidth = 55;
	fHardwareSpecs.minPressure = 0;
	fHardwareSpecs.realMaxPressure = 255;
	fHardwareSpecs.maxPressure = 255;

	fStatus = B_OK;
}


MxtDevice::~MxtDevice()
{
	Uninitialize();
	free(fPublishPath);
}


void
MxtDevice::Uninitialize()
{
	// Stop worker before removing IRQ — worker may still be draining.
	_StopWorker();

	delete fTouchEngine;
	fTouchEngine = NULL;

	if (fIRQInstalled)
		remove_io_interrupt_handler(fIRQ, _MxtInterrupt, this);
	fIRQInstalled = false;
}


// ----------------------------------------------------------------#
// I/O control — input server protocol
// ----------------------------------------------------------------#

void
MxtDevice::SetPublishPath(char* publishPath)
{
	free(fPublishPath);
	fPublishPath = publishPath;
}


// Convert aggregated touch state to Haiku touchpad_movement
static void
_StateToMovement(const mxt_touch_state& state, touchpad_movement* info)
{
	memset(info, 0, sizeof(*info));
	info->xPosition = state.x;
	info->yPosition = state.y;
	info->zPressure = state.pressure;

	// Button mapping: the engine sets state.button for both physical T19 GPIO
	// presses and two-finger tap gestures. The input server's
	// _ClickFingerButtonEmulator converts buttons + finger count into the
	// correct button identity (left, right, middle).
	if (state.button)
		info->buttons = 1;

	// Finger bitmask: preserved directly from engine output.
	// The engine sets fingers = 0x03 on two-finger tap lift events so the
	// input server emulator can classify the gesture as a right click.
	info->fingers = state.fingers;

	// fingerWidth: legacy Synaptics convention used as fallback finger count.
	// 4-7 = normal width (one finger), 0 = two fingers, 1 = three or more.
	if (state.contactCount <= 1) {
		info->fingerWidth = 4;
	} else if (state.contactCount == 2) {
		info->fingerWidth = 0;
	} else {
		info->fingerWidth = 1;
	}

	TRACE("Touch: contacts=%d, x=%" B_PRIu32 " y=%" B_PRIu32
		" btn=%u, fingers=0x%02x, pressure=%" B_PRIu8 "\n",
		state.contactCount, info->xPosition, info->yPosition,
		info->buttons, info->fingers, (uint32)info->zPressure);
}


status_t
MxtDevice::Control(uint32 op, void* buffer, size_t length)
{
	switch (op) {
		case B_GET_DEVICE_NAME:
		{
			if (!IS_USER_ADDRESS(buffer))
				return B_BAD_ADDRESS;

			if (user_strlcpy((char*)buffer, "Atmel maXTouch Touchpad", length) > 0)
				return B_OK;

			return B_ERROR;
		}

		case MS_IS_TOUCHPAD:
			TRACE("MS_IS_TOUCHPAD\n");
			if (buffer == NULL)
				return B_OK;
			TRACE_ALWAYS("MS_IS_TOUCHPAD: areaEndX=%u, areaEndY=%u\n",
				fHardwareSpecs.areaEndX, fHardwareSpecs.areaEndY);
			return user_memcpy(buffer, &fHardwareSpecs, sizeof(fHardwareSpecs));

		case MS_READ_TOUCHPAD:
		{
			touchpad_read read;
			memset(&read, 0, sizeof(read));

			if (length < sizeof(touchpad_read))
				return B_BUFFER_OVERFLOW;

			if (user_memcpy(&read.timeout, &(((touchpad_read*)buffer)->timeout),
					sizeof(bigtime_t)) != B_OK)
				return B_BAD_ADDRESS;

			read.event = MS_READ_TOUCHPAD;

			// Return one worker-produced state, blocking until the ring has data.
			mxt_touch_state state;
			status_t result = fBuffer.ReadOrWait(state);
			if (result == B_OK) {
				_StateToMovement(state, &read.u.touchpad);
				if (!IS_USER_ADDRESS(buffer)
					|| user_memcpy(buffer, &read, sizeof(read)) != B_OK) {
					return B_BAD_ADDRESS;
				}
			}
			return result;
		}
	}

	return B_ERROR;
}


status_t
MxtDevice::Open(uint32 flags)
{
	atomic_add(&fOpenCount, 1);
	return B_OK;
}


status_t
MxtDevice::Close()
{
	atomic_add(&fOpenCount, -1);
	return B_OK;
}


void
MxtDevice::Removed()
{
	fRemoved = true;
}


bool
MxtDevice::IsRemoved() const
{
	return fRemoved;
}


// ----------------------------------------------------------------#
// Interrupt handler — ISR context, no blocking
// ----------------------------------------------------------------#

int32
MxtDevice::_MxtInterrupt(void* data)
{
	MxtDevice* device = static_cast<MxtDevice*>(data);
	return device->_MxtInterruptInt();
}


int32
MxtDevice::_MxtInterruptInt()
{
	// A level-triggered IRQ remains asserted until the worker drains T44/T5.
	fEventCV.NotifyOne();
	return B_HANDLED_INTERRUPT;
}


// ----------------------------------------------------------------#
// Drain one T44 cycle: read count + messages, feed touch engine
// Returns B_OK with state populated (may be empty if queue drained).
// Called from worker thread context (NOT ISR).
// ----------------------------------------------------------------#

status_t
MxtDevice::_DrainAndAggregate(mxt_touch_state& state)
{
	memset(&state, 0, sizeof(state));

	// Hold the I2C bus for the complete queue drain.
	I2cBusGuard guard(fI2C, fI2CCookie);
	if (guard.Acquire() != B_OK) {
		ERROR("drain: acquire_bus failed\n");
		return B_ERROR;
	}

	// Per spec §9.4: T44 sits immediately before T5, so a single
	// burst from T44 returns count byte + first message.
	uint8 addr[2];
	_EncodeAddr(fT44Address, addr);
	size_t readSize = 1 + fT5MsgSize;

	// Write phase: set I2C pointer to T44
	status_t status = fI2C->exec_command(fI2CCookie, I2C_OP_WRITE_STOP,
		addr, sizeof(addr), NULL, 0);
	if (status != B_OK) {
		ERROR("drain: T44 write failed: %s\n", strerror(status));
		return status;
	}
	snooze(50);

	// Read phase: count + first message in one burst
	uint8 countMsg[TRANSFER_BUFFER_SIZE];
	status = fI2C->exec_command(fI2CCookie, I2C_OP_READ_STOP,
		NULL, 0, countMsg, readSize);
	if (status != B_OK) {
		ERROR("drain: T44 read failed: %s\n", strerror(status));
		return status;
	}

	uint8 count = countMsg[0];
	if (count == 0) {
		// Queue empty — IRQ line will deassert. Signal worker to stop
		// the inner drain loop and go back to waiting on the CV.
		return B_BAD_DATA;
	}
	if (count >= 0xFF) {
		// Overflow — hardware dropped old messages. Nothing useful this cycle.
		ERROR("drain: T44 count=0xFF (overflow)\n");
		return B_BAD_DATA;
	}

	TRACE("drain: T44 count=%u, msgSize=%" B_PRIuSIZE "\n", count, readSize);

	// Feed all messages through the touch engine for stateful processing.
	// The engine tracks individual contacts, persistent button state,
	// and classifies gestures (move, tap, two-finger tap).
	if (fTouchEngine != NULL) {
		// Process first message (already in countMsg[1..msgSize])
		fTouchEngine->ProcessMessage(countMsg + 1, fT5MsgSize,
			fT9ReportIDMin, fT9ReportIDMax,
			fT100ReportIDMin, fT100ReportIDMax,
			fT19ReportID, fHasT9, fHasT100);

		// Read remaining messages (device auto-increments pointer)
		uint8 msgBuf[MXT_MSG_SIZE];
		for (uint8 i = 1; i < count; i++) {
			status = fI2C->exec_command(fI2CCookie, I2C_OP_READ_STOP,
				NULL, 0, msgBuf, fT5MsgSize);
			if (status == B_OK)
				fTouchEngine->ProcessMessage(msgBuf, fT5MsgSize,
					fT9ReportIDMin, fT9ReportIDMax,
					fT100ReportIDMin, fT100ReportIDMax,
					fT19ReportID, fHasT9, fHasT100);
		}

		// Flush engine state into one aggregated touch state
		fTouchEngine->Flush(&state);
		TRACE("drain: engine flush contacts=%d, btn=%d, fingers=0x%02x\n",
			state.contactCount, state.button, state.fingers);
	} else {
		// Fallback: legacy aggregate path (no gesture engine)
		_ProcessMessage(countMsg + 1, fT5MsgSize, state);

		uint8 msgBuf[MXT_MSG_SIZE];
		for (uint8 i = 1; i < count; i++) {
			status = fI2C->exec_command(fI2CCookie, I2C_OP_READ_STOP,
				NULL, 0, msgBuf, fT5MsgSize);
			if (status == B_OK)
				_ProcessMessage(msgBuf, fT5MsgSize, state);
		}

		if (state.contactCount > 0) {
			state.x /= state.contactCount;
			state.y /= state.contactCount;
			state.pressure /= state.contactCount;
			state.fingers = (uint8)((1u << MIN(state.contactCount, 32)) - 1);
		}
	}

	// Apply orientation transform (spec Section 11.1), then convert to
	// screen space.  The device reports Y=0 at its "top" (ACPI header /
	// display hinge).  Because the touchpad sits below the keyboard, the
	// device's Y=0 is at the screen bottom --- so we flip Y after the
	// orientation transform to get display coordinates.
	if (state.contactCount > 0 || state.fingers != 0) {
		if (fHasT9) {
			if (fT9Orientation & 0x01) {
				uint16 tmp = state.x;
				state.x = state.y;
				state.y = tmp;
			}
			if (fT9Orientation & 0x02)
				state.x = fTouchXRange - state.x;
			if (fT9Orientation & 0x04)
				state.y = fTouchYRange - state.y;
		}

		// Screen-space conversion: device Y=0 is at the hinge (screen bottom)
		if (state.contactCount > 0)
			state.y = fTouchYRange - state.y;
	}

	return B_OK;
}


// ----------------------------------------------------------------#
// Worker thread — ISR-scheduled I2C pump
// ----------------------------------------------------------------#

int32
MxtDevice::_WorkerThread(void* data)
{
	MxtDevice* device = static_cast<MxtDevice*>(data);
	return device->_WorkerThreadInt();
}


int32
MxtDevice::_WorkerThreadInt()
{
	// A stanza-ending zero event resets the input server's position tracker.
	bool inTouchStanza = false;

	while (!IsRemoved()) {
		// Duplicate ISR wakes may coalesce while the queue is being drained.
		fEventCV.Wait();

		// Drain until T44 reports an empty queue and the IRQ deasserts.
		do {
			mxt_touch_state state;
			status_t result = _DrainAndAggregate(state);
			if (result == B_BAD_DATA) {
				break;
			}
			if (result != B_OK) {
				ERROR("worker: drain error: %s\n", strerror(result));
				break;
			}

			if (state.contactCount > 0) {
				fBuffer.Write(state);
				inTouchStanza = true;
			} else if (inTouchStanza) {
				// Emit exactly one stanza-ending zero-contact event.
				fBuffer.Write(state);
				inTouchStanza = false;
			} else if (state.button) {
				// Physical clickpad presses remain valid without contact data.
				fBuffer.Write(state);
			}
			// Suppress redundant zero-contact states between stanzas.
		} while (!IsRemoved());
	}

	return B_OK;
}


status_t
MxtDevice::_StartWorker()
{
	fWorkerThread = spawn_kernel_thread(_WorkerThread,
		"MXT worker", B_NORMAL_PRIORITY, this);
	if (fWorkerThread < B_OK) {
		ERROR("spawn_kernel_thread failed: %s\n", strerror(fWorkerThread));
		return fWorkerThread;
	}
	resume_thread(fWorkerThread);
	return B_OK;
}


void
MxtDevice::_StopWorker()
{
	if (fWorkerThread >= B_OK) {
		// Wake the worker so it can see IsRemoved() and exit.
		fEventCV.NotifyOne();
		wait_for_thread(fWorkerThread, NULL);
		fWorkerThread = B_BAD_VALUE;
	}
}


// ----------------------------------------------------------------#
// I2C transport
// ----------------------------------------------------------------#

status_t
MxtDevice::_ExecCommand(i2c_op op, uint8* cmd, size_t cmdLength, void* buffer,
	size_t bufferLength)
{
	status_t status = fI2C->acquire_bus(fI2CCookie);
	if (status != B_OK)
		return status;

	status = fI2C->exec_command(fI2CCookie, op, cmd, cmdLength, buffer,
		bufferLength);
	fI2C->release_bus(fI2CCookie);
	return status;
}


status_t
MxtDevice::_FetchBuffer(uint8* cmd, size_t cmdLength, void* buffer,
	size_t bufferLength)
{
	if (bufferLength == 0)
		return B_BAD_VALUE;

	// BayTrail DesignWare I2C: separate write (set address pointer) and read.
	status_t status = _ExecCommand(I2C_OP_WRITE_STOP, cmd, cmdLength,
		NULL, 0);
	if (status != B_OK)
		return status;

	snooze(50);	// 50us for device to latch register address

	status = _ExecCommand(I2C_OP_READ_STOP, NULL, 0, buffer, bufferLength);
	if (status != B_OK)
		return status;

	if (bufferLength == 1) {
		TRACE("_FetchBuffer addr 0x%02x%02x: read byte=0x%02x\n",
			cmd[0], cmd[1], static_cast<uint8*>(buffer)[0]);
	}

	return B_OK;
}


void
MxtDevice::_EncodeAddr(uint16 addr, uint8* out)
{
	out[0] = addr & 0xff;		// little-endian on wire
	out[1] = addr >> 8;
}


// ----------------------------------------------------------------#
// OBP discovery — info block and object table
// ----------------------------------------------------------------#

status_t
MxtDevice::_ReadInfoBlock()
{
	uint8 cmd[2] = { 0x00, 0x00 };	// Address 0x0000
	uint8 data[7];

	status_t status = _FetchBuffer(cmd, sizeof(cmd), data, sizeof(data));
	if (status != B_OK) {
		ERROR("failed to read info block\n");
		return status;
	}

	TRACE_ALWAYS("Info Block: family=0x%02x, variant=0x%02x, "
		"version=%u.%u, build=%u\n",
		data[0], data[1],
		(data[2] >> 4) & 0x0F, data[2] & 0x0F, data[3]);
	TRACE_ALWAYS("Info Block: xSize=%u, ySize=%u, numObjects=%u\n",
		data[4], data[5], data[6]);

	return B_OK;
}


uint8
MxtDevice::_ReportIDMinAt(size_t index) const
{
	uint8 minID = 1;
	for (size_t j = 0; j < index; j++) {
		const mxt_object_entry* obj = fObjectTable.ObjectAt(j);
		if (obj && obj->numReportIDs > 0)
			minID += obj->numReportIDs * obj->instances;
	}
	return minID;
}


uint8
MxtDevice::_ReportIDMaxAt(size_t index) const
{
	const mxt_object_entry* obj = fObjectTable.ObjectAt(index);
	if (!obj || obj->numReportIDs == 0)
		return 0;

	uint8 minID = _ReportIDMinAt(index);
	return minID + obj->numReportIDs * obj->instances - 1;
}


status_t
MxtDevice::_ParseObjectTable()
{
	// Read object count from info block offset 6
	uint8 encoded[2];
	_EncodeAddr(0x0006, encoded);
	uint8 data[1];

	snooze(500);	// settle after info block read

	status_t status = _FetchBuffer(encoded, sizeof(encoded), data, sizeof(data));
	if (status != B_OK) {
		ERROR("failed to read object count: %s\n", strerror(status));
		return status;
	}

	uint8 numObjects = data[0];
	TRACE("_ParseObjectTable: numObjects=%u\n", numObjects);

	if (numObjects == 0) {
		ERROR("object count is zero\n");
		return B_ERROR;
	}

	// Read full object table (6 bytes per entry, starts at 0x0007)
	size_t tableSize = numObjects * 6;
	uint8 encoded2[2];
	_EncodeAddr(0x0007, encoded2);

	uint8* tableData = new(std::nothrow) uint8[tableSize];
	if (tableData == NULL)
		return B_NO_MEMORY;

	status = _FetchBuffer(encoded2, sizeof(encoded2), tableData, tableSize);
	if (status != B_OK) {
		ERROR("failed to read object table\n");
		delete[] tableData;
		return status;
	}

	fObjectTable.Initialize(tableData, tableSize);

	// Trace at most the first 48 bytes of the object table.
	TRACE("Object Table raw (%" B_PRIu32 " bytes): ", (uint32)tableSize);
	for (uint32 i = 0; i < tableSize && i < 48; i++) {
		if (i % 12 == 0)
			dprintf("\n");
		dprintf("0x%02x ", tableData[i]);
	}
	dprintf("\n");

	delete[] tableData;

	// Cache addresses and report ID ranges for objects we care about
	for (size_t i = 0; i < fObjectTable.CountObjects(); i++) {
		const mxt_object_entry& obj = *fObjectTable.ObjectAt(i);

		switch (obj.type) {
			case MXT_OBJECT_TYPE_T5: {
				fT5Address = (uint16)obj.startAddress;
				// Message size excludes checksum byte (CRC disabled on this device)
				fT5MsgSize = (uint8)(obj.size - 1);
				TRACE("T5: addr=0x%04x, msgSize=%u\n", fT5Address, fT5MsgSize);
				break;
			}
			case MXT_OBJECT_TYPE_T6:
				fT6Address = (uint16)obj.startAddress;
				TRACE("T6: addr=0x%04x\n", fT6Address);
				break;
			case MXT_OBJECT_TYPE_T7:
				fT7Address = (uint16)obj.startAddress;
				TRACE("T7: addr=0x%04x\n", fT7Address);
				break;
			case MXT_OBJECT_TYPE_T9: {
				fHasT9 = true;
				fT9Address = (uint16)obj.startAddress;
				fT9ReportIDMin = _ReportIDMinAt(i);
				fT9ReportIDMax = _ReportIDMaxAt(i);
				TRACE("T9: addr=0x%04x, reportIDs=%u-%u\n",
					fT9Address, fT9ReportIDMin, fT9ReportIDMax);
				break;
			}
			case MXT_OBJECT_TYPE_T100: {
				fHasT100 = true;
				fT100Address = (uint16)obj.startAddress;
				fT100ReportIDMin = _ReportIDMinAt(i);
				fT100ReportIDMax = _ReportIDMaxAt(i);
				TRACE("T100: addr=0x%04x, reportIDs=%u-%u\n",
					fT100Address, fT100ReportIDMin, fT100ReportIDMax);
				break;
			}
			case MXT_OBJECT_TYPE_T18:
				fT18Address = (uint16)obj.startAddress;
				TRACE("T18: addr=0x%04x\n", fT18Address);
				break;
			case MXT_OBJECT_TYPE_T19:
				fT19ReportID = _ReportIDMinAt(i);
				TRACE("T19: reportID=%u\n", fT19ReportID);
				break;
			case MXT_OBJECT_TYPE_T44:
				fT44Address = (uint16)obj.startAddress;
				TRACE("T44: addr=0x%04x\n", fT44Address);
				break;
		}
	}

	TRACE_ALWAYS("Object table: T9=%s(0x%04x,%u-%u), T100=%s(0x%04x,%u-%u), "
		"T5=0x%04x(%u), T6=0x%04x, T44=0x%04x\n",
		fHasT9 ? "yes" : "no", fT9Address, fT9ReportIDMin, fT9ReportIDMax,
		fHasT100 ? "yes" : "no", fT100Address, fT100ReportIDMin,
		fT100ReportIDMax, fT5Address, fT5MsgSize,
		fT6Address, fT44Address);

	return B_OK;
}


// ----------------------------------------------------------------#
// OBP configuration — power, touch resolution, essential registers
// ----------------------------------------------------------------#

//! Write bytes to an object register using the bundled maXTouch I2C
//! protocol (addr + data in one transaction), then read back and verify.
//! Each phase is its own full transaction — _ExecCommand acquires/releases
//! the bus independently, and waits for STOP_DET before returning.
status_t
MxtDevice::_WriteObjectVerify(uint16 addr, const uint8* data, size_t len,
	const char* label)
{
	if (len == 0 || len > 8)
		return B_BAD_VALUE;

	uint8 writeBuf[2 + 8];
	_EncodeAddr(addr, writeBuf);
	memcpy(writeBuf + 2, data, len);

	// Write: addr(2 bytes LE) + data(len bytes) in one transaction (STOP)
	status_t status = _ExecCommand(I2C_OP_WRITE_STOP, writeBuf, 2 + len,
		NULL, 0);
	if (status != B_OK) {
		ERROR("%s: write failed: %s\n", label, strerror(status));
		return status;
	}

	// Read-back: separate pointer-set + read transaction
	uint8 readback[8];
	status = _FetchBuffer(writeBuf, 2, readback, len);
	if (status != B_OK) {
		ERROR("%s: read-back failed: %s\n", label, strerror(status));
		return status;
	}

	for (size_t i = 0; i < len; i++) {
		if (data[i] != readback[i]) {
			ERROR("%s: write mismatch — wrote 0x%02x, read 0x%02x at offset %zu\n",
				label, data[i], readback[i], i);
			return B_ERROR;
		}
	}

	TRACE("%s: verified OK (wrote %zu bytes)\n", label, len);
	return B_OK;
}


status_t
MxtDevice::_InitializePowerConfig()
{
	if (fT7Address == 0) {
		ERROR("T7 address not found\n");
		return B_ERROR;
	}

	uint8 encoded[2];
	_EncodeAddr(fT7Address, encoded);
	uint8 data[2];

	status_t status = _FetchBuffer(encoded, sizeof(encoded), data, sizeof(data));
	if (status != B_OK) {
		ERROR("failed to read T7 config\n");
		return status;
	}

	fT7Idle = data[0];
	fT7Active = data[1];
	TRACE("T7 pre-reset: idle=0x%02x (%u), active=0x%02x (%u)\n",
		fT7Idle, fT7Idle, fT7Active, fT7Active);

	if (fT7Idle == 0 && fT7Active == 0) {
		TRACE("T7 is zero, applying defaults\n");
		fT7Idle = MXT_DEFAULT_T7_IDLE;
		fT7Active = MXT_DEFAULT_T7_ACTIVE;
	}

	uint8 writeData[2] = { fT7Idle, fT7Active };
	TRACE("T7 pre-reset write: idle=0x%02x (%u), active=0x%02x (%u)\n",
		fT7Idle, fT7Idle, fT7Active, fT7Active);
	return _WriteObjectVerify(fT7Address, writeData, 2, "T7");
}


status_t
MxtDevice::_ReadTouchResolution()
{
	status_t status = B_OK;

	if (fHasT9) {
		uint8 encoded[2];
		uint8 data[2];

		// T9 X Range at offset 18 (2 bytes LE)
		_EncodeAddr(fT9Address + 18, encoded);
		status = _FetchBuffer(encoded, sizeof(encoded), data, sizeof(data));
		if (status == B_OK) {
			fTouchXRange = data[0] | (data[1] << 8);
			TRACE("T9 X Range: %u\n", fTouchXRange);
		}

		// T9 Y Range at offset 20 (2 bytes LE)
		if (status == B_OK) {
			_EncodeAddr(fT9Address + 20, encoded);
			status = _FetchBuffer(encoded, sizeof(encoded), data, sizeof(data));
		}
		if (status == B_OK) {
			fTouchYRange = data[0] | (data[1] << 8);
			TRACE("T9 Y Range: %u\n", fTouchYRange);
		}

		// T9 X Size at offset 3, Y Size at offset 4
		if (fT9Address != 0) {
			_EncodeAddr(fT9Address + 3, encoded);
			_FetchBuffer(encoded, sizeof(encoded), data, sizeof(data));
			if (status == B_OK) {
				fTouchXSize = data[0];
				fTouchYSize = data[1];
			}
		}

		// T9 Orientation at offset 9
		if (fT9Address != 0) {
			uint8 orient[1];
			_EncodeAddr(fT9Address + 9, encoded);
			_FetchBuffer(encoded, sizeof(encoded), orient, sizeof(orient));
			fT9Orientation = orient[0];
			TRACE("T9 Orientation: 0x%02x\n", fT9Orientation);
		}

		// Fallback if range reads failed or returned zero
		if (fTouchXRange == 0) {
			TRACE("T9 X Range is zero, defaulting to 1023\n");
			fTouchXRange = 1023;
		}
		if (fTouchYRange == 0) {
			TRACE("T9 Y Range is zero, defaulting to 1023\n");
			fTouchYRange = 1023;
		}
	} else if (fHasT100) {
		uint8 encoded[2];
		uint8 data[2];

		// T100 X Range at offset 13
		_EncodeAddr(fT100Address + 13, encoded);
		status = _FetchBuffer(encoded, sizeof(encoded), data, sizeof(data));
		if (status == B_OK) {
			fTouchXRange = data[0] | (data[1] << 8);
			TRACE("T100 X Range: %u\n", fTouchXRange);
		}

		// T100 Y Range at offset 24
		if (status == B_OK) {
			_EncodeAddr(fT100Address + 24, encoded);
			status = _FetchBuffer(encoded, sizeof(encoded), data, sizeof(data));
		}
		if (status == B_OK) {
			fTouchYRange = data[0] | (data[1] << 8);
			TRACE("T100 Y Range: %u\n", fTouchYRange);
		}

		if (fTouchXRange == 0) {
			fTouchXRange = 1023;
		}
		if (fTouchYRange == 0) {
			fTouchYRange = 1023;
		}
	}

	TRACE_ALWAYS("Touch resolution: X=%u, Y=%u (%s)\n",
		fTouchXRange, fTouchYRange,
		fHasT9 ? "T9" : (fHasT100 ? "T100" : "none"));

	return status;
}


status_t
MxtDevice::_WriteEssentialConfig()
{
	status_t status = B_OK;

	// T7 (Power Config): write + verify with two-phase I2C protocol
	if (fT7Address != 0) {
		uint8 t7Data[2] = { MXT_DEFAULT_T7_IDLE, MXT_DEFAULT_T7_ACTIVE };
		status = _WriteObjectVerify(fT7Address, t7Data, 2, "T7");
		if (status != B_OK) {
			ERROR("failed to write T7 power config with verification\n");
			// Non-fatal — firmware may have set reasonable defaults
		}
	}

	// T9 Ctrl=0x83: enable scan + report + auto mode.
	// Bits 7:   enable (1)
	// Bits 4-6: active touches - 1 (000 = 1)
	// Bits 0-1: report mode (00 = message object)
	// Bits 2-3: auto/move/press config
	// Multi-finger detection comes from the device generating multiple T9
	// messages per cycle, not from the ActiveTouches field.
	if (fHasT9 && fT9Address != 0) {
		uint8 t9Data = 0x83;
		status = _WriteObjectVerify(fT9Address, &t9Data, 1, "T9 Ctrl");
		if (status != B_OK) {
			ERROR("failed to write T9 Ctrl with verification\n");
			// Non-fatal — firmware may have set reasonable defaults
		}
	}

	// T18 Ctrl=0x00: default interrupt behavior
	if (fT18Address != 0) {
		uint8 t18Data = 0x00;
		status = _WriteObjectVerify(fT18Address, &t18Data, 1, "T18 Ctrl");
		if (status != B_OK) {
			ERROR("failed to write T18 Ctrl with verification\n");
			// Non-fatal — firmware may have set reasonable defaults
		}
	}

	return status;
}


status_t
MxtDevice::_ApplyDefaultConfig()
{
	// ChromeOS UEFI firmware supplies the operating configuration.
	TRACE("_ApplyDefaultConfig: skipping (firmware provides config)\n");
	return B_OK;
}


// ----------------------------------------------------------------#
// Message queue — drain, parse, aggregate
// ----------------------------------------------------------------#

status_t
MxtDevice::_DrainInitMessages()
{
	// Drain stale messages accumulated during power-on or reset.
	// Read from T44 count and discard all messages.
	if (fT44Address == 0 || fT5MsgSize == 0)
		return B_ERROR;

	uint8 addr[2];
	_EncodeAddr(fT44Address, addr);

	// Read count + first message in one burst
	uint8 buf[MXT_MSG_SIZE];
	size_t readSize = MIN((size_t)(1 + fT5MsgSize), sizeof(buf));
	status_t status = _FetchBuffer(addr, sizeof(addr), buf, readSize);
	if (status != B_OK)
		return status;

	uint8 count = buf[0];
	if (count == 0 || count >= 0xFF)
		return B_OK;

	// Read remaining messages from T5 address (auto-incrementing)
	uint8 t5Addr[2];
	_EncodeAddr(fT5Address, t5Addr);

	for (uint8 i = 1; i < count; i++) {
		_FetchBuffer(t5Addr, sizeof(t5Addr), buf, fT5MsgSize);
	}

	TRACE("Drained %u init messages\n", count);
	return B_OK;
}


status_t
MxtDevice::_ProcessMessage(const uint8* msg, size_t msgSize,
	mxt_touch_state& state)
{
	if (msg == NULL || msgSize < 2)
		return B_OK;

	uint8 reportID = msg[0];

	// Skip invalid/empty messages
	if (reportID == MXT_REPORT_ID_INVALID || reportID == 0)
		return B_OK;

	// T9 legacy touch message
	if (fHasT9 && reportID >= fT9ReportIDMin && reportID <= fT9ReportIDMax) {
		return _ParseT9Message(msg, msgSize, state);
	}

	// T100 modern touch message
	if (fHasT100 && reportID >= fT100ReportIDMin
		&& reportID <= fT100ReportIDMax) {
		return _ParseT100Message(msg, msgSize, state);
	}

	// T19 GPIO carries the clickpad button state.
	if (fT19ReportID != 0 && reportID == fT19ReportID) {
		// GPIO bits are active-low: a clear bit means pressed
		if (msgSize >= 2) {
			uint8 gpioState = msg[1];
			// Bit 0 = primary button (active-low)
			if (!(gpioState & 0x01))
				state.button = true;
		}
		return B_OK;
	}

	// T6 command/status, T22 noise, etc. — silently ignored
	return B_OK;
}


status_t
MxtDevice::_ParseT9Message(const uint8* msg, size_t msgSize,
	mxt_touch_state& state)
{
	// T9 message format (spec Section 10.3):
	// [0] report ID, [1] flags, [2] X high, [3] Y high,
	// [4] X lo[7:4] | Y lo[3:0], [5] area, [6] amplitude, [7] vector

	if (msgSize < 7)
		return B_OK;

	uint8 flags = msg[1];

	// Skip suppressed contacts
	if (flags & MXT_T9_SUPPRESS)
		return B_OK;

	// Reconstruct 12-bit coordinates
	uint16 x = (msg[2] << 4) | ((msg[4] >> 4) & 0xf);
	uint16 y = (msg[3] << 4) | (msg[4] & 0xf);

	// Downscale to 10-bit if range < 1024 (spec Section 10.3)
	if (fTouchXRange < 1024)
		x >>= 2;
	if (fTouchYRange < 1024)
		y >>= 2;

	// Clamp to hardware range
	if (x > fTouchXRange)
		x = fTouchXRange;
	if (y > fTouchYRange)
		y = fTouchYRange;

	uint8 area = msg[5];
	uint8 amplitude = msg[6];
	(void)amplitude;	// used in TRACE below
	bool detect = (flags & MXT_T9_DETECT) != 0;

	TRACE("T9: id=0x%02x flags=0x%02x x=%u y=%u area=%u amp=%u\n",
		msg[0], flags, x, y, area, amplitude);

	// Accumulate contact into state.
	// Button state comes from T19 GPIO (clickpad), NOT from touch flags.
	// DETECT/PRESS control per-contact tip switch in HID, not the mouse button.
	if (detect) {
		state.x += x;
		state.y += y;
		state.pressure += area;
		state.contactCount++;
	}

	return B_OK;
}


status_t
MxtDevice::_ParseT100Message(const uint8* msg, size_t msgSize,
	mxt_touch_state& state)
{
	// T100 message format (spec Section 10.4):
	// [0] report ID, [1] flags, [2-3] X (LE), [4-5] Y (LE),
	// [6+] auxiliary bytes per aux-enable field

	if (msgSize < 6)
		return B_OK;

	uint8 flags = msg[1];
	uint8 type = (flags & 0x70) >> 4;
	bool detect = (flags & 0x80) != 0;

	// Skip non-active contact types
	if (type == MXT_T100_TYPE_HOVER_FINGER || type == MXT_T100_TYPE_LARGE)
		return B_OK;

	uint16 x = msg[2] | (msg[3] << 8);
	uint16 y = msg[4] | (msg[5] << 8);

	// Clamp to hardware range
	if (x > fTouchXRange)
		x = fTouchXRange;
	if (y > fTouchYRange)
		y = fTouchYRange;

	uint8 area = 0;
	uint8 amplitude = 0;
	if (msgSize >= 7)
		amplitude = msg[6];
	if (msgSize >= 8)
		area = msg[7];
	(void)amplitude;	// used in TRACE below

	TRACE("T100: id=0x%02x type=%u x=%u y=%u area=%u amp=%u\n",
		msg[0], type, x, y, area, amplitude);

	// Accumulate contact into state.
	// Button state comes from T19 GPIO (clickpad), NOT from touch flags.
	if (detect && (type == MXT_T100_TYPE_FINGER
		|| type == MXT_T100_TYPE_PASSIVE_STYLUS
		|| type == MXT_T100_TYPE_GLOVE)) {
		state.x += x;
		state.y += y;
		state.pressure += area;
		state.contactCount++;
	}

	return B_OK;
}


// ----------------------------------------------------------------#
// Full initialization sequence
// ----------------------------------------------------------------#

status_t
MxtDevice::Initialize()
{
	TRACE("Starting maXTouch initialization\n");

	// Phase 0: Discover IRQ from ACPI _CRS
	if (fACPI != NULL && fACPIHandle != NULL) {
		mxt_crs_context crs = { 0, 0 };

		status_t walkStatus = fACPI->walk_resources(fACPIHandle,
			(char*)"_CRS", _mxt_crs_callback, &crs);

		if (walkStatus == B_OK && crs.irq > 0) {
			fIRQ = crs.irq;
			fIRQFlags = crs.irqFlags;
			TRACE_ALWAYS("ACPI _CRS: IRQ %d, flags 0x%x\n", fIRQ, fIRQFlags);
		} else {
			ERROR("failed to discover IRQ from ACPI _CRS\n");
		}
	}

	// Phase 1: Read Information Block
	status_t status = _ReadInfoBlock();
	if (status != B_OK) {
		ERROR("failed to read info block\n");
		return status;
	}

	// Phase 2: Parse Object Table
	status = _ParseObjectTable();
	if (status != B_OK) {
		ERROR("failed to parse object table\n");
		return status;
	}

	// Phase 3: Drain pending messages from boot
	TRACE("Draining pending messages\n");
	_DrainInitMessages();	// non-fatal if fails

	// Phase 4: Read touch resolution (before reset — firmware config active)
	status = _ReadTouchResolution();
	if (status == B_OK) {
		// Apply orientation swap to specs (matches Windows driver)
		if (fHasT9 && (fT9Orientation & 0x01)) {
			fHardwareSpecs.areaEndX = fTouchYRange;
			fHardwareSpecs.areaEndY = fTouchXRange;
		} else {
			fHardwareSpecs.areaEndX = fTouchXRange;
			fHardwareSpecs.areaEndY = fTouchYRange;
		}
		TRACE("Touchpad dimensions: %dx%d (orientation 0x%02x)\n",
			fHardwareSpecs.areaEndX, fHardwareSpecs.areaEndY, fT9Orientation);
	} else {
		ERROR("failed to read touch resolution\n");
		// Fallback
		fHardwareSpecs.areaEndX = 1023;
		fHardwareSpecs.areaEndY = 1023;
		fTouchXRange = 1023;
		fTouchYRange = 1023;
	}

	// Phase 4b: Initialize power configuration (pre-reset — sets NVM defaults)
	// Spec §7 Phase 5: read T7, apply defaults if zero, write back.
	// This ensures the device's NVM has correct values before soft reset.
	TRACE("Phase 4b: initializing power config (pre-reset)\n");
	_InitializePowerConfig();	// non-fatal — firmware may have good defaults

	// Phase 5: Soft reset
	TRACE("Sending soft reset...\n");
	uint8 encoded[2];
	_EncodeAddr(fT6Address, encoded);
	uint8 resetCmd[] = { encoded[0], encoded[1], MXT_T6_CMD_SOFT_RESET };
	status = _ExecCommand(I2C_OP_WRITE_STOP, resetCmd, sizeof(resetCmd), NULL, 0);
	if (status != B_OK) {
		ERROR("failed to send soft reset\n");
		return status;
	}

	// Wait for RESET bit to clear (up to 100ms)
	for (int i = 0; i < 100; i++) {
		uint8 data[1];
		_EncodeAddr(fT6Address, encoded);
		status = _FetchBuffer(encoded, sizeof(encoded), data, sizeof(data));
		if (status == B_OK && !(data[0] & 0x01))
			break;
		snooze(1000);
	}
	TRACE("Soft reset complete\n");

	// Phase 5b: Drain post-reset messages
	_DrainInitMessages();

	// Phase 6: Apply essential configuration
	snooze(10000);	// 10ms for device to exit reset
	status = _WriteEssentialConfig();
	if (status != B_OK) {
		ERROR("failed to apply essential config\n");
		// Non-fatal — firmware may have set reasonable defaults
	}

	// Phase 6b: Create touch engine (stateful contact/gesture tracking)
	fTouchEngine = new(std::nothrow) TouchEngine();
	if (fTouchEngine != NULL) {
		fTouchEngine->SetResolution(fTouchXRange, fTouchYRange);
		TRACE("Touch engine initialized (resolution %ux%u)\n",
			fTouchXRange, fTouchYRange);
	} else {
		ERROR("failed to allocate touch engine\n");
		// Non-fatal — driver will operate without gesture engine
	}

	// Phase 7: Install interrupt handler
	if (fIRQ > 0) {
		// install_io_interrupt_handler() does not program trigger/polarity.
		arch_int_configure_io_interrupt(fIRQ, fIRQFlags);

		status = install_io_interrupt_handler(fIRQ, _MxtInterrupt, this, 0);
		if (status == B_OK) {
			fIRQInstalled = true;
			TRACE_ALWAYS("IRQ %d installed (level-triggered, active-low)\n", fIRQ);
		} else {
			ERROR("install_io_interrupt_handler failed: %s\n", strerror(status));
		}
	} else {
		ERROR("no IRQ available from ACPI _CRS\n");
	}

	// Phase 8: Start worker thread — ISR-scheduled I2C pump
	if (fIRQInstalled) {
		status = _StartWorker();
		if (status != B_OK) {
			ERROR("failed to start worker thread\n");
		}
	}

	TRACE("Initialization complete\n");
	return B_OK;
}
