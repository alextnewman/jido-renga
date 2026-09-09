# Intel ValleyView graphics driver

`intel_valleyview` is the native Haiku graphics driver for Winky's ValleyView
GPU, PCI `8086:0f31`. It adopts the firmware-lit eDP panel on DP_C and pipe A,
publishes the native 1366x768 RGB32 mode, accelerates frame presentation and
cursor movement, and provides the kernel interface used by the Crocus OpenGL
renderer.

The runtime components install at canonical Haiku paths:

```text
/boot/system/add-ons/kernel/drivers/dev/graphics/intel_valleyview
/boot/system/add-ons/accelerants/intel_valleyview.accelerant
/boot/system/non-packaged/add-ons/opengl/Crocus
/boot/system/bin/intel_valleyview_probe
```

## Firmware-gated display ownership

The driver does not implement a general modesetter. It accepts the Winky
firmware state only when all required registers agree:

- DPLL and pipe A are enabled for the native 1366x768 timing;
- eDP is active on DP_C;
- the firmware source is 1024x768 RGB32 with a 4096-byte stride;
- the panel fitter is enabled in firmware AUTO mode;
- panel power and PWM state are coherent;
- the firmware framebuffer has a complete GGTT mapping; and
- no firmware cursor is active.

Every live register is checked immediately before takeover. A mismatch leaves
the display untouched. Successful takeover saves the firmware plane, fitter,
cursor, CxSR, PWM, and GGTT state for teardown.

## P0 presentation

The accelerant gives app_server a cloneable write-back shadow framebuffer, not
a live write-combined scanout. A kernel worker presents complete frames:

1. choose the scanout not named by `DSPASURFLIVE`;
2. copy the complete shadow into that inactive scanout;
3. wait for BCS completion, or drain CPU stores for the fallback copy;
4. program `DSPASURF`;
5. retain the target until `DSPASURFLIVE` confirms the latch; and
6. reuse only the other scanout.

This hides app_server's row-by-row damage copies from the panel. A delayed latch
keeps the target pending; hardware-visible memory is never overwritten or
released speculatively.

### GGTT layout

P0 reserves three 1032-page framebuffer ranges and eight private pages at the
top of the 256 MiB GMADR aperture:

| Range | GGTT offset | CPU mapping | GGTT policy |
|---|---:|---|---|
| app_server shadow | `0x0f3e0000` | write-back, cloneable | writable, snooped |
| scanout 0 | `0x0f7e8000` | write-combining, private | writable, non-snooped |
| scanout 1 | `0x0fbf0000` | write-combining, private | writable, non-snooped |
| 64x64 cursor | `0x0fff8000` | private | writable, non-snooped |
| BCS ring | `0x0fffc000` | private | writable, non-snooped |
| BCS HWS | `0x0fffd000` | private | writable, non-snooped |
| BCS test source | `0x0fffe000` | private | writable, non-snooped |
| BCS test destination | `0x0ffff000` | private | writable, non-snooped |

ValleyView has no LLC. The shadow's snooped PTEs make app_server's cached writes
visible to BCS. Scanouts remain write-combined and non-snooped because they are
display destinations.

Before publishing the graphics node, the driver verifies a coordinate-dependent
BCS copy from the cached shadow to the inactive scanout, including visible
pixels and untouched row padding. A safe test failure selects full-frame CPU
copy. An uncertain ring cleanup faults the graphics path rather than treating
fallback as safe.

## Cursor, brightness, and DPMS

The accelerant implements `B_SET_CURSOR_BITMAP`. RGBA cursors are converted to
Intel's 64x64 ARGB format and use cursor mode `0x27`; cursor movement is
independent from frame presentation.

Brightness control preserves the firmware PWM period. Soft DPMS serializes
with presentation and blanks the backlight, cursor, and primary plane without
power-cycling the panel link. Unblanking starts from a confirmed detached plane,
populates one scanout, confirms its latch, and only then restarts presentation.
While blanked, the presentation worker blocks on a semaphore rather than
periodically polling disabled state.

## Render discovery and contexts

`kGetRenderDeviceInfo` is a separately versioned discovery ABI. It reports the
ValleyView generation, no-LLC cache model, GGTT aperture, P0 reservation,
supported queue modes, memory limits, and whether the complete render service
is ready.

Each open render client owns:

- one scratch-backed 2 GiB Gen7 PPGTT;
- a private scratch page mapped by every free PTE;
- a 64 KiB hardware context;
- a persistent ring and HWS page;
- trusted batch and result storage;
- a bounded queue and ordered 64-bit timeline; and
- independently accounted BO allocation and residency.

Page zero is reserved. PPGTT tables occupy a fragmented 2 MiB DMA32 allocation
whose physical pages are bound as 512 Gen6 PDE entries in a 64 KiB-aligned GGTT
run. Cached page-table writes are completed with bounded `clflush` and
`mfence`.

## Buffer objects and residency

User BOs receive stable page-aligned PPGTT addresses and are not permanently
bound into GGTT. Direct presentation and BCS diagnostics create a temporary
GGTT binding for the bounded operation and restore its entries immediately.

| Resource | Limit |
|---|---:|
| BO size | 64 MiB |
| BOs per client | 256 |
| Allocated bytes per client | 256 MiB |
| Objects per submission | 64 |
| Wired user-BO backing per client | 96 MiB |

The backing area is pageable. Eviction replaces an idle BO's complete PPGTT
range with scratch PTEs before unwiring its pages. Reload wires the same data,
rebuilds the physical list, and restores the original GPU VA. Queued, active,
GGTT-bound, internal, or quarantined buffers are not eviction candidates.

CPU mappings are driver-owned `B_KERNEL_AREA` clones. The mapping remains valid
while backing is evicted because it names the pageable area rather than a
physical allocation. Before close, the driver detaches inherited clones with
`vm_change_clones_to_null_areas()`.

Single-sample window color, staging, batch, and state resources are linear.
Gen7 multisample color and depth use tiled layouts; separate stencil uses W
tiling. Tiled GPU resources are not exposed as logically linear mappings or
external modifiers.

## Submission isolation

An enqueue names one context, one batch BO, a dword-aligned range of at most
64 KiB, and up to 64 unique client BO handles with explicit read, write, and
execute access. The batch is the only executable BO and is read-only.

The kernel copies the batch into private GGTT memory before parsing it. The
parser rejects unknown commands, nested batch starts, BLT commands, unapproved
LRI pairs, global-GTT or MMIO `PIPE_CONTROL` writes, malformed lengths, and
nonzero data after `MI_BATCH_BUFFER_END`.

The trusted ring loads the client's page directory, posts the load, invalidates
the TLB, switches hardware context with the required arbitration and
extended-state flags, performs cache barriers, dispatches the immutable shadow,
and writes a completion marker outside client PPGTT.

Safe mode resets RCS and verifies the captured baseline after every batch.
Persistent modes retain context and power ownership between healthy jobs.
Timeout, fault, Safe handoff, teardown, and shutdown reset the engine and fail
any completion that cannot be established.

## Queue and presentation

Each client has a bounded FIFO and a monotonically increasing nonzero fence
timeline. The kernel worker schedules ready clients round-robin. Completion
records support blocking waits, dequeue, and `select()` readiness.

Direct `SwapBuffers()` appends a presentation job to the same timeline and
returns without waiting. Crocus supplies the retired linear color BO and a
copied clipping snapshot. BCS copies the requested rectangles into P0's shadow;
P0 continues to own shadow-to-scanout presentation.

Older unpresented frames may be dropped only within the same window stream.
Their fences still retire in order. The CPU presentation fallback drains the
timeline first so pending BCS work cannot overwrite the fallback frame.

## Crocus renderer

`tools/build-crocus` builds the maintained Mesa fork as the
`BGLRenderer` add-on. Haiku loads it from the system non-packaged override
directory. If discovery, context creation, or screen setup fails, the add-on
delegates to packaged Software Pipe.

The BGL frontend exposes an OpenGL 3.1 compatibility context with GLSL 1.40.
EGL, GLES, WebGL, external buffer sharing, performance monitors, and
externally supplied tiled allocations are not part of this interface.

Compatibility uniforms use binding-table pull loads. Display-list save BOs are
bounded to 4 MiB. Haiku-specific transfer allocation and Mesa-internal locking
avoid unsupported allocator and pthread assumptions.

## Locking and teardown

The lock hierarchy is:

```text
device.lock -> renderLock -> bcsLock
device.lock -> presentLock -> bcsLock
```

The present worker takes `presentLock -> bcsLock` without `device.lock`.
`renderLock` and `presentLock` never nest.

Shutdown joins the render and present workers, releases persistent RCS
ownership, detaches the candidate cursor and BCS ring, restores and observes the
firmware plane, and then restores the original GGTT PTEs. Any object that may
remain referenced is quarantined rather than freed.

## Diagnostics

`intel_valleyview_probe` provides:

- `--p0-status` for display, cursor, engine, and presentation state;
- `--p0-benchmark` for shadow throughput and confirmed scanout;
- `--p0-test` for the private BCS fill/copy self-test;
- `--render-info` for the render capability boundary;
- `--render-transport-test` for RCS diagnostics, isolated submission, Crocus
  raster, mapping ownership, and P0 coexistence;
- `--render-persistent-test` for two-client switching and fault recovery; and
- `--render-residency-test` for stable-VA eviction and reload.

`intel_valleyview_gl_suite --p2-lab` combines persistent scheduling, fault
recovery, residency, direct-presentation collapse, and the process-isolated
OpenGL semantic suite.

Set `VALLEYVIEW_GPU_DEBUG=1` only for application investigation. It enables
bounded batch, queue, fault, and presentation telemetry.

## Supported configuration and limitations

The driver supports Winky's:

- ValleyView PCI `8086:0f31`;
- eDP panel on DP_C and pipe A;
- native 1366x768 RGB32 mode;
- BCS presentation with CPU fallback;
- monochrome and ARGB hardware cursors;
- PWM brightness and soft DPMS; and
- Crocus OpenGL 3.1 compatibility rendering.

The panel fitter stays in firmware AUTO mode. While the display is active,
presentation copies complete frames rather than consuming app_server damage
notifications and confirms scanout latching with bounded register polling. The
worker sleeps indefinitely while soft-blanked. Suspend/resume, other ValleyView
devices, ports, pipes, display timings, EGL, and browser surface integration are
not implemented.
