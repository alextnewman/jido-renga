# Atmel maXTouch touchpad driver

`i2c_atmel_mxt` is a Haiku input driver for the Atmel maXTouch clickpad used by
Winky. It binds to an I2C child with ACPI HID or CID `ATML0000` and publishes a
standard Haiku touchpad device.

The Winky image uses a small native I2C bus shim that omits absent ACPI HID/CID
attributes during child-node registration. Haiku's device manager compares
registered string attributes with `strcmp()`, so publishing a successful null
string makes a later scan unsafe before any leaf driver's support callback runs.
The Atmel callback independently treats missing or null attributes as absent.

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
5. flush the ordered output frames into the event ring;
6. repeat until T44 reports an empty queue.

This keeps I2C out of interrupt context and leaves the driver asleep when the
touchpad is idle. The input server blocks on a separate condition variable until
the ring contains an event. The empty check and condition-variable wait share
the ring mutex so a final release frame cannot be stranded by a lost wakeup.

## Touch state

`TouchEngine` has two deliberately separate responsibilities:

1. maintain authoritative controller contact and button state;
2. adapt that state into ordered frames that match Haiku's touchpad contract.

T9 and T100 report IDs identify persistent contact slots. DETECT activates or
updates a slot, while DETECT clear releases it immediately. An absent report ID
does nothing because a T44 drain contains state changes, not a complete contact
snapshot.

The selected active slot remains the representative primary contact until it
lifts. Output coordinates come from that primary rather than a centroid, so
adding or removing a second finger cannot move the pointer by itself. If the
primary lifts while another contact remains, the engine emits a zero-contact
reset followed by a replacement baseline. Haiku therefore starts tracking the
new primary without interpreting the coordinate change as movement or scroll.

Important Winky behavior:

- T9 does not reliably assert its PRESS flag. A contact becomes active when its
  report has the DETECT bit set.
- T9 reports lift with DETECT clear, often alongside RELEASE.
- Physical depression can release one or both contact slots before the delayed
  T19 button press arrives. On such a release, the engine may retain a recent
  two-contact frame as an output-only click candidate. A T19 press within 150 ms
  uses that frame for Haiku's click-finger classification, but the released
  controller slots remain released. This lets a physical two-finger press become
  a right-click without creating ghost contacts or delaying ordinary lifts.
- T19 reports the physical clickpad button on active-low bit 5. T19 messages are
  change-only, so the last button value persists across drain cycles.
- T9 and T100 coordinates are clamped to the discovered hardware range before
  the board orientation transform is applied.

The engine reports the active-slot bitmap through `fingers` and the primary
contact's coordinates and pressure. Haiku's input server remains responsible for
two-finger scrolling and physical click-finger policy.

One T44 drain can require more than one Haiku frame. Primary replacement,
multi-finger button release, and multiple T19 edges are queued in controller
order. The worker emits required zero-contact resets and suppresses only
redundant idle zeros.

Physical button state changes are emitted even when no contacts remain. This
ensures a click released after both fingers lift cannot leave the input server
replaying the prior pressed state. An incomplete I2C drain similarly resets the
touch engine and emits a zero-contact frame rather than retaining uncertain
contacts. A full output queue is reported as an error only after the remaining
hardware messages have been drained, preventing an asserted interrupt from
wedging the device.

## Current support

The authoritative-slot and ordered-frame implementation is validated on Winky
for smooth pointer movement, physical clickpad input, two-finger scrolling, and
physical two-finger right-click without cursor jumps or loss of interaction.
The controller lifecycle is also covered by host-side message-sequence tests.

Current limitations:

- Suspend/resume hooks are not implemented.
- Scrolling and most gesture interpretation remain in Haiku's input server.
- Object-table CRC helpers are not used at runtime and are not hardware
  validated.
- maXTouch variants and board orientation outside Winky have not been
  validated.
