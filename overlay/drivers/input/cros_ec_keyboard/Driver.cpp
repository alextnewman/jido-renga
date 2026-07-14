// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with Qwen 3.6
// SPDX-FileContributor: Generated with GitHub Copilot

//! ChromeOS EC keyboard driver -- native driver for the 8042-class interface
//! emulated by the ChromeOS Embedded Controller (WINKY / Chromebook 2).


#include "Driver.h"

// Reuse the PS/2 set-1 keycode map (identical translated protocol).
#include "ATKeymap.h"

// ACPICA full struct definitions (C++ naming: Type, Data, etc.).
#include "acpi.h"

#include <KernelExport.h>
#include <PCI.h>
#include <arch/int.h>
#include <device_manager.h>
#include <new>
#include <stdio.h>
#include <string.h>





static device_manager_info* sDeviceManager;





struct crs_context {
	uint8		irq;
	uint32		irqFlags;
};


static acpi_status
_crs_callback(ACPI_RESOURCE* res, void* context)
{
	crs_context* crs = static_cast<crs_context*>(context);

	TRACE("_CRS resource type %d\n", res->Type);

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
		TRACE("_CRS ExtendedIRQ: irq=%d, triggering=%d, polarity=%d, flags=0x%x\n",
			crs->irq, res->Data.ExtendedIrq.Triggering,
			res->Data.ExtendedIrq.Polarity, crs->irqFlags);
	} else if (res->Type == ACPI_RESOURCE_TYPE_IRQ) {
		crs->irq = res->Data.Irq.Interrupts[0];
		crs->irqFlags = 0;
		if (res->Data.Irq.Triggering == 1)
			crs->irqFlags |= B_EDGE_TRIGGERED;
		else
			crs->irqFlags |= B_LEVEL_TRIGGERED;
	}

	return B_OK;
}





static status_t
_kbc_wait_writable()
{
	bigtime_t start = system_time();
	while (system_time() - start < kTimeoutCtrl) {
		if ((kbc_read_status() & kStatusInputBufferFull) == 0)
			return B_OK;
		snooze(kPollDelayUs);
	}
	return B_TIMED_OUT;
}


static status_t
_kbc_wait_readable()
{
	bigtime_t start = system_time();
	while (system_time() - start < kTimeoutCtrl) {
		if ((kbc_read_status() & kStatusOutputBufferFull) != 0)
			return B_OK;
		snooze(kPollDelayUs);
	}
	return B_TIMED_OUT;
}


static status_t
_kbc_drain()
{
	for (uint32 i = 0; i < kDrainLimit; i++) {
		if ((kbc_read_status() & kStatusOutputBufferFull) == 0)
			return B_OK;
		kbc_read_data();
		snooze(kPollDelayUs);
	}
	return B_IO_ERROR;
}


static status_t
_kbc_command(uint8 cmd, const uint8* params, size_t paramCount,
	uint8* response, size_t responseCount)
{
	status_t status;

	status = _kbc_wait_writable();
	if (status != B_OK)
		return status;
	kbc_write_command(cmd);

	for (size_t i = 0; i < paramCount; i++) {
		status = _kbc_wait_writable();
		if (status != B_OK)
			return status;
		kbc_write_data(params[i]);
	}

	for (size_t i = 0; i < responseCount; i++) {
		status = _kbc_wait_readable();
		if (status != B_OK)
			return status;
		response[i] = kbc_read_data();
	}

	return B_OK;
}


static status_t
_kbc_read_stable_config(uint8* config)
{
	//! Read config byte until two consecutive reads agree (§15.11).
	uint8 last = 0xFF;

	for (uint32 i = 0; i < kConfigStableReads; i++) {
		uint8 current;
		status_t status = _kbc_command(kCtrlReadConfig, NULL, 0, &current, 1);
		if (status != B_OK)
			return status;
		if (current == last) {
			*config = current;
			return B_OK;
		}
		last = current;
	}

	//! Never stabilized; return the last value anyway.
	*config = last;
	return B_OK;
}





static status_t
_kbc_dev_byte(driver_cookie* d, uint8 byte, bigtime_t ackTimeout, bool useISR)
{
	if (useISR)
		atomic_set(&d->fCommandMode, 1);

	for (uint32 attempt = 0; attempt < 2; attempt++) {
		status_t status = _kbc_wait_writable();
		if (status != B_OK) {
			if (useISR)
				atomic_set(&d->fCommandMode, 0);
			return status;
		}
		kbc_write_data(byte);

		if (useISR) {
			//! Wait for ISR to capture ACK/Resend via semaphore.
			status = acquire_sem_etc(d->fCommandSem, 1, B_CAN_INTERRUPT,
			 ackTimeout);
			if (status != B_OK) {
				atomic_set(&d->fCommandMode, 0);
				return status;
			}
			uint8 resp = d->fCommandByte;
			if (resp == kRespAck) {
				atomic_set(&d->fCommandMode, 0);
				return B_OK;
			}
			if (resp == kRespResend)
				continue;	// retry this byte
			atomic_set(&d->fCommandMode, 0);
			return B_IO_ERROR;
		}

		//! Direct polling mode.
		bigtime_t start = system_time();
		while (system_time() - start < ackTimeout) {
			if ((kbc_read_status() & kStatusOutputBufferFull) != 0) {
				uint8 resp = kbc_read_data();
				if (resp == kRespAck)
					return B_OK;
				if (resp == kRespResend)
					break;	// retry this byte
				return B_IO_ERROR;
			}
			snooze(kPollDelayUs);
		}
	}

	if (useISR)
		atomic_set(&d->fCommandMode, 0);
	return B_TIMED_OUT;
}


//! Direct-polling response wait (used during bring-up and re-init).
static status_t
_kbc_dev_response_direct(uint8* outByte, bigtime_t timeout)
{
	bigtime_t start = system_time();
	while (system_time() - start < timeout) {
		if ((kbc_read_status() & kStatusOutputBufferFull) != 0) {
			*outByte = kbc_read_data();
			return B_OK;
		}
		snooze(kPollDelayUs);
	}
	return B_TIMED_OUT;
}





//! Enqueue a single key event into the ring buffer.
//! Returns true if the event was enqueued, false if the ring is full.
static bool
_enqueue_key(driver_cookie* d, uint32 keycode, bool isKeydown)
{
	raw_key_info keyInfo;
	keyInfo.timestamp = system_time();
	keyInfo.keycode = keycode;
	keyInfo.is_keydown = isKeydown;

	int32 next = (d->fKeyRingTail + 1) % kKeyRingSize;
	if (next != d->fKeyRingHead) {
		d->fKeyRing[d->fKeyRingTail] = keyInfo;
		d->fKeyRingTail = next;
		d->fEventCV.NotifyOne();
		return true;
	}

	//! Ring full — log the drop (always visible, not behind TRACE).
	dprintf("cros_ec_kbd: RING FULL! keycode=0x%03x %s DROPPED\n",
		keycode, isKeydown ? "down" : "up");
	return false;
}


static void
_cros_ec_decode_byte(driver_cookie* d, uint8 byte)
{
	TRACE("decode 0x%02x ext=%d rel=%d\n",
		byte, d->fExtendedMode, d->fIsRelease);

	// ── Phase 1: Extended prefixes (§11.3) ──────────────────────────
	// E0 starts a one-byte extended code (right modifiers, arrows, etc.).
	if (byte == kPrefixE0) {
		d->fExtendedMode = 1;
		d->fIsRelease = false;
		return;
	}

	// E1 starts the Pause/Break long sequence.
	if (byte == kPrefixE1) {
		d->fExtendedMode = 2;
		d->fPauseSeqIndex = 0;
		d->fIsRelease = false;
		return;
	}

	// ── Phase 2: E1 Pause/Break sequence collection ─────────────────
	if (d->fExtendedMode == 2) {
		static const uint8 kPauseSeq[] = { 0x1D, 0x45 };

		if (byte == kPauseSeq[d->fPauseSeqIndex]) {
			d->fPauseSeqIndex++;
			if (d->fPauseSeqIndex >= 2) {
				// Full sequence E1 1D 45 received → Pause key press.
				_enqueue_key(d, kPauseKeycode, true);
			}
		}
		d->fExtendedMode = 0;
		d->fPauseSeqIndex = 0;
		d->fIsRelease = false;
		return;
	}

	// ── Phase 3: Collision tracking for non-extended bytes (§11.5) ──
	// Only update collision bits when NOT inside an E0-prefixed sequence.
	// Extended bytes are never reinterpreted as protocol responses.
	//
	// When a collision code is recognized (make or break with prior make),
	// set collisionHandled so that Phase 4 skips the sentinel check -- the
	// byte is a valid key event, not a protocol response.
	bool collisionHandled = false;
	if (d->fExtendedMode == 0) {
		uint8 code = byte & 0x7F;
		bool isBreak = (byte & 0x80) != 0;

		switch (code) {
			case kCollisionFA:
				if (isBreak) {
					if (!d->fCollisionFA)
						return;	// ACK byte, no prior make → ignore
					d->fCollisionFA = false;
					collisionHandled = true;
				} else {
					d->fCollisionFA = true;
					collisionHandled = true;
				}
				break;
			case kCollisionFE:
				if (isBreak) {
					if (!d->fCollisionFE)
						return;	// Resend byte, no prior make → ignore
					d->fCollisionFE = false;
					collisionHandled = true;
				} else {
					d->fCollisionFE = true;
					collisionHandled = true;
				}
				break;
			case kCollisionAA:
				if (isBreak) {
					if (!d->fCollisionAA)
						return;	// BAT byte, no prior make → ignore
					d->fCollisionAA = false;
					collisionHandled = true;
				} else {
					d->fCollisionAA = true;
					collisionHandled = true;
				}
				break;
			case kCollisionFF:
				if (isBreak) {
					if (!d->fCollisionFF)
						return;	// Overrun byte, no prior make → ignore
					d->fCollisionFF = false;
					collisionHandled = true;
				} else {
					d->fCollisionFF = true;
					collisionHandled = true;
				}
				break;
			case kCollisionF1:
				if (isBreak) {
					if (!d->fCollisionF1)
						return;	// 0xF1 language key, no prior make → ignore
					d->fCollisionF1 = false;
					collisionHandled = true;
				} else {
					d->fCollisionF1 = true;
					collisionHandled = true;
				}
				break;
			case kCollisionF2:
				if (isBreak) {
					if (!d->fCollisionF2)
						return;	// 0xF2 language key, no prior make → ignore
					d->fCollisionF2 = false;
					collisionHandled = true;
				} else {
					d->fCollisionF2 = true;
					collisionHandled = true;
				}
				break;
			default:
				break;
		}
	}

	// ── Phase 4: Protocol sentinel bytes (§9.1, §11.4) ──────────────
	// Skip sentinel check when Phase 3 handled a collision code -- the byte
	// is a valid key event (e.g., Left Shift break 0xAA), not a protocol byte.

	// §11.7: Spontaneous reset detection.
	// 0xAA (BAT) as a make code means the device reset itself.
	if (!collisionHandled && byte == kRespSelfTestPassed) {
		TRACE("spontaneous reset (BAT)\n");
		d->fDecodingEnabled = false;
		d->fNeedReinit = true;
		d->fExtendedMode = 0;
		d->fIsRelease = false;
		d->fEventCV.NotifyOne();
		return;
	}

	// §15.12: Tolerate stray ACK/Resend outside a command transaction.
	if (!collisionHandled && (byte == kRespAck || byte == kRespResend))
		return;

	// §11.4: 0xF0 break prefix (set 2/3 artifact -- tolerate in translated).
	// Preserves fExtendedMode so that E0 F0 XX correctly decodes as an
	// extended key release (e.g., Right Ctrl = E0 F0 1D).
	if (byte == 0xF0) {
		d->fIsRelease = true;
		return;
	}

	// §11.4: 0xFF = overrun / "too many keys".
	if (!collisionHandled && byte == 0xFF) {
		d->fExtendedMode = 0;
		d->fIsRelease = false;
		return;
	}

	// ── Phase 5: Extract make/break and compose lookup index ─────────
	// §11.2: In set 1, press = code (0x01..0x7F), release = code | 0x80.
	bool isRelease = (byte & 0x80) != 0;
	uint8 code = byte & 0x7F;

	// E0-prefixed key: fold the extended flag into the high bit so that
	// plain and E0-prefixed forms of the same code occupy distinct slots
	// in kATKeycodeMap (e.g., Left Ctrl = 0x1D, Right Ctrl = 0x9D).
	if (d->fExtendedMode == 1) {
		code |= 0x80;
		d->fExtendedMode = 0;
	}

	// Ignore codes outside the keymap range.
	if (code == 0 || code > B_COUNT_OF(kATKeycodeMap)) {
		d->fIsRelease = false;
		return;
	}

	// ── Phase 6: Look up keycode and enqueue event ───────────────────
	uint32 keycode = kATKeycodeMap[code - 1];

	// Combined release: either the set-1 break bit or a prior 0xF0 prefix.
	bool isKeydown = !(isRelease || d->fIsRelease);

	// Emergency key tracking (Alt+SysRq, matches PS/2 driver).
	if (code == kLeftAltScan || code == (kRightAltScan & 0x7F)) {
		if (isKeydown) {
			d->fEmergencyKeyStatus |= code == kLeftAltScan
				? kEmergencyLeftAlt : kEmergencyRightAlt;
		} else {
			d->fEmergencyKeyStatus &= ~(code == kLeftAltScan
				? kEmergencyLeftAlt : kEmergencyRightAlt);
		}
	}

	_enqueue_key(d, keycode, isKeydown);

	// ── Phase 7: Reset per-byte state ────────────────────────────────
	d->fIsRelease = false;
}





static int32
cros_ec_interrupt(void* data)
{
	driver_cookie* d = static_cast<driver_cookie*>(data);

	// §12.2: Edge-triggered IRQ -- drain all pending bytes while OBF is set.
	uint32 byteCount = 0;
	while (true) {
		uint8 status = kbc_read_status();
		if ((status & kStatusOutputBufferFull) == 0)
			return byteCount > 0 ? B_HANDLED_INTERRUPT : B_UNHANDLED_INTERRUPT;

		uint8 byte = kbc_read_data();
		byteCount++;

		// Discard auxiliary data (no aux device on WINKY, §15.5).
		if ((status & kStatusAuxiliaryData) != 0)
			continue;

		// Command mode: ISR routes byte to the command state machine (§8.1).
		if (d->fCommandMode != 0) {
			d->fCommandByte = byte;
			release_sem_etc(d->fCommandSem, 1, B_DO_NOT_RESCHEDULE);
			continue;
		}

		// Normal mode: decode scan code and enqueue key event.
		if (d->fDecodingEnabled)
			_cros_ec_decode_byte(d, byte);
	}

	return B_HANDLED_INTERRUPT;
}





static status_t
_bring_up_device(driver_cookie* d)
{
	TRACE("cros_ec_kbd: bringing up device\n");

	// All device commands use direct polling during bring-up: the keyboard
	// interrupt is disabled in the config byte, so the ISR will not fire
	// for keyboard data and the semaphore path would time out.

	// §10: Drain before any device commands.
	TRACE("bring-up: drain\n");
	status_t status = _kbc_drain();
	if (status != B_OK) {
		TRACE("bring-up: drain failed %d\n", status);
		return status;
	}

	// Device reset — consume self-test result (non-fatal).
	uint8 resp = 0;
	TRACE("bring-up: reset\n");
	_kbc_dev_byte(d, kDevReset, kTimeoutResetAck, false);
	_kbc_dev_response_direct(&resp, kTimeoutResetBat);
	TRACE("bring-up: reset BAT=0x%02x\n", resp);

	// Device identify — tolerate failure, §15.9.
	TRACE("bring-up: identify\n");
	_kbc_dev_byte(d, kDevIdentify, kTimeoutDevAck, false);
	uint8 id[2] = {0, 0};
	for (int i = 0; i < 2; i++) {
		_kbc_dev_response_direct(&resp, kTimeoutDevResp);
		id[i] = resp;
	}
	(void)id;	// consumed to keep the KBC in sync; value only traced
	TRACE("bring-up: ID=0x%02x%02x\n", id[0], id[1]);

	// Select scan-code set 2 (controller translates to set 1).
	_kbc_dev_byte(d, kDevScanCodeSet, kTimeoutDevAck, false);
	_kbc_dev_byte(d, 2, kTimeoutDevAck, false);

	_kbc_dev_byte(d, kDevSetIndicators, kTimeoutDevAck, false);
	_kbc_dev_byte(d, 0x00, kTimeoutDevAck, false);

	_kbc_dev_byte(d, kDevSetAutoRepeat, kTimeoutDevAck, false);
	_kbc_dev_byte(d, 0x00, kTimeoutDevAck, false);

	// Enable scanning last — bytes arrive immediately after this.
	TRACE("bring-up: enable scanning\n");
	_kbc_dev_byte(d, kDevEnableScanning, kTimeoutDevAck, false);
	d->fScanningEnabled = true;

	// Arm the keyboard interrupt now that decoding is ready.
	uint8 config = d->fInitialConfigByte;
	config &= ~kConfigKbdPortDisable;
	config |= kConfigKeyboardIntEnable;
	config |= kConfigTranslationEnable;
	config |= kConfigAuxPortDisable;
	TRACE("bring-up: write config 0x%02x (was 0x%02x)\n",
		config, d->fInitialConfigByte);
	_kbc_command(kCtrlWriteConfig, &config, 1, NULL, 0);

	d->fDecodingEnabled = true;

	TRACE("bring-up: complete\n");
	return B_OK;
}


static void
_tear_down_device(driver_cookie* d)
{
	TRACE("cros_ec_kbd: tearing down device\n");

	// Disable decoding first (ISR drops bytes).
	d->fDecodingEnabled = false;

	// Disable keyboard interrupt (stops new interrupts).
	uint8 config = d->fInitialConfigByte;
	config &= ~kConfigKeyboardIntEnable;
	_kbc_command(kCtrlWriteConfig, &config, 1, NULL, 0);

	// Use direct polling — keyboard interrupt is now disabled.
	if (d->fScanningEnabled) {
		_kbc_dev_byte(d, kDevDisableScanning, kTimeoutDevAck, false);
		d->fScanningEnabled = false;
	}

	// Reset decoder state so a stale partial sequence doesn't corrupt next use.
	d->fExtendedMode = 0;
	d->fIsRelease = false;
	d->fPauseSeqIndex = 0;
	d->fEmergencyKeyStatus = 0;

	_kbc_drain();

	TRACE("cros_ec_kbd: device down\n");
}


static status_t
_reinit_device(driver_cookie* d)
{
	TRACE("cros_ec_kbd: re-initializing device\n");

	// Disable decoding and keyboard interrupt before re-init to avoid the ISR
	// receiving bytes while command mode is toggled. All device commands use
	// direct polling (keyboard interrupt disabled).
	d->fDecodingEnabled = false;
	d->fExtendedMode = 0;
	d->fIsRelease = false;
	d->fPauseSeqIndex = 0;
	d->fCollisionFA = false;
	d->fCollisionFE = false;
	d->fCollisionAA = false;
	d->fCollisionFF = false;
	d->fCollisionF1 = false;
	d->fCollisionF2 = false;
	d->fEmergencyKeyStatus = 0;

	// Disable keyboard interrupt.
	uint8 config = d->fInitialConfigByte;
	config &= ~kConfigKeyboardIntEnable;
	_kbc_command(kCtrlWriteConfig, &config, 1, NULL, 0);

	// Drain stale bytes before re-init.
	_kbc_drain();

	_kbc_dev_byte(d, kDevDisableScanning, kTimeoutDevAck, false);
	d->fScanningEnabled = false;

	_kbc_dev_byte(d, kDevReset, kTimeoutResetAck, false);
	// Consume self-test result (non-fatal).
	uint8 resp;
	_kbc_dev_response_direct(&resp, kTimeoutResetBat);

	// Select scan-code set 2 (controller translates to set 1).
	_kbc_dev_byte(d, kDevScanCodeSet, kTimeoutDevAck, false);
	_kbc_dev_byte(d, 2, kTimeoutDevAck, false);

	_kbc_dev_byte(d, kDevSetIndicators, kTimeoutDevAck, false);
	_kbc_dev_byte(d, 0x00, kTimeoutDevAck, false);

	// Auto-repeat (use stored rate).
	static const uint16 kRateTable[] = {
		33, 37, 42, 46, 50, 54, 58, 63, 67, 75,
		83, 92, 100, 109, 116, 125, 133, 149, 167,
		182, 200, 217, 232, 250, 270, 303, 333,
		370, 400, 435, 470, 500
	};

	uint8 rateIndex = 31;
	for (int32 i = 0; i < 32; i++) {
		if (d->fRepeatRate <= kRateTable[i]) {
			rateIndex = static_cast<uint8>(i);
			break;
		}
	}

	uint8 delayIndex = 0;
	if (d->fRepeatDelay >= 1000000)
		delayIndex = 3;
	else if (d->fRepeatDelay >= 750000)
		delayIndex = 2;
	else if (d->fRepeatDelay >= 500000)
		delayIndex = 1;

	uint8 param = rateIndex | (delayIndex << 5);
	_kbc_dev_byte(d, kDevSetAutoRepeat, kTimeoutDevAck, false);
	_kbc_dev_byte(d, param, kTimeoutDevAck, false);

	// Drain anything accumulated during re-init.
	_kbc_drain();

	_kbc_dev_byte(d, kDevEnableScanning, kTimeoutDevAck, false);
	d->fScanningEnabled = true;

	// Re-arm keyboard interrupt now that decoding is ready.
	config = d->fInitialConfigByte;
	config &= ~kConfigKbdPortDisable;
	config |= kConfigKeyboardIntEnable;
	config |= kConfigTranslationEnable;
	config |= kConfigAuxPortDisable;
	_kbc_command(kCtrlWriteConfig, &config, 1, NULL, 0);

	d->fDecodingEnabled = true;
	d->fNeedReinit = false;

	TRACE("cros_ec_kbd: device re-initialized\n");
	return B_OK;
}



static float
cros_ec_supports_device(device_node* parent)
{
	if (sDeviceManager == NULL || parent == NULL)
		return 0.0f;

	const char* bus = NULL;
	if (sDeviceManager->get_attr_string(parent, B_DEVICE_BUS, &bus, false)
		!= B_OK || bus == NULL || strcmp(bus, "acpi") != 0) {
		return 0.0f;
	}

	uint32 deviceType;
	if (sDeviceManager->get_attr_uint32(parent, ACPI_DEVICE_TYPE_ITEM,
			&deviceType, false) != B_OK || deviceType != ACPI_TYPE_DEVICE) {
		return 0.0f;
	}

	// Primary match: HID GOOG000A (ChromeOS EC keyboard).
	const char* hid;
	if (sDeviceManager->get_attr_string(parent, ACPI_DEVICE_HID_ITEM, &hid,
			false) == B_OK && hid != NULL) {
		TRACE("supports_device HID=%s\n", hid);
		if (strcmp(hid, CROS_EC_KBD_ACPI_HID) == 0)
			return 0.8f;
	}

	// Fallback: compatible IDs PNP0303 / PNP030B.
	const char* cid;
	if (sDeviceManager->get_attr_string(parent, ACPI_DEVICE_CID_ITEM, &cid,
			false) == B_OK && cid != NULL) {
		TRACE("supports_device CID=%s\n", cid);
		if (strcmp(cid, CROS_EC_KBD_ACPI_CID_PNP0303) == 0
			|| strcmp(cid, CROS_EC_KBD_ACPI_CID_PNP030B) == 0) {
			return 0.5f;
		}
	}

	return 0.0f;
}


static status_t
cros_ec_register_device(device_node* parent)
{
	device_attr attrs[] = {
		{ B_DEVICE_PRETTY_NAME, B_STRING_TYPE,
			{ .string = "ChromeOS EC Keyboard" }},
		{ B_DEVICE_TYPE, B_UINT16_TYPE, { .ui16 = PCI_input }},
		{ B_DEVICE_SUB_TYPE, B_UINT16_TYPE, { .ui16 = PCI_keyboard }},
		{ NULL }
	};

	io_resource ioRes[] = {
		{ B_IO_PORT, 0x60, 1 },
		{ B_IO_PORT, 0x64, 1 },
		{ 0, 0, 0 },
	};

	return sDeviceManager->register_node(parent, CROS_EC_KBD_DRIVER_MODULE_NAME,
		attrs, ioRes, NULL);
}


static status_t
cros_ec_init_driver(device_node* node, void** _driverCookie)
{
	TRACE("init_driver entered\n");

	driver_cookie* d = new(std::nothrow) driver_cookie();
	if (d == NULL)
		return B_NO_MEMORY;

	d->fNode = node;
	d->fACPI = NULL;
	d->fACPIDevice = NULL;
	d->fIRQ = 0;
	d->fIRQFlags = 0;
	d->fIRQInstalled = false;
	d->fInitialConfigByte = 0;
	d->fDecodingEnabled = false;
	d->fExtendedMode = 0;
	d->fIsRelease = false;
	d->fPauseSeqIndex = 0;
	d->fCollisionFA = false;
	d->fCollisionFE = false;
	d->fCollisionAA = false;
	d->fCollisionFF = false;
	d->fCollisionF1 = false;
	d->fCollisionF2 = false;
	d->fCommandMode = false;
	d->fCommandSem = -1;
	d->fCommandByte = 0;
	d->fNeedReinit = false;
	d->fEmergencyKeyStatus = 0;
	d->fKeyRing = NULL;
	d->fKeyRingHead = 0;
	d->fKeyRingTail = 0;
	d->fHasReader = false;
	d->fHasDebugReader = false;
	d->fKeyboardID = 0xAB83;	// standard AT keyboard identity
	d->fRepeatRate = 33;		// fastest repeat (ms)
	d->fRepeatDelay = 250000;	// 250 ms initial delay
	d->fScanningEnabled = false;

	// Initialize synchronization primitives.
	mutex_init(&d->fLock, "cros_ec_kbd");
	d->fEventCV.Init(d, "cros_ec_kbd_cv");
	d->fCommandSem = create_sem(0, "cros_ec_kbd_cmd");
	if (d->fCommandSem < B_OK) {
		mutex_destroy(&d->fLock);
		status_t semError = d->fCommandSem;
		delete d;
		return semError;
	}

	// Allocate ring buffer.
	d->fKeyRing = new(std::nothrow) raw_key_info[kKeyRingSize];
	if (d->fKeyRing == NULL) {
			delete_sem(d->fCommandSem);
			mutex_destroy(&d->fLock);
			delete d;
			return B_NO_MEMORY;
	}
	d->fKeyRingHead = 0;
	d->fKeyRingTail = 0;

	// Get ACPI handle from parent node's driver cookie.
	device_node* parent = sDeviceManager->get_parent_node(node);
	acpi_device_module_info* acpi;
	acpi_device acpiDevice;
	sDeviceManager->get_driver(parent, (driver_module_info**)&acpi,
		(void**)&acpiDevice);
	sDeviceManager->put_node(parent);

	if (acpi == NULL || acpiDevice == NULL) {
		delete[] d->fKeyRing;
		delete_sem(d->fCommandSem);
		mutex_destroy(&d->fLock);
		delete d;
		return B_DEVICE_NOT_FOUND;
	}

	d->fACPI = acpi;
	d->fACPIDevice = acpiDevice;

	// Walk _CRS for IRQ and I/O port resources.
	crs_context crs;
	memset(&crs, 0, sizeof(crs));

	status_t status = acpi->walk_resources(acpiDevice, (char*)"_CRS",
		_crs_callback, &crs);
	if (status != B_OK || crs.irq == 0) {
		delete[] d->fKeyRing;
		delete_sem(d->fCommandSem);
		mutex_destroy(&d->fLock);
		delete d;
		return B_DEVICE_NOT_FOUND;
	}

	d->fIRQ = crs.irq;
	d->fIRQFlags = crs.irqFlags;

	TRACE("cros_ec_kbd: IRQ %d, flags 0x%x\n",
		crs.irq, crs.irqFlags);

	// --- 8042 controller bring-up (§7) ---

	// Step 1: drain output buffer.
	status = _kbc_drain();
	if (status != B_OK) {
		delete[] d->fKeyRing;
		delete_sem(d->fCommandSem);
		mutex_destroy(&d->fLock);
		delete d;
		return status;
	}

	// Step 2: controller self-test (non-fatal, §15.10).
	for (uint32 i = 0; i < kSelfTestRetries; i++) {
		uint8 resp;
		if (_kbc_command(kCtrlSelfTest, NULL, 0, &resp, 1) == B_OK
			&& resp == 0x55) {
			break;
		}
		snooze(50000);
	}

	// Step 3: read stable configuration byte (§15.11).
	status = _kbc_read_stable_config(&d->fInitialConfigByte);
	if (status != B_OK) {
		delete[] d->fKeyRing;
		delete_sem(d->fCommandSem);
		mutex_destroy(&d->fLock);
		delete d;
		return status;
	}

	TRACE("cros_ec_kbd: initial config byte 0x%02x\n", d->fInitialConfigByte);

	// Step 4: quiesce keyboard (disable port + interrupt).
	uint8 config = d->fInitialConfigByte;
	config |= kConfigKbdPortDisable;
	config &= ~kConfigKeyboardIntEnable;
	config |= kConfigTranslationEnable;	// ensure set-1 translation
	config |= kConfigAuxPortDisable;	// no aux device, §15.5

	status = _kbc_command(kCtrlWriteConfig, &config, 1, NULL, 0);
	if (status != B_OK) {
		delete[] d->fKeyRing;
		delete_sem(d->fCommandSem);
		mutex_destroy(&d->fLock);
		delete d;
		return status;
	}

	// Step 8: drain after reconfig.
	_kbc_drain();

	// Step 9: enable keyboard port (but keep interrupt disabled).
	// Device bring-up uses direct polling; enabling the interrupt here would
	// cause the ISR to steal ACK bytes from our polled device commands.
	config = d->fInitialConfigByte;
	config &= ~kConfigKbdPortDisable;
	config &= ~kConfigKeyboardIntEnable;	// interrupt OFF during bring-up
	config |= kConfigTranslationEnable;
	config |= kConfigAuxPortDisable;

	status = _kbc_command(kCtrlWriteConfig, &config, 1, NULL, 0);
	if (status != B_OK) {
		delete[] d->fKeyRing;
		delete_sem(d->fCommandSem);
		mutex_destroy(&d->fLock);
		delete d;
		return status;
	}

	// Step 10: configure the IO-APIC pin for this GSI.
	// install_io_interrupt_handler() ignores trigger/polarity flags, so we
	// must program the IO-APIC redirection entry ourselves. Without this,
	// non-PCI ACPI devices with edge-triggered interrupts won't fire.
	TRACE("cros_ec_kbd: configuring IRQ %d, flags 0x%x\n",
		d->fIRQ, d->fIRQFlags);
	arch_int_configure_io_interrupt(d->fIRQ, d->fIRQFlags);

	// Install interrupt handler (ISR is safe -- fDecodingEnabled
	// is false, so it won't decode stray bytes).
	status = install_io_interrupt_handler(d->fIRQ, cros_ec_interrupt, d, 0);
	TRACE("cros_ec_kbd: install_io_interrupt_handler returned %d\n", status);
	if (status != B_OK) {
		delete[] d->fKeyRing;
		delete_sem(d->fCommandSem);
		mutex_destroy(&d->fLock);
		delete d;
		return status;
	}

	// Defer keyboard commands until first open so early boot can continue
	// reading scan codes directly from the controller.
	d->fOpenCount = 0;
	d->fIRQInstalled = true;

	TRACE("cros_ec_kbd: controller ready on IRQ %d (device bring-up deferred)\n", d->fIRQ);

	*_driverCookie = d;

	TRACE("init_driver OK, cookie %p, IRQ %d\n", d, d->fIRQ);
	return B_OK;
}


static void
cros_ec_uninit_driver(void* _cookie)
{
	driver_cookie* d = static_cast<driver_cookie*>(_cookie);

	// If the device was still open (e.g. driver unload), tear it down.
	if (d->fOpenCount > 0)
		_tear_down_device(d);

	// Remove IRQ handler.
	if (d->fIRQInstalled)
		remove_io_interrupt_handler(d->fIRQ, cros_ec_interrupt, d);

	delete_sem(d->fCommandSem);
	mutex_destroy(&d->fLock);
	delete[] d->fKeyRing;
	delete d;
}


static status_t
cros_ec_register_child_devices(void* _cookie)
{
	driver_cookie* d = static_cast<driver_cookie*>(_cookie);

	return sDeviceManager->publish_device(d->fNode, CROS_EC_KBD_DEVICE_PATH,
		CROS_EC_KBD_DEVICE_MODULE_NAME);
}


driver_module_info cros_ec_keyboard_driver_module = {
	{
		CROS_EC_KBD_DRIVER_MODULE_NAME,
		0,
		NULL
	},

	cros_ec_supports_device,
	cros_ec_register_device,
	cros_ec_init_driver,
	cros_ec_uninit_driver,
	cros_ec_register_child_devices,
	NULL,	// rescan
	NULL,	// device_removed
	NULL,	// suspend
	NULL,	// resume
};





struct open_cookie {
	driver_cookie*	fDriver;
	bool			fIsReader;
	bool			fIsDebugger;
};


static status_t
cros_ec_device_init_device(void* _driverCookie, void** _deviceCookie)
{
	// Driver and device hooks share one state object.
	*_deviceCookie = _driverCookie;
	return B_OK;
}


static void
cros_ec_device_uninit_device(void* _deviceCookie)
{
	(void)_deviceCookie;
}


static status_t
cros_ec_device_open(void* _deviceCookie, const char* path, int openMode,
	void** _cookie)
{
	driver_cookie* d = static_cast<driver_cookie*>(_deviceCookie);
	(void)path;
	(void)openMode;

	TRACE("device_open (cookie %p)\n", d);

	open_cookie* c = new(std::nothrow) open_cookie();
	if (c == NULL)
		return B_NO_MEMORY;

	c->fDriver = d;
	c->fIsReader = false;
	c->fIsDebugger = false;

	int32 prevCount = atomic_add(&d->fOpenCount, 1);

	// First open transfers the controller from early-boot access to this driver.
	if (prevCount == 0 && !d->fDecodingEnabled) {
		status_t status = _bring_up_device(d);
		if (status != B_OK)
			TRACE("cros_ec_kbd: bring-up failed: %d\n", status);
	}

	*_cookie = c;
	return B_OK;
}


static status_t
cros_ec_device_close(void* _cookie)
{
	open_cookie* c = static_cast<open_cookie*>(_cookie);
	if (c == NULL)
		return B_BAD_VALUE;

	driver_cookie* d = c->fDriver;
	if (d != NULL && c->fIsReader)
		d->fHasReader = false;
	if (d != NULL && c->fIsDebugger)
		d->fHasDebugReader = false;

	// Last close returns the controller to direct debugger access.
	if (d != NULL && atomic_add(&d->fOpenCount, -1) == 1)
		_tear_down_device(d);

	return B_OK;
}


static status_t
cros_ec_device_free(void* _cookie)
{
	open_cookie* c = static_cast<open_cookie*>(_cookie);
	delete c;
	return B_OK;
}


static status_t
cros_ec_device_read(void* _cookie, off_t pos, void* buffer, size_t* length)
{
	//! Haiku keyboard drivers deliver events via KB_READ ioctl, not read().
	(void)_cookie;
	(void)pos;
	(void)buffer;
	*length = 0;
	return B_NOT_ALLOWED;
}


static status_t
cros_ec_device_write(void* _cookie, off_t pos, const void* buffer,
	size_t* length)
{
	(void)_cookie;
	(void)pos;
	(void)buffer;
	*length = 0;
	return B_NOT_ALLOWED;
}


static status_t
_read_keyboard_packet(driver_cookie* d, raw_key_info* out, bool isDebugger)
{
	mutex_lock(&d->fLock);

	for (;;) {
		// Handle spontaneous reset recovery.
		if (d->fNeedReinit && d->fDecodingEnabled == false) {
			TRACE("reader: spontaneous reset detected, reinit\n");
			mutex_unlock(&d->fLock);
			_reinit_device(d);
			mutex_lock(&d->fLock);
		}

		// Wait for a key event in the ring buffer.
		while (d->fKeyRingHead == d->fKeyRingTail) {
			// Debugger yields: if a normal reader is active, give it the keys.
			if (isDebugger && d->fHasReader) {
				mutex_unlock(&d->fLock);
				snooze(10000);	// 10 ms yield
				mutex_lock(&d->fLock);
				continue;
			}
			d->fEventCV.Wait(&d->fLock, 0, 0);
		}

		// Ring has data -- read one event.
		*out = d->fKeyRing[d->fKeyRingHead];
		d->fKeyRingHead = (d->fKeyRingHead + 1) % kKeyRingSize;
		break;
	}

	mutex_unlock(&d->fLock);
	return B_OK;
}


static status_t
cros_ec_device_control(void* _cookie, uint32 op, void* buffer, size_t length)
{
	open_cookie* c = static_cast<open_cookie*>(_cookie);
	if (c == NULL)
		return B_BAD_VALUE;

	driver_cookie* d = c->fDriver;
	if (d == NULL)
		return B_BAD_VALUE;

	switch (op) {
		case KB_READ: {
			// Reader exclusivity: first caller becomes the primary reader.
			if (!d->fHasReader && !c->fIsDebugger) {
				c->fIsReader = true;
				d->fHasReader = true;
			} else if (!c->fIsDebugger && !c->fIsReader) {
				return B_BUSY;
			}

			raw_key_info packet;
			status_t status = _read_keyboard_packet(d, &packet,
				c->fIsDebugger);
			if (status != B_OK)
				return status;

			return user_memcpy(buffer, &packet, sizeof(packet));
		}

		case KB_SET_LEDS: {
			if (length < sizeof(led_info))
				return B_BAD_VALUE;

			led_info leds;
			status_t status = user_memcpy(&leds, buffer, sizeof(led_info));
			if (status != B_OK)
				return status;

			uint8 param = 0;
			if (leds.scroll_lock)
				param |= 0x01;
			if (leds.num_lock)
				param |= 0x02;
			if (leds.caps_lock)
				param |= 0x04;

			_kbc_dev_byte(d, kDevSetIndicators, kTimeoutDevAck, true);
			_kbc_dev_byte(d, param, kTimeoutDevAck, true);
			return B_OK;
		}

		case KB_SET_KEY_REPEATING: {
			// 0xFA -- Set All Keys Typematic/Make/Break.
			_kbc_dev_byte(d, 0xFA, kTimeoutDevAck, true);
			return B_OK;
		}

		case KB_SET_KEY_NONREPEATING: {
			// 0xF8 -- Set All Keys Make/Break only.
			_kbc_dev_byte(d, 0xF8, kTimeoutDevAck, true);
			return B_OK;
		}

		case KB_SET_KEY_REPEAT_RATE: {
			if (length < sizeof(uint32))
				return B_BAD_VALUE;

			uint32 rate;
			status_t status = user_memcpy(&rate, buffer, sizeof(uint32));
			if (status != B_OK)
				return status;

			d->fRepeatRate = rate;

			static const uint16 kRateTable[] = {
				33, 37, 42, 46, 50, 54, 58, 63, 67, 75,
				83, 92, 100, 109, 116, 125, 133, 149, 167,
				182, 200, 217, 232, 250, 270, 303, 333,
				370, 400, 435, 470, 500
			};

			uint8 rateIndex = 31;
			for (int32 i = 0; i < 32; i++) {
				if (rate <= kRateTable[i]) {
					rateIndex = static_cast<uint8>(i);
					break;
				}
			}

			uint8 delayIndex = 0;
			if (d->fRepeatDelay >= 1000000)
				delayIndex = 3;
			else if (d->fRepeatDelay >= 750000)
				delayIndex = 2;
			else if (d->fRepeatDelay >= 500000)
				delayIndex = 1;

			uint8 param = rateIndex | (delayIndex << 5);
			_kbc_dev_byte(d, kDevSetAutoRepeat, kTimeoutDevAck, true);
			_kbc_dev_byte(d, param, kTimeoutDevAck, true);
			return B_OK;
		}

		case KB_GET_KEY_REPEAT_RATE: {
			if (length < sizeof(uint32))
				return B_BAD_VALUE;
			return user_memcpy(buffer, &d->fRepeatRate, sizeof(uint32));
		}

		case KB_SET_KEY_REPEAT_DELAY: {
			if (length < sizeof(bigtime_t))
				return B_BAD_VALUE;

			bigtime_t delay;
			status_t status = user_memcpy(&delay, buffer, sizeof(bigtime_t));
			if (status != B_OK)
				return status;

			d->fRepeatDelay = delay;

			// Re-apply the combined rate+delay to hardware.
			static const uint16 kRateTable[] = {
				33, 37, 42, 46, 50, 54, 58, 63, 67, 75,
				83, 92, 100, 109, 116, 125, 133, 149, 167,
				182, 200, 217, 232, 250, 270, 303, 333,
				370, 400, 435, 470, 500
			};

			uint8 rateIndex = 31;
			for (int32 i = 0; i < 32; i++) {
				if (d->fRepeatRate <= kRateTable[i]) {
					rateIndex = static_cast<uint8>(i);
					break;
				}
			}

			uint8 delayIndex = 0;
			if (delay >= 1000000)
				delayIndex = 3;
			else if (delay >= 750000)
				delayIndex = 2;
			else if (delay >= 500000)
				delayIndex = 1;

			uint8 param = rateIndex | (delayIndex << 5);
			_kbc_dev_byte(d, kDevSetAutoRepeat, kTimeoutDevAck, true);
			_kbc_dev_byte(d, param, kTimeoutDevAck, true);
			return B_OK;
		}

		case KB_GET_KEY_REPEAT_DELAY: {
			if (length < sizeof(bigtime_t))
				return B_BAD_VALUE;
			return user_memcpy(buffer, &d->fRepeatDelay, sizeof(bigtime_t));
		}

		case KB_GET_KEYBOARD_ID: {
			if (length < sizeof(uint16))
				return B_BAD_VALUE;
			return user_memcpy(buffer, &d->fKeyboardID, sizeof(uint16));
		}

		case KB_SET_CONTROL_ALT_DEL_TIMEOUT:
		case KB_CANCEL_CONTROL_ALT_DEL:
		case KB_DELAY_CONTROL_ALT_DEL:
			return B_OK;

		case KB_SET_DEBUG_READER: {
			if (d->fHasDebugReader)
				return B_BUSY;

			c->fIsDebugger = true;
			d->fHasDebugReader = true;
			return B_OK;
		}

		default:
			return B_DEV_INVALID_IOCTL;
	}
}


device_module_info cros_ec_keyboard_device_module = {
	{
		CROS_EC_KBD_DEVICE_MODULE_NAME,
		0,
		NULL
	},

	cros_ec_device_init_device,
	cros_ec_device_uninit_device,
	NULL,

	cros_ec_device_open,
	cros_ec_device_close,
	cros_ec_device_free,
	cros_ec_device_read,
	cros_ec_device_write,
	NULL,	// io
	cros_ec_device_control,
	NULL,	// select
	NULL,	// deselect
};





module_dependency module_dependencies[] = {
	{ B_DEVICE_MANAGER_MODULE_NAME, (module_info**)&sDeviceManager },
	{}
};


module_info* modules[] = {
	(module_info*)&cros_ec_keyboard_driver_module,
	(module_info*)&cros_ec_keyboard_device_module,
	NULL
};


extern "C" status_t
std_ops(int32 op, ...)
{
	switch (op) {
		case B_MODULE_INIT:
			if (get_module(B_DEVICE_MANAGER_MODULE_NAME,
					(module_info**)&sDeviceManager) != B_OK)
				return B_ENTRY_NOT_FOUND;
			TRACE("cros_ec_kbd: module init\n");
			return B_OK;

		case B_MODULE_UNINIT:
			put_module(B_DEVICE_MANAGER_MODULE_NAME);
			sDeviceManager = NULL;
			TRACE("cros_ec_kbd: module uninit\n");
			return B_OK;

		default:
			break;
	}
	return B_ERROR;
}
