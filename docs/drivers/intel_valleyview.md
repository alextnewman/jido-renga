# Intel ValleyView graphics driver

`intel_valleyview` is the native Haiku graphics driver for the ValleyView GPU in
Winky. It owns PCI device `8086:0f31`, adopts the firmware-lit eDP panel on DP_C
and pipe A, and publishes a single native 1366x768 RGB32 mode with a 5504-byte
stride.

The driver, accelerant, and diagnostic tool install at their canonical Haiku
paths:

```text
/boot/system/add-ons/kernel/drivers/dev/graphics/intel_valleyview
/boot/system/add-ons/accelerants/intel_valleyview.accelerant
/boot/system/bin/intel_valleyview_probe
```

## Firmware-gated takeover

The driver does not perform a general modeset. It accepts only the exact
firmware state validated on Winky:

- a locked DPLL and enabled pipe A;
- native 1366x768 panel timing, 1530x793 total;
- eDP enabled on DP_C;
- the firmware 1024x768 RGB32 source and 4096-byte stride;
- an enabled panel fitter in firmware AUTO mode;
- panel power and PWM state consistent with the captured snapshot;
- a complete, present GGTT mapping for the firmware framebuffer;
- no active firmware cursor.

Every relevant live register is rechecked immediately before takeover. Unknown
or stale state is rejected without modifying the display. Takeover saves the
firmware plane, fitter, cursor, CxSR, PWM, and GGTT state before installing any
candidate mapping.

## Presentation architecture

Haiku's local app_server draws into its own cached backbuffer, then copies each
damaged rectangle row by row into the framebuffer returned by the accelerant.
Returning the live write-combined scanout therefore exposes each partial damage
copy as it happens.

`intel_valleyview` instead returns a cached write-back shadow framebuffer.
app_server's damage copies land in that shadow and are never scanned out
directly. A display-priority kernel worker presents complete frames:

1. select the scanout not named by `DSPASURFLIVE`;
2. copy the full shadow into that inactive scanout;
3. wait for the BCS `MI_FLUSH_DW` completion marker when BCS performs the copy;
4. arm the completed surface through `DSPASURF`;
5. retain ownership of the target until `DSPASURFLIVE` confirms the latch;
6. repeat with the other scanout.

The live panel only sees complete scanout surfaces. The confirmed latch naturally
paces the worker at the display refresh rate. A delayed latch remains pending;
the target is never reused or overwritten while hardware may still adopt it.

The resulting desktop, window drawing, text rendering, and cursor motion are
hardware-validated as fast and smooth on Winky.

## GGTT and cache contract

The native framebuffer footprint is 1032 pages. P0 reserves three such ranges
plus eight private pages at the top of the 256 MiB GMADR aperture:

| Range | GGTT offset | CPU mapping | GGTT policy |
|---|---:|---|---|
| app_server shadow | `0x0f3e0000` | write-back, cloneable | writable, CPU-cache snooped |
| scanout 0 | `0x0f7e8000` | write-combining, private | writable, non-snooped |
| scanout 1 | `0x0fbf0000` | write-combining, private | writable, non-snooped |
| 64x64 cursor | `0x0fff8000` | private | writable, non-snooped |
| BCS ring | `0x0fffc000` | private | writable, non-snooped |
| hardware status | `0x0fffd000` | private | writable, non-snooped |
| BCS test source | `0x0fffe000` | private | writable, non-snooped |
| BCS test destination | `0x0ffff000` | private | writable, non-snooped |

Physical pages are allocated at boot and vary between runs. The fixed GGTT
layout keeps cursor, ring, status, and test addresses at their proven top-of-
aperture locations while the larger presentation allocation grows downward.

The shadow's snooped PTEs make app_server's cached writes coherent with BCS on
non-LLC ValleyView. Scanouts remain write-combined and non-snooped because they
are display destinations, not CPU rendering surfaces.

## BCS presentation and fallback

Before the graphics node is published, the driver proves the complete
presentation path against the inactive scanout. It writes a deterministic
per-pixel pattern with distinct row padding into the cached shadow, performs a
full-frame BCS copy, and verifies every visible destination pixel and untouched
padding through the CPU mapping.

A successful test enables BCS presentation. If data verification or a safely
restored BCS submission fails, presentation uses a full-frame CPU copy instead.
The same page-flip and ownership protocol applies to both copy engines. An unsafe
ring cleanup faults the candidate rather than pretending the CPU fallback is
safe.

The runtime BCS path is synchronous. Each submission acquires forcewake, installs
the private ring and hardware-status page, waits for its completion marker,
restores the prior ring and wake state, and only then permits the surface flip.

## Cursor, brightness, and DPMS

The accelerant implements Haiku's preferred `B_SET_CURSOR_BITMAP` hook. Default
RGBA cursors are converted into Intel's 64x64 ARGB surface format and programmed
with ValleyView cursor mode `0x27`. The cursor plane moves independently of
frame presentation and is hardware-validated for smooth motion.

The driver preserves the firmware PWM period and exposes normalized brightness
control. Soft DPMS blanks the backlight, cursor, and primary plane without
power-cycling the panel link. Blank and unblank operations serialize with the
present worker. A scanout is not reused after blanking until the plane is
disabled and the live surface no longer names any P0 framebuffer.

## Locking and teardown

The lock hierarchy is:

```text
device.lock -> renderLock -> bcsLock
device.lock -> presentLock -> bcsLock
```

The present worker takes `presentLock -> bcsLock` and never takes `device.lock`.
`renderLock` and `presentLock` never nest. The RCS diagnostic binds both hidden
allocations under the render path, releases `renderLock`, then freezes
presentation under `presentLock -> bcsLock` while sampling display state and
using RCS. Every forcewake, engine-ring, and diagnostic GGTT operation takes
`bcsLock`.

Shutdown joins the present worker before quiescing BCS or restoring display
state. The candidate cursor is detached, BCS is quiesced, the firmware plane is
restored and observed live, and only then are the original GGTT PTEs reinstalled.
The saved firmware cursor state is restored and observed after its candidate
mapping is gone.

If the worker, cursor, BCS ring, plane, or GGTT cannot be proven detached, every
P0 allocation is quarantined. The kernel never returns a page to the allocator
while the GPU or display may still reference it.

## Diagnostics

`intel_valleyview_probe --p0-status` reports:

- native, BCS, and presentation status;
- shadow, scanout, cursor, ring, and status addresses;
- programmed and live plane surfaces;
- panel-fitter and cursor registers;
- BCS request, submission, and failure counts;
- confirmed frame, copy-engine, copy-time, flip-time, and latch-failure counts.

`intel_valleyview_probe --p0-benchmark` measures cached shadow upload and
read/modify/write throughput, writes two identical 128x128 bottom-left grids,
waits for two confirmed presentation frames, and verifies that the active
scanout matches `DSPASURFLIVE`.

`intel_valleyview_probe --p0-test` reruns the private BCS fill/copy self-test
under the same serialization used by presentation.

## Render boundary

The kernel driver exposes a separately versioned `kGetRenderDeviceInfo` query
for hardware-renderer discovery. It reports the ValleyView generation, GGTT
aperture, no-LLC cache model, P0's reserved aperture range, and the distinction
between engines proven by kernel diagnostics and engines available for
userspace submission.

The current render status remains deliberately `B_NOT_SUPPORTED`.
`IsRenderReady()` additionally requires tiled buffers, render contexts,
isolated RCS submission, completion fences, command isolation, reset recovery
as a service, and drawable presentation. A hardware OpenGL add-on therefore
cannot mistake kernel-owned diagnostics for a complete Crocus transport. The
image continues to use Mesa's Software Pipe OpenGL add-on.

`intel_valleyview_probe --render-info` prints this boundary without attempting
submission or changing GPU state.

### Linear render-memory substrate

When native P0 is healthy, the discovery query advertises per-open buffer
objects, driver-owned CPU mappings, GGTT addresses, and cache-domain
transitions.
Each client is limited to 64 buffers, 16 MiB per buffer, and 64 MiB total.
Buffers use fragmented pages locked below 4 GiB rather than requiring
physically contiguous allocations.

The GGTT allocator recognizes free space by the exact firmware scratch PTE
saved during P0 takeover. It excludes GGTT address zero and the complete P0
range, saves every displaced scratch entry, installs snooped writable PTEs, and
verifies both installation and exact restoration. Buffer pages are quarantined
rather than freed if restoration cannot be proven.

Current buffers are linear, write-back CPU mappings with snooped GGTT entries.
Mappings are non-transferable kernel areas revoked when their handle or client
closes. Teardown detaches every inherited clone from the backing cache before
releasing BO accounting, so forked mappings cannot retain pinned pages. Their
tracked domains are CPU and the kernel-owned BCS. BCS submission remains
synchronous under `bcsLock`; userspace cannot provide commands.

`intel_valleyview_probe --render-memory-test` creates two client-owned buffers,
clones both into the process, writes coordinate-dependent source and destination
patterns, confirms that userspace cannot delete the driver-owned mappings,
cycles their domains, performs a kernel-generated one-page BCS copy, verifies
both mappings, restores their GGTT entries, and closes the handles. The BCS
data path is hardware-validated on Winky while native P0 presentation remains
active and fault-free.

### RCS transport diagnostic

The driver has a kernel-generated RCS diagnostic, not a userspace submission
API. It allocates a hidden four-page transport buffer for the RCS ring,
hardware-status page, marker batch, and result page, plus a separate hidden
19-page GGTT object. The second object runs the MIT-licensed Ivy Bridge
clear-residual EU kernel and Gen7 media-pipeline sequence derived from the local
Linux i915 `gen7_renderclear.c` reference.

The ring first runs the marker batch and records the RCS timestamp, then chains
to the secure kernel shader batch and retires through a Gen7 `PIPE_CONTROL`
completion write. The shader batch initializes a B8G8R8A8 render-cache surface
with a sentinel and emits 36 `MEDIA_OBJECT` dispatches, the ValleyView
`max_threads` value, to write zero blocks. A following guard page must remain
untouched.

Verification reports zero, sentinel, and unexpected dword counts; the changed
range; checksums before and after execution; the first unexpected value; guard
mismatches; every shader GGTT PTE before binding, while bound, and after
restoration; and cache modes 0 and 1 before and after execution.

The diagnostic requires an idle legacy RCS with PPGTT and active CCID context
selection disabled. It programs and posts the hardware-status page before the
required RCS TLB sync-flush. It snapshots global GT, BCS, CCID, context,
page-directory, cache-mode, and decoded RCS fault state. Because the shader
changes pipeline, state-base, media, and cache state, every attempt ends with a
bounded RCS reset. The driver then restores cache modes, HWS, ring, context,
page-directory state, and both hidden GGTT allocations, flushes the TLB again,
and verifies the complete restoration. Unsafe restoration quarantines both
buffers and fails all further render work closed.

A successful diagnostic adds RCS to `provenEngines`; it does not add RCS to
`submissionEngines` or expose user commands, contexts, isolated RCS submission,
completion fences, reset recovery as a service, tiling, or presentation. When
hardware passes, it proves only this kernel-generated EU/render-cache workload.
The workload has not yet been hardware-validated.

`intel_valleyview_probe --render-transport-test` is the combined hardware gate.
It runs render discovery, captures P0 state, exercises mapping ownership and the
BCS memory copy, runs the RCS diagnostic, captures P0 again, and prints one
summary. Failure output retains all RCS stages, command addresses, marker
values, timestamps, shader verification counts and checksums, guard state,
every shader PTE transition, cache modes, ring/context registers, restoration
status, and P0 counters needed for offline diagnosis.

## Current support

The driver currently supports only the hardware-validated Winky configuration:

- ValleyView PCI `8086:0f31`;
- eDP on DP_C and pipe A;
- one native 1366x768 RGB32 mode;
- linear scanout;
- BCS full-frame presentation with CPU fallback;
- 64x64 monochrome and ARGB hardware cursors;
- PWM brightness and soft DPMS.

The panel fitter remains in its firmware AUTO configuration. With a native
1366x768 pipe source, the observed geometry and diagnostic grids are square.
Presentation copies the full frame continuously; it does not consume app_server
damage notifications. Suspend/resume and other ValleyView boards, ports, pipes,
formats, tiling modes, and display timings are not implemented or validated.
