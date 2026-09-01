<!--
SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
SPDX-License-Identifier: MIT
SPDX-FileContributor: Generated with GitHub Copilot
-->

# ValleyView render engine architecture

The ValleyView render stack provides hardware OpenGL through a bounded,
fail-closed Haiku interface. It combines private GPU address spaces, immutable
batch shadows, ordered per-client timelines, persistent RCS contexts, pageable
buffer backing, and asynchronous BCS presentation.

Safe GL remains available as the recovery path. EGL, GLES, WebGL, and WebKit
surface integration are outside this interface.

## Execution modes

The render queue has four usable policies:

- **Safe** executes synchronously and resets RCS to the captured baseline after
  each batch.
- **Queued** assigns a fence and executes the same reset-safe transaction on
  the kernel worker.
- **Persistent** assigns fences and retains healthy RCS context state between
  jobs.
- **Direct** uses persistent RCS contexts and adds asynchronous BCS
  presentation.

Safe and queued capabilities are advertised independently. Initialization or
runtime failure in an asynchronous mode never manufactures a successful fence
or silently redirects submitted hardware work.

## Queue and timeline

Each open client owns a monotonically increasing nonzero 64-bit timeline and a
bounded FIFO. Enqueue performs all admission work before returning:

1. validate the context and unique object list;
2. copy and parse the batch into kernel-owned memory;
3. validate explicit read, write, and execute access;
4. retain every referenced BO;
5. assign the next fence; and
6. append the immutable job.

The device worker serializes the single RCS engine and schedules ready clients
round-robin. Retirement advances each client timeline in order, releases BO
references, appends a completion record, wakes fence waiters, and notifies
`select()`:

- `B_SELECT_READ` means a completion is available;
- `B_SELECT_WRITE` means queue capacity is available;
- `B_SELECT_ERROR` means a fence failed or the context was lost.

A failed active fence is terminal. Work whose execution cannot be established
is failed or cancelled in timeline order.

## Persistent RCS ownership

Each render context owns its PPGTT directory, scratch page, 64 KiB hardware
context, ring, HWS page, trusted batch shadow, and result storage for its
lifetime.

Adjacent jobs from the same client reuse the active context. A client switch
loads `PP_DIR`, invalidates the TLB, saves and restores extended context state,
and executes the required `MI_SET_CONTEXT` sequence. Persistent ownership also
holds the render/media forcewake and GT-wake reference; BCS borrows that claim
under `bcsLock`.

Timeout, fault, Safe-mode handoff, context teardown, and driver shutdown reset
RCS to the captured global baseline. Completion is reported only when the
trusted marker and required restoration state are known.

## Submission isolation

Every client has a scratch-backed 2 GiB Gen7 PPGTT. Page zero is reserved and
all unallocated entries map a private scratch page. User BOs receive stable,
page-aligned PPGTT virtual addresses.

The sole executable BO is read-only. The kernel copies at most 64 KiB of batch
data into a trusted GGTT workspace and rejects unknown commands, nested batch
starts, BLT commands, unapproved LRI pairs, unsafe `PIPE_CONTROL` writes,
malformed lengths, and nonzero trailing data.

Client commands cannot address the trusted shadow, completion marker, another
client, or P0 memory. Failed PTE, GGTT, ring, or context restoration quarantines
memory that may still be referenced.

## Buffer residency

The userspace limits are:

| Resource | Limit |
|---|---:|
| BO size | 64 MiB |
| BOs per client | 256 |
| Allocated bytes per client | 256 MiB |
| Objects per submission | 64 |
| Wired user-BO backing per client | 96 MiB |

User BO areas are pageable. Their PPGTT virtual addresses do not change when
physical backing is evicted.

Eviction selects the least-recently-used BO that is CPU-owned, idle, healthy,
not queued, and not GGTT-bound. The driver verifies its complete PPGTT range,
replaces it with scratch PTEs, flushes the page-table cache lines, and then
unwires the area. Reload wires the pageable data, rebuilds the physical-page
list, restores the same PPGTT range, and relies on the trusted submission
wrapper's TLB invalidation before RCS access.

An uncertain PTE transition or partial unwire quarantines the context. Internal
PPGTT, ring, HWS, context, and presentation resources remain wired and are not
charged to the user-BO residency budget.

Window color and staging resources are linear. Private multisample color
targets use the tiled layout required by Gen7 and their MCS metadata remains
internal to Crocus. No externally shareable tiled modifier is advertised.

## Presentation

`SwapBuffers()` places presentation on the same ordered client timeline as
rendering and returns without waiting for RCS or BCS.

For a `BDirectWindow`, Crocus supplies a retired linear color BO and copied
clipping rectangles. The kernel binds that BO into GGTT for the bounded BCS
copy into P0's cached framebuffer shadow, then immediately restores the GGTT
entries. P0 retains ownership of shadow-to-scanout copying and confirmed
vblank latching.

Presentation is latest-frame oriented. An older unpresented frame may be
dropped only when a newer frame from the same window stream exists. Its fence
still retires in order. Queue-pressure fallback drains the timeline before a
CPU presentation so an older BCS copy cannot overwrite the fallback image.

## OpenGL boundary

Haiku BGL exposes the Crocus renderer as an OpenGL 3.1 compatibility context
with GLSL 1.40. The supported limit gate requires:

- 16K 2D and cube textures;
- 2K 3D textures;
- 16 vertex attributes;
- eight draw buffers; and
- at least four samples.

The process-isolated semantic suite covers explicit GLSL/VBO drawing,
fixed-function arrays and immediate mode, display lists, depth, lighting,
textures, FBOs, queries, instancing, buffer updates, 32-bit and element-buffer
indices, blending, scissoring, mipmaps, cube maps, readback, and four-sample
resolve.

This boundary is not Piglit or CTS certification. EGL contexts, shareable
window surfaces, GLES, WebGL, and browser composition require a separate
Haiku-facing surface contract.

## Validation

Run the complete hardware gate from the installed image:

```sh
intel_valleyview_gl_suite --p2-lab
```

The command runs:

1. two-client persistent scheduling, injected fault, and recovery;
2. 112 MiB of BO pressure with stable-VA eviction and reload;
3. an RCS marker through reloaded PPGTT addresses;
4. Safe and queued controls;
5. the direct-presentation collapse check; and
6. all 32 isolated GL semantic cases.

A passing run has no launch or case failures, no healthy-path reset, one
recovered injected fault, no restore failure, physical residency at or below
96 MiB, physical residency returned to its baseline after close, balanced GGTT
bind/evict accounting, and successful completion of every semantic case.

Diagnostics retain queue depth and latency, context switches and reuses, reset
and restoration state, physical and GGTT residency, dropped presentations,
parser results, and trusted completion state.
