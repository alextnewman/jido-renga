<!--
SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
SPDX-License-Identifier: MIT
SPDX-FileContributor: Generated with GitHub Copilot
-->

# ValleyView P2 asynchronous render engine

P2 turns the hardware-proven Safe GL path into a throughput-oriented Haiku
render engine. Safe GL remains the recovery baseline: it keeps strict parsing,
immutable batch shadows, private PPGTTs, trusted completion, bounded waits,
reset recovery, quarantine, Software Pipe delegation, and every P0 coexistence
gate. P2 changes when work blocks, how long engine state remains resident, and
how completed images reach the display.

EGL, GLES, WebGL, and WebKit integration are P3 work. P2 first makes the
desktop OpenGL engine asynchronous, efficient, observable, and suitable for
broad conformance testing.

## Execution model

Each open render client owns a monotonically increasing 64-bit timeline and a
bounded FIFO. An enqueue ioctl copies and parses the batch into a kernel-owned
shadow, validates explicit per-object read/write/execute access, takes BO
references, assigns a fence, and returns without waiting for the GPU.

A device render worker serializes the single RCS engine and schedules ready
clients round-robin. The worker is driven by a semaphore rather than userspace
polling. Retirement advances each client timeline in order, releases BO
hazards, appends a completion record, wakes fence waiters, and notifies
`B_SELECT_READ`. Faults additionally notify `B_SELECT_ERROR`.

The first queued implementation may use the existing bounded completion-marker
wait inside the kernel worker. That proves ABI lifetime and scheduling without
blocking applications. Interrupt-driven RCS retirement replaces the worker
wait before the queued capability becomes the default.

## Persistent engine state

A P2 render context owns its PPGTT directory, 64 KiB hardware context, status
page, ring resources, and trusted wrapper storage for its complete lifetime.
Healthy submissions do not reset RCS or rebuild these objects.

The scheduler may retain the current client context across adjacent jobs.
Switching clients reloads `PP_DIR`, invalidates the TLB, and executes the proven
inhibited `MI_SET_CONTEXT` sequence. Reset is reserved for timeout, fault,
failed retirement, explicit Safe GL mode, or shutdown. A reset fails all work
whose completion cannot be proven; it never returns uncertain BOs to CPU
ownership.

Safe and queued execution are separate advertised capabilities. Failure to
initialize P2 leaves Safe GL usable. Runtime P2 failure may disable the queued
capability and drain or fail its work, but it must not manufacture successful
fences or silently redirect submitted hardware work.

## Buffer hazards and residency

P2 submissions describe every referenced BO as read, write, or executable.
The batch BO is the sole executable object and is read-only. The kernel tracks
queued and active readers, the last writer fence, and deferred close state.
CPU mapping and domain transitions wait only for conflicting fences rather
than globally waiting for RCS.

Stable PPGTT virtual addresses are independent from backing residency. P2 may
grow beyond the Safe GL 64-buffer/64-MiB bootstrap limits only after it has
bounded accounting, eviction, and rollback. Tiled color, MSAA, and larger
textures require matching cache, mapping, and presentation contracts; generic
advertisement must follow end-to-end proof.

## Presentation

`SwapBuffers()` retires a render resource with a fence and returns. A
high-priority presentation worker waits for that fence and consumes the newest
eligible frame without blocking the render worker.

The first accelerated presentation target is direct mode. Crocus supplies the
linear render BO and clipping rectangles; the kernel BCS path copies those
rectangles into the P0-owned framebuffer under the established presentation
lock order. The existing `BBitmap` path remains the non-direct and recovery
fallback. Buffer sharing with app_server is a later Haiku integration problem,
not a reason to weaken P0 ownership.

Presentation queues are latest-frame oriented: obsolete unpresented frames may
be dropped after their render fences retire, but submitted GPU work and fence
results are never dropped.

## Phase gates

1. **P2A — queued Safe executor:** enqueue, timelines, waits, completion
   dequeue, `select()` notification, fair multi-client scheduling, immutable
   shadows, and deferred BO close. The worker may call the Safe GL executor.
2. **P2B — persistent RCS:** persistent workspaces and hardware contexts,
   multiple in-flight batches, context-switch accounting, interrupt-backed
   retirement, and reset only on failure.
3. **P2C — asynchronous presentation:** fence-aware frame queues and BCS direct
   presentation with no CPU readback in direct mode.
4. **P2D — scalable memory:** residency, eviction, truthful limits, tiled color,
   MSAA, and larger resource budgets.
5. **P2E — conformance:** broad Piglit coverage, advertised desktop profile,
   compatibility regression, stress, and multi-client fault recovery.

P3 begins with EGL and a shareable offscreen/window-surface contract.

## Hardware status

P2A is hardware-proven on Winky: two clients complete bounded round-robin
bursts, `select()` readiness is deterministic, injected failure retires in
order, a fresh client recovers, and the queued mode completes the 18-case
compatibility suite.

P2C is hardware-proven. A `BDirectWindow` run completed
all 18 cases with 18 successful BCS copies from Crocus render BOs to P0's
framebuffer shadow, zero direct-present failures, and no CPU frontbuffer
fallback.

Render protocol version 12 makes render and
presentation jobs share one ordered per-client timeline; `SwapBuffers()`
enqueues a copied clipping snapshot and returns its fence without waiting for
RCS or BCS. BO mapping, reuse, close, and teardown honor that presentation
fence. Older queued frames are dropped only within the same presentation
stream, while their timeline records still retire in order. Queue-pressure
fallback drains the timeline before CPU presentation so an older BCS copy
cannot overwrite the fallback frame.

The Winky collapse gate queued eight same-stream presents with 1–16 us ioctl
latency, reached queue depth six, retired seven obsolete presents with dropped
completion flags, and executed the newest BCS copy. Fences 4 through 11 retired
successfully in order. The full direct matrix then completed 18/18 with 25
queued presents, 18 executed copies, seven drops, zero presentation failures,
and zero mapped fallbacks. P0 retains its existing shadow-to-scanout worker;
that ownership boundary is intentional.

P2B is hardware-proven on Winky. Two clients retired 32 healthy jobs with 31
context switches and zero healthy resets. An injected failure caused one
reset-to-baseline, then the client recovered; restore failures remained zero.
Real Crocus contexts retain ring, HWS, PP_DIR, cache, saved extended state, and
the shared power reference through rendering and BCS presentation.

The integrated P2D bootstrap is hardware-proven at 112 MiB across 81 BOs,
including one 32-MiB allocation. Data and stable PPGTT addresses survive lazy
GGTT residency; bind/evict counters balance and resident bytes return to zero.
Physical backing remains locked, so physical-page eviction is still open.

The integrated P2E corpus completes 24/24 process-isolated cases. Added coverage
includes 32-bit indices, buffer subdata, complete FBOs, nonzero occlusion
queries, instanced draws, and 3D textures. This is a strong feature gate, not a
replacement for broad Piglit/CTS profile conformance.

## Integrated B+D+E candidate

Render protocol version 14 adds the integrated candidate:

- `safe`: the proven synchronous P1 transaction;
- `queued`: immutable enqueue, timeline fence, fair kernel worker, and Safe GL
  execution on the worker;
- `persistent`: lifetime-owned ring, HWS, trusted shadow, hardware context, and
  PPGTT workspace with reset only on fault or ownership release; and
- `direct`: persistent command execution plus P2C presentation.

Initialized client switches save and restore extended context state. Adjacent
jobs on the same owner omit redundant `MI_SET_CONTEXT`; first use remains
restore-inhibited. Mixed Safe work explicitly releases persistent ownership
back to the captured baseline. Persistent ownership also carries a render/media
forcewake and GT-wake reference so ring, HWS, context, and PP_DIR state survive
idle intervals; BCS borrows that reference under the shared engine lock.

User render BOs are PPGTT-only by default. Their stable render VA survives
lazy GGTT bind/evict cycles used by BCS presentation and diagnostics. The
candidate raises the truthful bootstrap limits to 64 MiB per BO, 256 MiB and
256 BOs per client, while retaining the 64-object submission bound. Physical
pages remain locked; swapping physical backing is later P2D work and is not
claimed by this milestone.

Run the complete lab matrix with:

```sh
intel_valleyview_gl_suite --p2-lab
```

The command first runs a persistent two-client switch/fault/recovery probe and
a residency probe with 80 one-MiB BOs plus one 32-MiB BO. It then runs one Safe
control, one queued control, and all 24 direct cases from a `BDirectWindow`.
The first direct case retains the proven eight-request collapse burst.

Require zero healthy resets, nonzero context switches and reuses, exactly one
fault reset, zero restore failures, stable data beyond the former 64-MiB/64-BO
limits, balanced GGTT bind/evict counters, 24 semantic GL passes, P2C frame
collapse, and clean teardown.

## Required evidence

P2 diagnostics retain Safe GL's exact failure snapshots and add queue depth,
high-water marks, enqueue-to-start and start-to-retire latency, context
switches, resets, dropped presentation frames, CPU/GPU cache transitions, and
copy paths.

P2 is not complete until:

- two clients make bounded forward progress without cross-client access;
- fence waits, timeouts, cancellation, close, and `select()` notification are
  deterministic;
- the 18-case GL suite completes without a healthy-path reset;
- injected timeout and parser failures preserve P0 and Safe GL recovery;
- GLTeapot reaches the display refresh rate in direct mode without CPU
  frontbuffer copies; and
- all reported resource and API limits match allocations that actually
  succeed.
