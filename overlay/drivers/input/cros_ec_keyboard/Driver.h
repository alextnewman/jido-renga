// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with Qwen 3.6

//! ChromeOS EC keyboard driver -- native driver for the 8042-class interface
//! emulated by the ChromeOS Embedded Controller (WINKY / Chromebook 2).
//
// The EC presents a standard i8042 controller at ports 0x60/0x64 with an
// attached PS/2 keyboard device in translated set-1 mode.  The only
// Chromebook-specific quirk is that the keyboard interrupt is routed through
// a GPIO to a dedicated GSI (vector 53 on WINKY) instead of legacy IRQ 1.
//
// This driver binds to the ACPI keyboard device (HID GOOG000A), reads the GSI
// from _CRS, installs an ISR on that vector, and produces raw_key_info events
// delivered via KB_READ ioctl -- matching the Haiku keyboard driver convention.


#ifndef CROS_EC_KEYBOARD_DRIVER_H
#define CROS_EC_KEYBOARD_DRIVER_H


#include <ACPI.h>
#include <OS.h>
#include <arch/x86/arch_cpuasm.h>
#include <condition_variable.h>
#include <keyboard_mouse_driver.h>
#include <lock.h>


// Debug tracing (disabled by default, enable for bring-up).
//#define TRACE_CROS_EC_KBD
#ifdef TRACE_CROS_EC_KBD
#	define TRACE(x...)	dprintf("cros_ec_kbd: " x)
#else
#	define TRACE(x...)
#endif


// constants


#define CROS_EC_KBD_DRIVER_MODULE_NAME \
	"drivers/input/cros_ec_keyboard/driver_v1"
#define CROS_EC_KBD_DEVICE_MODULE_NAME \
	"drivers/input/cros_ec_keyboard/device_v1"
#define CROS_EC_KBD_DEVICE_PATH "input/keyboard/cros_ec/0"

// ACPI Hardware ID for the ChromeOS EC keyboard device (PS2K in coreboot DSDT)
#define CROS_EC_KBD_ACPI_HID "GOOG000A"

// Compatible IDs (from _CID package in DSDT)
#define CROS_EC_KBD_ACPI_CID_PNP0303 "PNP0303"
#define CROS_EC_KBD_ACPI_CID_PNP030B "PNP030B"


// Standard port addresses (confirmed by _CRS on WINKY)
#define CROS_EC_KBD_DATA_PORT 0x60
#define CROS_EC_KBD_CMD_PORT  0x64

// Controller status register bits (read 0x64)
enum KbcStatusBits {
	kStatusOutputBufferFull	= 0x01,
	kStatusInputBufferFull	= 0x02,
	kStatusSystemFlag		= 0x04,
	kStatusCommandData		= 0x08,
	kStatusNotInhibited		= 0x10,
	kStatusAuxiliaryData	= 0x20,
	kStatusTimeout			= 0x40,
	kStatusParity			= 0x80,
};

// Controller configuration byte bits
enum KbcConfigBits {
	kConfigKeyboardIntEnable	= 0x01,
	kConfigAuxIntEnable		= 0x02,
	kConfigSystemFlag		= 0x04,
	kConfigIgnoreInhibit	= 0x08,
	kConfigKbdPortDisable	= 0x10,
	kConfigAuxPortDisable	= 0x20,
	kConfigTranslationEnable	= 0x40,
};

// Controller commands (write to 0x64)
enum KbcControllerCommands {
	kCtrlReadConfig		= 0x20,
	kCtrlWriteConfig	= 0x60,
	kCtrlSelfTest		= 0xAA,
	kCtrlKbdInterfaceTest = 0xAB,
	kCtrlDisableKbdPort	= 0xAD,
	kCtrlEnableKbdPort	= 0xAE,
};

// Keyboard device opcodes (write to 0x60, keyboard port selected)
enum KbcDeviceCommands {
	kDevSetIndicators	= 0xED,
	kDevScanCodeSet		= 0xF0,
	kDevIdentify		= 0xF2,
	kDevSetAutoRepeat	= 0xF3,
	kDevEnableScanning	= 0xF4,
	kDevDisableScanning	= 0xF5,
	kDevReset			= 0xFF,
};

// Device response/sentinel bytes
enum KbcDeviceResponses {
	kRespAck			= 0xFA,
	kRespResend		= 0xFE,
	kRespSelfTestPassed	= 0xAA,
	kRespError			= 0xFC,
};

// Scan-code prefixes
#define kPrefixE0 0xE0
#define kPrefixE1 0xE1

// Pause/Break key keycode (matches kATKeycodeMap[0xC6 - 1] used by the PS/2 driver).
#define kPauseKeycode 0x10

// Emergency keys (Alt+SysRq tracking, matches ps2_keyboard.cpp).
#define kLeftAltScan	0x38
#define kRightAltScan	0xB8

// Emergency key status bitmasks
enum EmergencyKeyBits {
	kEmergencyLeftAlt	= 0x01,
	kEmergencyRightAlt	= 0x02,
};


// Poll spin interval for status waits (µs)
#define kPollDelayUs 50

// Controller command timeout (500 ms)
#define kTimeoutCtrl 500000

// Device ACK timeout (200 ms)
#define kTimeoutDevAck 200000

// Device response timeout (500 ms)
#define kTimeoutDevResp 500000

// Device reset ACK timeout (1 s)
#define kTimeoutResetAck 1000000

// Device reset self-test result timeout (4 s)
#define kTimeoutResetBat 4000000

// Max bytes to discard in drain
#define kDrainLimit 16

// Self-test retry count
#define kSelfTestRetries 5

// Max config-byte reads before giving up on stability
#define kConfigStableReads 10



// Key event ring buffer size (power of 2, ISR-safe with mutex in reader)
#define kKeyRingSize 128


// Scan-code values that collide with device protocol bytes (§11.5).
// These make codes, when released (bit 7 set), equal a protocol byte:
//   0xFA (ACK), 0xFE (Resend), 0xAA (BAT), 0xFF (error), 0xF1, 0xF2
enum KbcCollisionCodes {
	kCollisionFA = 0x5A,	// 0x5A | 0x80 = 0xFA (ACK)
	kCollisionFE = 0x5E,	// 0x5E | 0x80 = 0xFE (Resend)
	kCollisionAA = 0x2A,	// 0x2A | 0x80 = 0xAA (BAT)
	kCollisionFF = 0x7F,	// 0x7F | 0x80 = 0xFF (error)
	kCollisionF1 = 0x71,	// 0x71 | 0x80 = 0xF1 (language key)
	kCollisionF2 = 0x72,	// 0x72 | 0x80 = 0xF2 (language key)
};





struct driver_cookie {
//! Device open count — at offset 0 for guaranteed alignment (atomic_add).
int32						fOpenCount;

device_node*				fNode;
acpi_device_module_info*	fACPI;
acpi_device					fACPIDevice;

uint8						fIRQ;
uint32					fIRQFlags;
bool						fIRQInstalled;

//! Saved initial config byte (from firmware, for suspend restore).
uint8						fInitialConfigByte;

// Decoder state (ISR-only, no locking -- ISR has exclusive access).
bool						fDecodingEnabled;
int8						fExtendedMode;	// 0=none, 1=E0, 2=E1
bool						fIsRelease;
uint8						fPauseSeqIndex;

//! Per-code collision tracking bits (§11.5).  Set on make, cleared on break.
bool						fCollisionFA;
bool						fCollisionFE;
bool						fCollisionAA;
bool						fCollisionFF;
bool						fCollisionF1;
bool						fCollisionF2;

//! Command mode: ISR routes bytes to command response, not decoder.
//! int32 so that atomic_set/atomic_get can be used from the thread side;
//! the ISR reads it as a plain load (int32 loads are atomic on all targets).
int32						fCommandMode;

//! Semaphore signaled by ISR when a command response byte arrives.
sem_id					fCommandSem;

//! Last byte captured by ISR for the command state machine.
uint8						fCommandByte;

//! Spontaneous reset detected during ISR -- triggers re-init in reader thread.
bool						fNeedReinit;

//! Emergency key status bitmask (§kEmergencyKeyBits). Tracks L-Alt, R-Alt,
//! SysRq press state for Alt+SysRq emergency-key detection (matches PS/2 driver).
int							fEmergencyKeyStatus;

// Key event ring buffer (protected by fLock).
raw_key_info*				fKeyRing;
int32						fKeyRingHead;
int32						fKeyRingTail;

mutex						fLock;
ConditionVariable			fEventCV;

//! Reader exclusivity: only one primary reader at a time.
bool						fHasReader;
bool						fHasDebugReader;

// Keyboard state (for ioctl handlers).
uint16					fKeyboardID;
uint32					fRepeatRate;		// ms per repeat
bigtime_t					fRepeatDelay;		// initial delay in us
bool						fScanningEnabled;
};





static inline uint8
kbc_read_status()
{
	return in8(CROS_EC_KBD_CMD_PORT);
}


static inline uint8
kbc_read_data()
{
	return in8(CROS_EC_KBD_DATA_PORT);
}


static inline void
kbc_write_command(uint8 cmd)
{
	out8(cmd, CROS_EC_KBD_CMD_PORT);
}


static inline void
kbc_write_data(uint8 data)
{
	out8(data, CROS_EC_KBD_DATA_PORT);
}





//! Controller: wait until IBF clear (writable).
static status_t _kbc_wait_writable();

//! Controller: wait until OBF set (readable).
static status_t _kbc_wait_readable();

//! Controller: drain output buffer.
static status_t _kbc_drain();

//! Synchronous controller command (write cmd to 0x64, optional params/response
//! via 0x60).
static status_t _kbc_command(uint8 cmd, const uint8* params,
	size_t paramCount, uint8* response, size_t responseCount);

//! Read config byte until two consecutive reads agree (§15.11).
static status_t _kbc_read_stable_config(uint8* config);

//! Send a byte to the keyboard device with ACK/Resend retry.
//! If useISR is true, uses ISR+semaphore; otherwise direct polling.
static status_t _kbc_dev_byte(driver_cookie* d, uint8 byte,
	bigtime_t ackTimeout, bool useISR);

//! Direct-polling response wait (used during bring-up and re-init).
static status_t _kbc_dev_response_direct(uint8* outByte, bigtime_t timeout);

//! Decode a scan code byte (set-1 translated) and enqueue a raw_key_info event.
//! Full spec compliance: E0/E1 prefixes, collision disambiguation, stray ACK
//! tolerance, spontaneous reset detection.
static void _cros_ec_decode_byte(driver_cookie* d, uint8 byte);

//! Bring up the keyboard device (reset, identify, scan set, enable scanning).
//! Called on first open (lazy init).
static status_t _bring_up_device(driver_cookie* d);

//! Tear down the keyboard device (disable scanning, disarm interrupt).
//! Called on last close.
static void _tear_down_device(driver_cookie* d);

//! Re-initialize the keyboard device (scan set, indicators, repeat, scanning).
//! Called on spontaneous reset detection or after protocol failure.
static status_t _reinit_device(driver_cookie* d);


#endif	// CROS_EC_KEYBOARD_DRIVER_H
