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

The render status is `B_NOT_SUPPORTED` until an open client has proven the RCS
diagnostic, created its PPGTT, and completed the immutable-shadow submission
bootstrap. It then becomes `B_OK`; `IsRenderReady()` requires the complete
linear synchronous transport, trusted completion, command isolation, and reset
recovery. Crocus keeps color and staging resources linear while using the
hardware-required Y/W layouts for Gen7 depth and stencil resources.

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

Buffers are write-back CPU mappings with snooped GGTT entries. Color, staging,
batch, and state resources are linear. Depth and stencil BOs expose only their
raw hardware layout to the CPU; Crocus does not advertise a logically detiled
mapping. Mappings are non-transferable kernel areas revoked when their handle
or client closes. Teardown detaches every inherited clone from the backing cache
before releasing BO accounting, so forked mappings cannot retain pinned pages.
Their tracked domains are CPU, the kernel-owned BCS, and synchronous RCS
ownership. BCS submission remains kernel-generated under `bcsLock`.

### Per-client PPGTT substrate

Each open client may explicitly create one software render context before
creating any BOs. Creation allocates a 2 GiB Gen7 two-level PPGTT and reserves
virtual page zero. Buffers created while the context is healthy receive a stable
page-aligned PPGTT address before creation succeeds. The existing `gpuOffset`
remains the GGTT/BCS diagnostic address; `renderAddress` is zero without a
context and carries the isolated PPGTT address with one. Duplicate context
creation and context creation after BO allocation return `B_BUSY` without
changing the client's live resources.

The PPGTT uses 512 complete 1024-entry page tables in a fragmented 2 MiB DMA32
allocation. Their physical pages are installed as Gen6 PDE encodings in a
2 MiB GGTT run aligned to 64 KiB; that GGTT offset is the diagnostic `PP_DIR`
base. A separate DMA32 scratch page backs all 524,288 PTEs initially, using
writable snooped BYT PTEs. Unmapped writes therefore remain in private scratch
memory. Although the encoding helpers preserve the Gen6 40-bit format, current
BO, page-table, and scratch allocations remain locked below 4 GiB.

Page-table writes are made through the cached kernel mapping and completed with
bounded x86 `clflush` operations followed by `mfence`. BO close restores its
PTEs to scratch before releasing its GGTT binding or backing. Context destroy
restores every BO mapping, verifies and restores the directory's GGTT entries,
then releases the directory, scratch, and bitmap. Any restoration that cannot
be proven quarantines the potentially referenced memory and disables render
work. Quarantined PPGTT resources are never rewritten or unbound during client
teardown. The scratch restoration model follows Linux i915
`gt/gen6_ppgtt.c`; cache-line completion follows its `gt/intel_gtt.c`
page-table fill path.

### Synchronous isolated RCS submission

After the kernel RCS diagnostic has proven the engine, a healthy PPGTT context
advertises executable render contexts, command isolation, and reset recovery.
Before the new address-space path is proven, the ioctl accepts only a
one-dword `MI_BATCH_BUFFER_END` bootstrap. Successful completion and full
restoration of that immutable-shadow transaction enable synchronous RCS
submission, trusted synchronous completion fences, and RCS in
`submissionEngines`. A normal submission names its
context, one batch BO, a dword-aligned batch range of at most 64 KiB, and a
fixed inline list of up to 64 unique client BO handles. The batch must be in the
list. Every listed BO must be CPU-owned, healthy, and mapped in that client's
PPGTT.

The kernel copies the range into a private 19-page GGTT workspace before
parsing it, so later CPU writes cannot change the accepted command stream.
The strict Gen7 parser rejects unknown commands, nested batches, BLT commands,
unapproved LRI pairs, global-GTT or MMIO `PIPE_CONTROL` writes, malformed
lengths, and nonzero data after `MI_BATCH_BUFFER_END`. State and resource
pointers still resolve only through the client's scratch-backed PPGTT.

The accepted shadow is dispatched with a privileged bare
`MI_BATCH_BUFFER_START`. On SNB/IVB/VLV, the nominal non-secure bit also selects
PPGTT once PPGTT is enabled, so setting it would fetch from a mutable client
address rather than the immutable GGTT shadow. The parser is therefore the
privilege boundary, following Linux i915's Gen6/7 shadow-parser model. The
workspace is not mapped into the client PPGTT, and the trusted ring writes its
timestamp and completion marker through GGTT. Client commands cannot forge
retirement or modify the shadow.

Submission freezes presentation, programs the client's 2 GiB `PP_DIR`, enables
the Gen7 64-byte PPGTT cache controls in `GAC_ECO_BITS` and `GAM_ECOCHK`,
programs the client's 2 GiB `PP_DIR`, enables legacy RCS PPGTT, flushes the TLB,
then repeats the page-directory load inside the trusted RCS ring. That sequence
matches Linux's Gen7 legacy-ring path: LRI loads of `PP_DIR_DCLV` and
`PP_DIR_BASE`, a GGTT posting read, and `INSTPM` TLB invalidation. The ring then
disables arbitration, switches to a 64 KiB-aligned kernel-owned hardware context
with restore inhibited, reenables arbitration, and issues two complete
PIPE_CONTROL invalidate/flush barriers before dispatch. The posting-read values,
context address, and all barrier markers are returned in submission diagnostics.
The context transition is required because Gen7 caches the PDEs in the active
hardware context; it also avoids Bay Trail's documented full-PPGTT timing
instability when execution follows page-directory changes too quickly.

The driver waits synchronously for the trusted completion marker. Every started
submission resets RCS, restores and verifies the original ring, HWS, mode,
`PP_DIR`, global PPGTT controls, L3 registers, `INSTPM`, wake state, display
signature, and BCS state, then restores BO ownership to CPU. A timeout, fault,
or failed restoration is returned in the submission record with before,
active, fault, and after snapshots. Any memory that might remain referenced is
quarantined without further PTE or GGTT mutation.

### Crocus raster candidate

The combined probe carries the exact Mesa 22.0.5 Crocus render corpus generated
for ValleyView PCI `0x0f31` from Gallium's triangle test. The target is forced
linear: 300x300 B8G8R8A8, 1200-byte stride, and a 384 KiB allocation. The corpus
contains the real clear and triangle VS/PS kernels, state, workaround data,
vertex data, 2,036-byte command stream, seven BO roles, fourteen command
relocations, and two surface-state relocations. Host tests reconstruct every BO
at synthetic PPGTT addresses and parse the complete relocated batch.

After the immutable-shadow bootstrap succeeds,
`intel_valleyview_probe --render-transport-test` creates the seven real client
BOs, patches only the recorded Crocus relocations to their assigned PPGTT
addresses, submits all three `3DPRIMITIVE` packets, and checks both Crocus's
PPGTT fence write and trusted kernel completion. The render target begins as a
sentinel. Verification requires all 90,000 visible pixels to become opaque,
roughly 36,000 pixels to carry interpolated triangle color, red/green/blue
vertex regions, triangle edge positions and widths at six rows, representative
interpolation samples, and an untouched 8,304-dword allocation guard. The
checksum and every count remain in probe output for offline diagnosis.

This remains a host-validated hardware candidate until the combined Winky run,
but it uses the same transport as the installed Crocus screen.

### Haiku Crocus renderer

The derivative image installs `non-packaged/add-ons/opengl/Crocus`, a Mesa
22.0.5 Gallium renderer built reproducibly by `tools/build-crocus`. Haiku checks
the system non-packaged add-on directory before package add-ons, so Crocus
deterministically owns renderer selection. If hardware screen creation fails,
that add-on delegates to the packaged Software Pipe renderer. Crocus is mastered as a
real loose file in `/boot/system/non-packaged/add-ons/opengl`, not as content
inside packagefs; the packaged `mesa_swpipe` renderer remains installed as the
next fallback. The maintained Mesa fork adds a
Haiku Crocus buffer manager that creates and maps driver-owned BOs, uses their
stable PPGTT addresses directly, shares the one per-open context between
Crocus's synchronous render batches, submits the fixed inline validation list,
and treats the returned trusted completion as its fence. DRM sharing, userptr
aliasing, performance monitors, and externally supplied tiled allocation fail
closed or remain disabled. The Haiku path uses binding-table pull constants
because BYT VS push fetches do not retire in the isolated context, caps
display-list save BOs at 4 MiB, and uses direct transfer records plus atomic
Mesa-internal locks for texture upload and sampler validation.

The HGL frontend creates the Crocus screen directly from
`/dev/misc/intel_valleyview_probe`. Color and staging resources remain linear;
Gen7 depth uses Y tiling and separate stencil uses W tiling as required by the
hardware. `flush_frontbuffer` maps only the retired linear color resource,
copies it into a Haiku `BBitmap`, and hands it to the existing `BGLRenderer`
clipping/direct-mode presentation path. The screen therefore reaches the
P0-backed desktop without exposing overlay paths or GPU mappings at runtime. If
discovery, bootstrap, or screen creation fails, it loads Haiku's packaged
Software Pipe add-on, preserving the proven llvmpipe fallback.

`intel_valleyview_crocus_demo` opens a 600x500 `BGLView`, prints `GL_RENDERER`
and `GL_VERSION`, and draws a visible interpolated RGB triangle. It is the
visible half of the final hardware gate after
`intel_valleyview_probe --render-transport-test` passes.

Set `VALLEYVIEW_GPU_DEBUG=1` in an application's environment to enable Crocus
submission and presentation telemetry. The mode reports up to 256 parsed
batches, the first timeout for each `VALLEYVIEW_GPU_CASE` with its RCS
instruction/fault snapshot and surrounding immutable batch words, and
frontbuffer samples and checksums. It is an investigation interface, not an
acceptance test; GLTeapot covers one fixed-function compatibility workload and
does not represent the complete OpenGL surface.

`intel_valleyview_gl_suite` is a focused Haiku port of permissively licensed
Piglit GL 1.0/1.1 batch, depth-function, and array-start cases, combined with
project-owned explicit GLSL/VBO controls. One invocation tags and isolates
clear, client-array, fixed-function VBO, immediate-mode, display-list,
quad-strip scaling, depth, lighting, texture, line, and post-stall recovery
stages in separate child processes so one failure cannot exhaust the following
cases. It sets `VALLEYVIEW_GPU_DEBUG` and Mesa's batch decoder itself, and
labels every submission through `VALLEYVIEW_GPU_CASE`, producing one capture
that includes command/state decoding and can locate a hang without a flash per
hypothesis. Winky hardware passes all 18 stages without a parser rejection,
timeout, GL error, or core dump. The uploaded-texture stage presents the
expected green sample, and the final explicit stage proves recovery after the
legacy cases.

GLTeapot also renders through Crocus. Its default **Limit FPS to refresh rate**
setting calls `WaitForRetrace()` after every frame; Winky currently sustains
about 44 fps and can dip during manipulation. GLTeapot emits roughly 162
immediate-mode primitives per frame, while the current renderer submits
synchronously, resets/restores RCS after each batch, invalidates the CPU mapping,
copies into a temporary `BBitmap`, and then copies into the direct framebuffer.
Because GLTeapot uses `BDirectWindow`, app_server composition is not the primary
limit. The result is a compatibility proof, not a representative Crocus
throughput benchmark.

The asynchronous successor is specified in
[`docs/design/intel_valleyview_p2.md`](../design/intel_valleyview_p2.md). P2
keeps this Safe GL path as a separately selectable recovery mode while moving
healthy work to queued timelines, persistent contexts, and fence-aware direct
presentation. EGL remains outside that phase.

Render protocol version 11 carries the P2 lab queue: immutable enqueue, explicit
BO access, 64-bit timeline waits, completion dequeue, `select()` readiness,
bounded per-client/device depth, a round-robin kernel worker, and complete
submission-cleanup status in each completion record. Failed retired fences are
terminal: Mesa poisons affected BOs instead of polling them as perpetually busy.
The initial worker calls the proven Safe GL executor. The no-reset experiment
restores all engine state without a healthy reset, while direct mode
independently proves clipped BCS presentation into P0's framebuffer shadow
using the reset-safe executor. These capabilities remain runtime-selected and
are not advertised as final P2 services before the lab gate passes.

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

Output verification requires exactly 2,048 zero dwords and 14,336 sentinel
dwords, no third values, and an untouched guard. It deliberately does not assume
byte positions: the first Winky run showed that ValleyView media-block placement
differs from the naive coordinate model. Diagnostics also retain the changed
range, checksums, first unexpected value, every shader GGTT PTE transition, and
cache modes 0 and 1 before and after execution.

The diagnostic requires an idle legacy RCS with PPGTT and active CCID context
selection disabled. It programs and posts the hardware-status page before the
required RCS TLB sync-flush. It snapshots global GT, BCS, CCID, context,
page-directory, cache-mode, and decoded RCS fault state. Because the shader
changes pipeline, state-base, media, and cache state, every attempt ends with a
bounded RCS reset. The driver then restores cache modes, HWS, ring, context,
page-directory state, and both hidden GGTT allocations, flushes the TLB again,
and verifies the complete restoration. Unsafe restoration quarantines both
buffers and fails all further render work closed.

A successful diagnostic adds RCS to `provenEngines` and permits the isolated
one-dword submission bootstrap; it does not itself add RCS to
`submissionEngines`. When hardware passes, it proves only this kernel-generated
EU/render-cache workload, not the separate PPGTT dispatch, completion fences,
tiling, or presentation.
The `gfx_test8` Winky run hardware-validated the RCS marker and timestamp, EU
shader/render-cache writes, `PIPE_CONTROL` completion, bounded reset,
cache/ring/HWS/context and all 19 shader-PTE restorations, BCS operation, and P0
coexistence. This is not evidence of 3D rasterization or Crocus readiness.

`intel_valleyview_probe --render-transport-test` is the combined hardware gate.
It runs render discovery and the kernel RCS diagnostic, creates a PPGTT context,
verifies the executable capability boundary, rejects a duplicate context,
submits a parsed one-dword `MI_BATCH_BUFFER_END` through the isolated transport,
submits and verifies the Crocus triangle corpus, validates PPGTT addresses while
exercising mapping ownership and the BCS memory copy, closes the buffers,
destroys the context, captures P0 again, and prints one summary. Failure output
retains the RCS diagnostic plus submission parser, object, workspace,
completion, raster geometry/color/guard, L3, global GT, ring, `PP_DIR`, reset,
restoration, and P0 state needed for offline diagnosis.

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
