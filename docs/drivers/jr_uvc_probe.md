<!--
SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
SPDX-License-Identifier: MIT
SPDX-FileContributor: Generated with GitHub Copilot (GPT-5.6 Sol)
-->

# `jr_uvc_probe`

`jr_uvc_probe` is a standalone userspace fitness sweep for Winky's internal UVC
camera. It is not a Media Kit add-on. Its descriptor, control, payload, and
validation core lives under `overlay/bin/jr_uvc_probe/core/`.

The Winky BSP installs the binary at `/boot/system/bin/jr_uvc_probe`. It opens
only the public USB Kit API and matches the internal `2232:1068` camera. By
default it discovers the camera, records its complete raw USB descriptor
evidence and processing controls, and exercises every standard YUY2 and MJPEG
mode from lowest estimated bandwidth upward. The vendor `0x0e`/`0x0f` M420,
2VUY, and 3VUY descriptors are preserved in the report but are not streamed.

```sh
jr_uvc_probe
jr_uvc_probe --output /boot/home/camera-run --frames 5
jr_uvc_probe --enumerate-only
jr_uvc_probe --device /dev/bus/usb/0/2
```

For hardware collection, use the installed wrapper:

```sh
jr_uvc_collect
jr_uvc_collect /boot/home 3
jr_uvc_collect "/My USB Disk" 3
```

The first argument is the output root and the optional second argument is the
number of valid frames requested per mode. The wrapper creates
`jr_uvc-run-<timestamp>/`, stores the probe artifacts, console transcript, and
exit status inside it. It also captures before/after USB inventories and system
logs, recording unavailable diagnostics explicitly. The wrapper writes
`jr_uvc-run-<timestamp>.zip` beside the directory when `zip` is available.

The default output directory is `/boot/home/jr_uvc-<timestamp>`. The tool writes
`jr_uvc_report.json` plus the first valid sample from each passing mode as
`jr_uvc_<mode-id>.yuy2` or `.jpg`.

## Capture contract

Each mode starts at alternate zero, negotiates a 26-byte UVC 1.0 Probe, commits
the returned and fixed-up control, and selects the largest single-transaction
alternate. Payload capacity is decoded directly from raw `wMaxPacketSize`:

```text
((raw >> 11 & 3) + 1) * (raw & 0x7ff)
```

If the negotiated payload exceeds that alternate, the report preserves the
device's value and records `high_bandwidth_avoided`; it does not represent the
compatibility transfer as full-bandwidth coverage. The synchronous USB Kit
isochronous call uses 16 equal, fixed request slots. Packet `i` always begins at
`buffer + i * request_length`; actual lengths never pack the slots.

The report retains every packet status and actual length up to the explicit
diagnostic bound, payload assembly counters, negotiated controls, alternate
selection, validation status, frame size and timing, cleanup result, and sweep
summary. Capture has one recovery retry. The synchronous transfer runs in a
dedicated userspace thread;
a bounded wait sends `SIGKILLTHR` through `kill_thread()` on timeout, which is
the kill signal supported by the raw USB driver's `B_KILL_CAN_INTERRUPT` wait.
If the killed worker still cannot join within one second, the tool records the
abandoned worker, writes the partial report, and exits the team without running
USB object destructors; it never continues a sweep with a live transfer.
Every mode exit returns to alternate zero and waits 100 ms; failure to do so
aborts the sweep as a wedged or lost device.

A requested mode counts as tested only when the camera's Probe response returns
the exact requested format, frame, and interval. Firmware substitution is
recorded as `FAIL_NEGOTIATED_MISMATCH`, not a pass for the requested tuple.

The optional processing-control census runs only after the mode sweep. For UVC
1.0 it uses the standard descriptor bitmap and fixed control sizes instead of
issuing UVC 1.1-style `GET_INFO`/`GET_LEN` requests. It stops after the first
stalled control request and records the incomplete census without risking later
capture work.

YUY2 frames must be exactly `width * height * 2`. MJPEG requires SOI and a SOF
marker, stays within the negotiated size, and records a missing EOI as a warning
rather than discarding an otherwise useful bring-up sample.

Haiku's xHCI path rejects Winky's multiplier-bearing alternates 4 through 7.
The camera requires 3072 bytes per microframe on alternate 7, while stock xHCI
programs raw high-speed `wMaxPacketSize` (`0x1400`) as 5120 bytes instead of
separating the 1024-byte payload from its three-transaction multiplier.

Until that general Haiku xHCI fix lands, the probe deliberately selects the
largest single-transaction alternate (alternate 3, 1024 bytes per microframe)
and records `high_bandwidth_avoided`.

The single-transaction fallback negotiates and activates modes but receives
header-only packets because it cannot satisfy the committed 3072-byte payload
contract. No mode produces a valid frame.

Winky camera streaming requires a generic Haiku xHCI high-bandwidth
isochronous fix. The PCI routing filter cannot alter endpoint-context encoding
inside stock xHCI, and Media Kit cannot repair transport below USB Kit. No
camera Media Kit add-on is shipped.

Exit status is 0 only after at least one active mode passes, 1 after enumeration
with no passing mode or a fatal sweep error, and 2 when the exact camera cannot
be found or opened.
