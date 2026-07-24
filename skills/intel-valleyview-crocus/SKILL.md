---
name: intel-valleyview-crocus
description: Build Jidō Renga's fail-closed Crocus hardware-rendering path on ValleyView without regressing the proven P0 display stack. Use when changing the render ABI, buffer objects, GPU address spaces, RCS contexts or submission, synchronization, reset recovery, Mesa packaging, the Haiku BGLRenderer add-on, or 3D presentation.
---

# Extending ValleyView toward Crocus

Read [`AGENTS.md`](../../AGENTS.md), the
[`jido-renga-overlay-build`](../jido-renga-overlay-build/SKILL.md) skill, and
the [`intel-valleyview-p0`](../intel-valleyview-p0/SKILL.md) skill first. Never
modify the captive `haiku/` or `buildtools/` submodules.

## Current boundary

`kGetRenderDeviceInfo` is the userspace discovery boundary. Its ABI is
versioned separately from the display protocol. The current driver implements
linear buffer objects, driver-owned CPU mappings, dynamic GGTT bindings,
coherent CPU/BCS domain transitions, and one scratch-backed 2 GiB Gen7 PPGTT
context per open client. It also has a kernel-generated BCS copy test, an RCS
marker and EU/render-cache diagnostic, and parsed synchronous RCS submission
through a private GGTT shadow and the client's PPGTT. Trusted synchronous
completion is the initial fence contract. The Mesa 22.0.5 Haiku Crocus backend
uses stable ABI addresses, keeps color and staging resources linear, permits
only the hardware-required Y/W layouts for depth and stencil, and presents
retired frontbuffers through HGL's clipped `BBitmap` path. Its add-on delegates
to the packaged Software Pipe renderer if hardware setup fails. The combined
probe reconstructs the exact ValleyView linear triangle corpus and verifies its
offscreen color, geometry, fence, and allocation guard; this remains a hardware
candidate until the combined Winky gate passes.

Keep the hardware renderer fail-closed. It may instantiate only when
`IsRenderReady()` succeeds. Until then, Haiku's Software Pipe add-on remains
the functional renderer.

## Required transport

Do not execute mutable, unparsed, or globally addressed user batches. Hardware
rendering requires all capabilities named by `kRenderRequiredCapabilities`:

1. per-open buffer-object ownership and deterministic cleanup;
2. driver-owned CPU mappings with an explicit cache policy and bounded lifetime;
3. GPU virtual addresses that cannot reach another client or P0's allocation;
4. explicit CPU/GPU cache-domain transitions for the no-LLC memory model;
5. a declared linear or tiled resource policy with matching cache ownership;
6. render contexts and isolated RCS submission;
7. completion fences with bounded waits;
8. command isolation appropriate to the selected address-space model;
9. hang detection, reset recovery, and failed-work signaling;
10. a drawable presentation path that respects Haiku clipping and P0 ownership.

Capability bits describe working end-to-end services, not code that merely
exists or hardware that is believed to be present.

## P0 coexistence

P0 owns the top `kP0AllocationBytes` of the GGTT aperture. Render allocations
must never overlap that range, the live firmware mapping, or any GGTT entry
whose ownership has not been established. Preserve the lock order documented
by the P0 skill. Render-memory operations use
`device.lock -> renderLock -> bcsLock`; presentation uses
`device.lock -> presentLock -> bcsLock`. `renderLock` and `presentLock` must
never be nested. An RCS diagnostic allocates and binds both hidden GGTT objects
under `renderLock`, then releases it before taking
`presentLock -> bcsLock` for stable display-state sampling and engine execution.

The no-LLC cache model is part of the render contract. CPU mappings, PTE snoop
bits, flushes, and GPU retirement must agree before a buffer changes owner.
Current buffers are write-back and snooped. CPU-interpreted color, staging,
batch, and state resources remain linear. Depth and stencil may use their
hardware-required tiled layouts only as GPU resources; do not expose them as
logically detiled CPU mappings. Do not advertise non-snooped mappings or tiled
presentation until their cache maintenance and fence-register ownership are
implemented and tested.

Each open client may create one software PPGTT context before creating BOs.
Rejected or duplicate context creation must not alter live client resources.
Its 2 MiB fragmented page-table allocation is bound as 512 Gen6 PDE entries in
a 64 KiB-aligned GGTT run. All 524,288 PTEs initially name a separate private
scratch page; page zero remains reserved. Client BO mappings replace only
bitmap-owned PTEs and are restored to scratch before BO backing or context
resources are released. PPGTT table writes require bounded `clflush` followed
by `mfence`. Failed PTE or GGTT restoration quarantines the referenced memory
and fails render work closed.
Once quarantined, PPGTT PTEs, directory GGTT entries, and backing pages must
remain untouched during handle, context, and client teardown.

Synchronous submission copies at most 64 KiB into a kernel-owned GGTT shadow
before parsing. On SNB/IVB/VLV, do not set `MI_BATCH_NON_SECURE_I965` for that
dispatch: with PPGTT enabled the bit selects PPGTT and defeats the immutable
shadow. Use the privileged bare batch start only after strict parsing. Keep the
shadow and trusted completion outside the client PPGTT, reset RCS after every
started submission, and verify restoration before returning BOs to CPU.
Gen7 submission must program the 64-byte PPGTT cache controls in
`GAC_ECO_BITS` and `GAM_ECOCHK` before enabling the ring, then restore and
verify both registers after reset.
Load `PP_DIR_DCLV` and `PP_DIR_BASE` again through the trusted RCS ring, with a
GGTT posting read and `INSTPM` invalidation. Then bracket an inhibited switch to
a 64 KiB-aligned kernel context with `MI_ARB_ON_OFF`, followed by full pre/post
PIPE_CONTROL barriers. Gen7 caches PDEs in the active hardware context, and Bay
Trail cannot safely rely on low-latency MMIO-only full-PPGTT switching. Retain
the context address, GPU-observed directory, and barrier markers in diagnostics.
Do not advertise RCS submission until the immutable-shadow
`MI_BATCH_BUFFER_END` bootstrap has itself completed and restored successfully;
before that proof, reject every other user batch.

User mappings must be `B_KERNEL_AREA` clones owned by the driver. Do not expose
cloneable backing-area IDs. Before releasing BO accounting, detach every
inherited clone from the backing cache with `vm_change_clones_to_null_areas()`.

## Haiku integration

The hardware renderer is a `BGLRenderer` add-on installed in Haiku's canonical
system non-packaged `add-ons/opengl` override path. This deterministically
owns renderer selection when mastered as a loose image file outside packagefs.
Keep the packaged `mesa_swpipe` renderer as the next fallback, and retain
explicit delegation to that packaged add-on from Crocus. The overlay path and
`JIDO_RENGA_TOP` are build-time details and must not appear in its runtime ABI,
paths, or diagnostics.

Keep Mesa adaptation separate from kernel policy. Mesa may translate Crocus
buffer-manager requests into the render ABI, but it must not map GPU registers,
write GGTT PTEs, or submit rings directly.
Build the tracked Mesa patch with `tools/build-crocus`; never modify captive
Haiku/buildtools sources or commit `generated.crocus/`.

## Trusted diagnostics

Use the local `research/linux` tree first for i915 reference behavior. Keep
derived code MIT-licensed and record the specific upstream source in current
implementation documentation.

Kernel shader diagnostics remain distinct from submission. User LRI commands
are accepted only from the exact host-tested Crocus corpus and only after the
kernel has copied and parsed the complete batch; all other register/value pairs
fail closed.

Diagnostic pipeline, state-base, media, and cache changes require a bounded RCS
reset after every attempt. Then restore and verify cache modes, HWS, ring,
context, page-directory state, and every GGTT PTE for both hidden allocations.
Unsafe restoration quarantines both buffers and fails closed.

Verify shader output by exact content cardinality: 2,048 zero dwords, 14,336
sentinel dwords, no third values, and an untouched guard. Do not require byte
positions; the first Winky run showed that ValleyView media-block placement
differs from the naive coordinate model.

## Validation gates

Run the host suite and the existing P0 targets after every render ABI change:

```sh
make -C tests -j4
tools/weave generated.x86_64
cd generated.x86_64
../tools/jr-jam -q intel_valleyview intel_valleyview.accelerant \
  intel_valleyview_probe intel_valleyview_crocus_demo
cd ..
tools/build-crocus generated.x86_64
```

Before enabling a renderer in the image, validate discovery, client teardown,
cross-client isolation, invalid batches, timeout/reset behavior, and P0
presentation under concurrent render load. Build success is not hardware proof.

Conserve device flashes by accumulating cohesive functionality behind
diagnostics. Keep a one-flash combined gate; do not add incremental hardware
tests. The final gate is:

```sh
intel_valleyview_probe --render-transport-test
intel_valleyview_crocus_demo
```

Do not request another flash for an intermediate register or command check.
The combined output must retain pre-state, bound state, failure state, zero,
sentinel, and unexpected counts, changed range, checksums, first unexpected
value, guard mismatches, every shader PTE transition, cache modes,
reset/restoration state, raster color/coverage/interpolation, and P0 counters
needed to diagnose a failed run offline. The `gfx_test8` Winky run
hardware-validated the RCS marker and timestamp, EU shader/render-cache writes,
`PIPE_CONTROL` completion, bounded reset, cache/ring/HWS/context and all 19
shader-PTE restorations, BCS operation, and P0 coexistence. That historical run does not prove the newer 3D raster candidate or packaged
Crocus screen.
