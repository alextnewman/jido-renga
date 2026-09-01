<!--
SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
SPDX-License-Identifier: MIT
SPDX-FileContributor: Generated with GitHub Copilot
-->

# Bay Trail xHCI PCI filter

`byt_xhci_filter` is an out-of-tree PCI filter for Intel Bay Trail xHCI
function `8086:0f35`. It does not contain or replace an xHCI implementation.
Haiku's stock `xhci` add-on remains unchanged at its canonical package and boot
paths.

The filter claims only the Bay Trail xHCI PCI node, then publishes a child with
the standard `pci_device_module_info` interface. Haiku's ordinary device-manager
scan binds stock `xhci` to that child. Every PCI operation delegates to the real
parent PCI device.

The only policy intervention concerns Intel's legacy EHCI-to-xHCI routing
sequence. Stock xHCI reads the USB2 and USB3 routing masks and copies them into
the live routing registers. Winky has no visible EHCI companion: coreboot has
already routed all ports to xHCI and hidden EHCI. Bay Trail-M also has incorrect
port-map fuse data, so replaying those masks can discard coreboot's valid
routing.

For 32-bit reads only, the filter substitutes:

| Requested register | Read from |
|---|---|
| USB2 routing mask `0xd4` | live USB2 route `0xd0` |
| USB3 routing mask `0xdc` | live USB3 enable `0xd8` |

The subsequent stock-driver writes therefore preserve the live values. All
writes and every other read are passed through unchanged.

## Boot diagnostics

The filter uses the shared Jidō Renga kernel diagnostic format with label
`byt_xhci_filter`. Its ordered events distinguish load-path failures:

1. `module discovered by kernel loader` — the module list found and initialized
   the exported driver module;
2. `matched PCI 8086:0f35 ...` — device-manager probing reached the Winky xHCI
   function and the filter reported a positive match;
3. `registering delegated PCI child for stock xHCI` — creation of the synthetic
   PCI node began after the filter was selected;
4. `bound: USB2=...` — the child initialized against the real parent PCI
   interface;
5. two `config read` lines — stock xHCI consumed the substituted routing masks.
6. `delegated PCI node registration returned B_OK` — creation of the synthetic
   PCI node returned successfully. Haiku's dynamic-driver scan can mask a
   downstream driver's registration failure, so this line alone does not prove
   that stock xHCI completed initialization.

If the first line is absent, inspect the boot-module package/link. If only the
first line appears, inspect the PCI identity and device-manager node tree.

## Scope and removal

This Winky-specific filter is a compatibility layer, not an xHCI fork. It is
needed while stock Haiku transfers Intel port routing without checking for an
EHCI companion. Remove the filter and its BSP entries when the captive Haiku
revision contains equivalent routing policy.

## Supported behavior and limitations

External USB devices enumerate after insertion, and the internal camera
enumerates as UVC 1.0 device `2232:1068`.

Camera streaming is outside this filter's scope. The camera requires a
three-transaction high-speed isochronous endpoint, which stock Haiku xHCI
currently encodes incorrectly. A single-transaction userspace fallback reaches
the device but receives header-only UVC packets. See
[`jr_uvc_probe.md`](jr_uvc_probe.md).

Internal Bluetooth, boot-present USB devices, repeated hotplug, and both
external ports are not qualified. Camera streaming is blocked on the generic
xHCI high-bandwidth isochronous issue described in
[`jr_uvc_probe.md`](jr_uvc_probe.md).
