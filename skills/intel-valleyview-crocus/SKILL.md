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
linear buffer objects, driver-owned CPU mappings, dynamic GGTT bindings, coherent
CPU/BCS domain transitions, a kernel-generated BCS copy test, and a
kernel-generated RCS batch-chain diagnostic. It still returns
`B_NOT_SUPPORTED` as its overall render status because tiled buffers, contexts,
isolated userspace RCS submission, fences, reset recovery, and presentation are
absent. Neither kernel diagnostic is a userspace submission engine.

Keep the hardware renderer fail-closed. It may instantiate only when
`IsRenderReady()` succeeds. Until then, Haiku's Software Pipe add-on remains
the functional renderer.

## Required transport

Do not expose raw RCS batches as an intermediate shortcut. Hardware rendering
requires all capabilities named by `kRenderRequiredCapabilities`:

1. per-open buffer-object ownership and deterministic cleanup;
2. driver-owned CPU mappings with an explicit cache policy and bounded lifetime;
3. GPU virtual addresses that cannot reach another client or P0's allocation;
4. explicit CPU/GPU cache-domain transitions for the no-LLC memory model;
5. tiled buffers with safe fence-register ownership;
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
never be nested. An RCS diagnostic allocates and binds under `renderLock`, then
releases it before taking `presentLock -> bcsLock` for stable display-state
sampling and engine execution.

The no-LLC cache model is part of the render contract. CPU mappings, PTE snoop
bits, flushes, and GPU retirement must agree before a buffer changes owner.
The current linear buffers are write-back and snooped. Do not advertise
non-snooped or tiled mappings until their cache maintenance and fence-register
ownership are implemented and tested.

User mappings must be `B_KERNEL_AREA` clones owned by the driver. Do not expose
cloneable backing-area IDs. Before releasing BO accounting, detach every
inherited clone from the backing cache with `vm_change_clones_to_null_areas()`.

## Haiku integration

The hardware renderer is an OpenGL add-on installed at Haiku's canonical
`add-ons/opengl` path and implementing `BGLRenderer`. The overlay path and
`JIDO_RENGA_TOP` are build-time details and must not appear in its runtime ABI,
paths, or diagnostics.

Keep Mesa adaptation separate from kernel policy. Mesa may translate Crocus
buffer-manager requests into the render ABI, but it must not map GPU registers,
write GGTT PTEs, or submit rings directly.

## Validation gates

Run the host suite and the existing P0 targets after every render ABI change:

```sh
make -C tests -j4
tools/weave generated.x86_64
cd generated.x86_64
../tools/jr-jam -q intel_valleyview intel_valleyview.accelerant \
  intel_valleyview_probe
```

Before enabling a renderer in the image, validate discovery, client teardown,
cross-client isolation, invalid batches, timeout/reset behavior, and P0
presentation under concurrent render load. Build success is not hardware proof.

Conserve device flashes by accumulating cohesive functionality behind
diagnostics. The current combined hardware gate is:

```sh
intel_valleyview_probe --render-transport-test
```

Do not request another flash for an intermediate register or command check.
The combined output must retain enough pre-state, active state, failure state,
cleanup state, and P0 counters to diagnose a failed run offline.
