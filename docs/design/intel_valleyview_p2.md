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

## First lab image

The first P2 lab image implements four runtime modes in one driver and renderer:

- `safe`: the proven synchronous P1 transaction;
- `queued`: immutable enqueue, timeline fence, fair kernel worker, and Safe GL
  execution on the worker;
- `direct`: queued Safe execution plus clipped BCS copies from the retired
  Crocus color BO into P0's framebuffer shadow; and
- `persistent`: queued execution with healthy-path RCS reset disabled while
  retaining complete ring, cache, PPGTT, wake, and P0 restoration.

`persistent` is an experiment toward resident contexts, not the final P2B
architecture. `direct` removes GPU readback, the temporary `BBitmap`, and the
CPU direct-buffer copy, but P0 still performs its existing framebuffer-to-
scanout copy and vblank latch.

Run the complete lab matrix with:

```sh
intel_valleyview_gl_suite --p2-lab
```

The command first initializes RCS and runs a raw two-client queue burst with
ordered fault and recovery. It then executes all 18 GL cases in Safe, queued,
and direct modes, proves an injected queue failure and fresh-client recovery,
and runs one explicit no-reset control draw last. The destructive experiment is
last so a fail-closed engine quarantine cannot hide independent queue or
presentation evidence. Queue captures report depth, high-water marks,
submitted/completed/failed/cancelled jobs, no-reset jobs, queue/execution
latency, and submission cleanup status.

## Required evidence

P2 diagnostics retain Safe GL's exact failure snapshots and add queue depth,
high-water marks, enqueue-to-start and start-to-retire latency, context
switches, resets, dropped presentation frames, CPU/GPU cache transitions, and
copy paths.

The queued capability is not complete until:

- two clients make bounded forward progress without cross-client access;
- fence waits, timeouts, cancellation, close, and `select()` notification are
  deterministic;
- the 18-case GL suite completes without a healthy-path reset;
- injected timeout and parser failures preserve P0 and Safe GL recovery;
- GLTeapot reaches the display refresh rate in direct mode without CPU
  frontbuffer copies; and
- all reported resource and API limits match allocations that actually
  succeed.
