---
name: intel-valleyview-crocus-p2
description: Build Jidō Renga's asynchronous P2 Crocus engine on ValleyView while preserving Safe GL isolation and P0. Use for queued submission, timeline fences, persistent RCS contexts, scheduling, residency, direct GPU presentation, performance, or conformance work.
---

# Building the ValleyView P2 render engine

Read [`AGENTS.md`](../../AGENTS.md), the
[`jido-renga-overlay-build`](../jido-renga-overlay-build/SKILL.md),
[`intel-valleyview-p0`](../intel-valleyview-p0/SKILL.md), and
[`intel-valleyview-crocus`](../intel-valleyview-crocus/SKILL.md) skills first.
The P2 contract is
[`docs/design/intel_valleyview_p2.md`](../../docs/design/intel_valleyview_p2.md).

## Phase boundary

P1 is Safe GL and remains a supported recovery mode. P2 adds a separately
advertised queued engine. Do not make existing synchronous submission
success-shaped when P2 is unavailable, and do not advertise P2 because queue
types or worker code merely exist.

EGL, GLES, WebGL, and WebKit are P3. Do not add them to P2 patches.

## Queue ownership

- Copy and parse the batch before enqueue succeeds.
- Hold kernel references on every shadow, BO, context, and completion record
  until ordered retirement or explicit failure.
- Assign nonzero 64-bit fences monotonically and never wrap them.
- Retire fences in submission order per client.
- Use explicit read/write/execute object flags. Only the batch BO is
  executable, and it is never writable by the GPU.
- Bound per-client and device queue depth. Apply backpressure; do not allocate
  unbounded kernel work.
- Schedule clients round-robin. A client that continuously submits must not
  starve another ready client.

The device worker owns RCS programming. Ioctl, `select()`, close, and
presentation paths must not program the ring.

## Haiku synchronization

Use a kernel worker plus semaphore for queued work. Use per-client wait
semaphores for blocking fence waits and `select_sync_pool` for event-loop
integration:

- `B_SELECT_READ`: one or more completion records are ready;
- `B_SELECT_WRITE`: queue capacity is available;
- `B_SELECT_ERROR`: a fence failed, the context was lost, or the client was
  disconnected.

Never spin in userspace. Interrupt handlers acknowledge hardware, capture the
minimal retirement state, and release a semaphore with
`B_DO_NOT_RESCHEDULE`; parsing, reset, cleanup, and notification run outside
interrupt context.

## Persistent RCS

Keep per-context ring, HWS, trusted shadow storage, hardware context, and PPGTT
resources alive. Reuse the current context for adjacent jobs. On a client
switch, execute the proven directory-load, TLB-invalidate, arbitration, and
`MI_SET_CONTEXT` sequence.

Do not reset healthy submissions. On timeout or fault:

1. stop accepting work for the affected context;
2. snapshot the engine and trusted completion memory;
3. reset and restore using the Safe GL machinery;
4. complete proven fences only;
5. fail or cancel every uncertain fence in order;
6. quarantine memory whose retirement cannot be established; and
7. preserve P0 and other clients whenever hardware state permits.

## Memory and presentation

BO close is deferred while queued or active references exist. CPU access waits
only for conflicting writer/reader fences. Stable VA does not imply permanent
residency.

Direct presentation is fence-aware BCS work, not CPU readback. Preserve
`device.lock -> presentLock -> bcsLock`; never nest `renderLock` and
`presentLock`. Retain the `BBitmap` path as a fallback until app_server has a
proven shareable surface contract.

## Validation order

1. Host-test queue, timeline, access, fairness, cancellation, and wrap policy.
2. Prove queued Safe execution with two clients and deterministic waits.
3. Prove direct BCS presentation independently with the reset-safe executor and
   a `BDirectWindow` that supplies real clipping rectangles.
4. Implement resident-context ownership before enabling failure-only reset.
5. Expand residency and resource formats.
6. Run broad conformance only after the engine metrics and recovery gates pass.

Every hardware capture must include Safe GL control results and P0 state.

For the first lab image, run only:

```sh
intel_valleyview_gl_suite --p2-lab
```

After P2A passes, it runs one Safe control, one queued control, and the complete
direct compatibility matrix. Do not repeat the proven queue burst or full
Safe/queued matrices in presentation captures. `direct` is reset-safe
BCS-to-P0-shadow presentation; it is not the final app_server-sharing design.
Do not revive the reset-omission experiment: P2B requires a kernel-owned
resident hardware context and explicit switching.

The Winky hardware baseline is 18/18 direct cases, 18 successful direct
presents, zero direct-present failures, and no mapped presentation fallback.
Preserve that baseline while adding the nonblocking latest-frame queue.

The version 12 Winky baseline is an eight-request single-BO burst at queue depth
six with seven same-stream drops, 1–16 us ioctl latency, ordered successful
retirement, and no fallback. Preserve it alongside the 18/18 direct matrix.

For the version 14 B+D+E candidate, `--p2-lab` must run the persistent
two-client switch/fault probe and residency probe before one Safe, one queued,
and 24 direct semantic cases. Require no healthy resets, a nonzero switch/reuse
count, one fault reset, no restore failure, balanced lazy GGTT binds/evictions,
and stable data for 112 MiB across 81 BOs. Treat physical backing as locked;
do not call this physical-page eviction.

Persistent ownership includes the render/media forcewake and GT-wake reference.
BCS borrows it under `bcsLock`; release it only after reset-to-baseline on
fault, Safe handoff, teardown, or final driver shutdown.

The version 14 Winky baseline is 32 healthy jobs, 31 switches, zero healthy
resets, one recovered fault reset, zero restore failures, 112 MiB across 81
stable-VA BOs, balanced lazy GGTT residency, and 24/24 semantic direct cases.
Do not describe this as physical-page eviction or broad CTS conformance.

For the version 15 release candidate, user BO areas are pageable and each
client may wire at most 96 MiB for GPU access. Evict only idle CPU-domain BOs
with no queued reference or GGTT binding. Replace and flush their complete
stable PPGTT range with scratch before unwiring; on reload, wire first, rebuild
the physical list, and restore that same range. Any partial unwire or uncertain
PTE transition quarantines the context. Internal PPGTT, ring, HWS, and hardware
context resources remain permanently wired and outside this budget.

The version 15 `--p2-lab` gate must require physical eviction, at least two
reloads, an RCS write through a reloaded stable PPGTT VA, a 96-MiB residency
ceiling, and baseline physical residency after close. It then runs all 32
isolated direct cases, including pixel-verified blending, scissoring, mipmapped
and cube textures, readback, element-buffer drawing, four-sample FBO resolve,
and the explicit OpenGL 3.1/GLSL 1.40 limit gate. Treat this as a candidate
until one complete Winky capture passes; do not substitute allocation-only
feature checks or describe it as Piglit/CTS certification.
