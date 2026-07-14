# ChromeOS EC keyboard driver

`cros_ec_keyboard` is a Haiku input driver for the 8042-compatible keyboard
interface exposed by the ChromeOS Embedded Controller on Winky.

## Hardware contract

The ACPI keyboard device has:

- HID `GOOG000A`;
- PS/2-compatible CIDs `PNP0303` and `PNP030B`;
- controller ports `0x60` and `0x64`;
- a board-routed interrupt described by ACPI `_CRS`, rather than legacy IRQ 1.

The driver reads the interrupt number and trigger/polarity flags from `_CRS`.
Winky routes this signal to GSI 53, but the driver does not hard-code that
number.

The EC presents a translated set-1 scan stream through an 8042-class controller.
The driver publishes:

```text
/dev/input/keyboard/cros_ec/0
```

and implements Haiku's keyboard ioctls, including `KB_READ`, LED control,
repeat-rate configuration, keyboard identity, and debugger-reader selection.

## Ownership and initialization

The add-on binds as an ACPI driver and owns the keyboard side of the controller.
It is self-contained; it does not carry a modified Haiku PS/2 bus manager.

Controller setup occurs during driver initialization:

1. drain the output buffer;
2. perform the controller self-test;
3. read a stable controller configuration byte;
4. disable keyboard interrupts while command responses are polled;
5. configure the IO-APIC entry from the ACPI interrupt flags;
6. install the interrupt handler.

Keyboard-device setup is deferred until the first open. This preserves direct
controller access used by early boot and the kernel debugger. The first opener
resets and identifies the keyboard, selects scan set 2, enables controller
translation to set 1, configures repeat and LEDs, enables scanning, and finally
arms the keyboard interrupt.

The last close disables scanning and restores the controller to a state suitable
for direct debugger access.

## Interrupt and command paths

The ISR drains the output buffer completely for each interrupt. Auxiliary bytes
are ignored because this interface has no attached PS/2 pointing device.

Two data paths share the ISR:

- During a keyboard command, response bytes are written to a command slot and a
  semaphore wakes the issuing thread.
- During normal input, scan bytes are decoded and appended to a 128-entry
  `raw_key_info` ring. A condition variable wakes the reader.

The command semaphore and event condition variable are separate because command
responses require ordered delivery while key events are stored in the ring.

## Scan-code decoder

The decoder handles:

- ordinary set-1 make and break bytes;
- `E0` extended keys;
- the `E1` Pause/Break sequence;
- tolerated `F0` break prefixes;
- spontaneous reset, overrun, ACK, and resend sentinels;
- make/break values that collide with protocol response bytes.

Collision tracking records whether a matching make was seen before treating a
break byte such as `0xFA` or `0xAA` as a key event. Extended bytes are never
reinterpreted as protocol responses.

A spontaneous keyboard reset disables decoding and schedules reinitialization
from reader context.

## Current support

The driver is validated on Winky for normal typing, modifiers, extended keys,
repeat configuration, and early-boot/debugger handoff.

Current limitations:

- Chromebook top-row action keys are exposed as ordinary function keys.
- Keyboard backlight control is outside the 8042 interface and is not
  implemented.
- Suspend/resume wake handling is not implemented.
- Alt/SysRq state is tracked, but magic SysRq dispatch is not implemented.
- Other Chromebook EC keyboard implementations have not been validated.
