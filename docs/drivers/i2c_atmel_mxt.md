# Atmel maXTouch touchpad driver

`i2c_atmel_mxt` is a Haiku input driver for the Atmel maXTouch clickpad used by
Winky. It binds to an I2C child with ACPI HID or CID `ATML0000` and publishes a
standard Haiku touchpad device.

The Winky image uses Haiku's stock I2C bus manager. The driver's support
callbacks treat missing or null device attributes as absent and do not require a
replacement bus manager.

## Device discovery

Initialization reads the maXTouch information block and object table, then
caches the objects used by the driver:

| Object | Purpose |
|---|---|
| T5 | Message processor |
| T6 | Command processor |
| T7 | Power configuration |
| T9 | Legacy multitouch contacts |
| T18 | Communications configuration |
| T19 | Clickpad GPIO state |
| T44 | Pending-message count |
| T100 | Modern multitouch contacts |

The object table determines register addresses, message sizes, and report-ID
ranges. Touch resolution comes from the active T9 or T100 configuration.

Configuration writes that affect operation are read back and verified. Device
initialization fails when a required register cannot be programmed.

## Interrupt and worker model

The maXTouch interrupt is level-triggered while messages remain queued. The ISR
does not perform I2C work; it only wakes the worker.

The worker owns message I/O:

1. acquire the I2C bus;
2. read T44 and the first T5 message in one transaction;
3. read the remaining queued T5 messages;
4. pass every message to `TouchEngine`;
5. flush one aggregate touch state into the event ring;
6. repeat until T44 reports an empty queue.

This keeps I2C out of interrupt context and leaves the driver asleep when the
touchpad is idle. The input server blocks on a separate condition variable until
the ring contains an event.

## Touch state

`TouchEngine` maintains contact state across queue drains instead of treating
each message batch as an independent sample.

Important Winky behavior:

- T9 does not reliably assert its PRESS flag. A contact becomes active when its
  report ID first appears.
- Lift is represented by contact absence in the next drain cycle. Active
  contacts not refreshed during a cycle are released.
- T19 reports the physical clickpad button on active-low bit 5. T19 messages are
  change-only, so the last button value persists across drain cycles.
- T9 and T100 coordinates are clamped to the discovered hardware range before
  the board orientation transform is applied.

The engine aggregates active contacts into a centroid, average pressure, and
finger mask. A two-contact lift with bounded movement is emitted in the form
expected by Haiku's click-finger emulator for a right click.

The worker emits one zero-contact event when a touch stanza ends. That event is
required so the input server clears its previous position before the next touch;
redundant idle zeros are suppressed.

## Current support

The driver is validated on Winky for pointer movement, physical clickpad input,
multi-contact reporting, and two-finger tap right-click behavior.

Current limitations:

- Suspend/resume hooks are not implemented.
- Scrolling and most gesture interpretation remain in Haiku's input server.
- Object-table CRC helpers are not used at runtime and are not hardware
  validated.
- maXTouch variants and board orientation outside Winky have not been
  validated.
