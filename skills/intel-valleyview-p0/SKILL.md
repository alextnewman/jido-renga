---
name: intel-valleyview-p0
description: Maintain and extend Jidō Renga's hardware-validated ValleyView P0 graphics path on Winky. Use when changing intel_valleyview driver memory, GGTT mappings, BCS commands, page-flip presentation, cursor, DPMS, accelerant hooks, diagnostics, teardown, or graphics hardware tests.
---

# Maintaining the ValleyView P0 graphics path

The current `intel_valleyview` architecture is hardware-validated on Winky. It
provides a native 1366x768 desktop with smooth window, text, and cursor motion.
Treat its shadow/copy/confirmed-flip path as the known-good baseline.

Read [`AGENTS.md`](../../AGENTS.md) and the
[`jido-renga-overlay-build`](../jido-renga-overlay-build/SKILL.md) skill first.
Never modify the captive `haiku/` or `buildtools/` submodules.

## Established architecture

app_server must receive the cloneable write-back shadow framebuffer. It must
never receive either write-combined scanout.

Each presentation cycle:

1. identifies the scanout not named by `DSPASURFLIVE`;
2. copies the complete shadow into that inactive scanout;
3. waits for BCS retirement through `MI_FLUSH_DW` and the HWS marker, or drains
   CPU write-combining stores for the CPU fallback;
4. writes the completed surface to `DSPASURF`;
5. retains the target as pending until `DSPASURFLIVE` confirms it;
6. only then reuses the previous scanout.

Do not replace this with direct app_server writes into the live WC surface.
Those row-by-row damage copies visibly expose partial window and text updates.

## Memory and GGTT contract

P0 maps three 1032-page framebuffer ranges followed by eight private pages:

```text
0x0f3e0000  cached render shadow, writable + CPU-cache snooped
0x0f7e8000  WC scanout 0, writable + non-snooped
0x0fbf0000  WC scanout 1, writable + non-snooped
0x0fff8000  64x64 cursor (four pages)
0x0fffc000  BCS ring
0x0fffd000  hardware status
0x0fffe000  test source
0x0ffff000  test destination
```

The shadow snoop bit is required because ValleyView has no LLC and BCS reads
data written through CPU caches. Scanouts and private pages remain non-snooped.
Keep the cursor/ring/status/test offsets fixed unless hardware evidence proves a
replacement layout.

Before publication, `VerifyBcsPresent()` must exercise the actual cached-shadow
to WC-scanout path with a coordinate-dependent pattern and distinct padding.
A uniform fill is insufficient because it cannot detect pitch or row-selection
errors.

## Flip ownership

A programmed surface is not free merely because `DSPASURF` was written again.
Ownership changes only when `DSPASURFLIVE` confirms the latch.

- Never copy into `pendingScanout`.
- A latch warning may update telemetry, but it must not free or reuse the target.
- Blanking must disable the plane and confirm that the live surface no longer
  names any P0 framebuffer before clearing pending ownership.
- Unblanking must start from a confirmed detached plane, populate one scanout,
  latch it, and only then restart continuous presentation.

## BCS serialization and ordering

Every operation that touches forcewake, the BCS ring, its HWS page, or diagnostic
GGTT PTEs takes `bcsLock`. This includes diagnostics and self-tests.

The lock hierarchy is:

```text
device.lock -> presentLock -> bcsLock
```

The worker takes `presentLock -> bcsLock` and never `device.lock`. Do not add a
`bcsLock -> presentLock` or `bcsLock -> device.lock` path.

`SubmitBcsPresent()` is synchronous. A scanout may be armed only after the
`MI_FLUSH_DW` completion marker is visible. A CPU `memory_write_barrier()` orders
the CPU fallback but is not a substitute for GPU retirement.

## Accelerant contract

The accelerant exposes the native mode, cached framebuffer clone, ARGB and
monochrome hardware cursor, brightness, and DPMS hooks. It deliberately does not
advertise legacy engine/fill/blit hooks: Haiku's local app_server does not
consume them, and the logical framebuffer is the shadow rather than a live
scanout.

Haiku prefers `B_SET_CURSOR_BITMAP`. Preserve the 64x64 ARGB path and Intel mode
`0x27`; falling back to software cursor rendering regresses the proven smooth
cursor behavior.

## Teardown and quarantine

Stop and join the present worker before quiescing BCS or restoring display
state. Teardown must prove all of these before freeing any P0 page:

1. the present worker has exited;
2. the candidate cursor base is no longer live;
3. BCS ring start, HWS, head, tail, and valid state are detached;
4. the firmware plane is restored and observed live;
5. original GGTT PTEs are restored exactly.

Any failure quarantines the shadow, both scanouts, and private allocation
together. Never free a subset while the GPU or display may retain an address.

## Validation

Run the host suite and all three target builds:

```sh
make -C tests -j4
tools/weave generated.x86_64
cd generated.x86_64
../tools/jr-jam -q intel_valleyview intel_valleyview.accelerant \
  intel_valleyview_probe
```

Build the composed image for hardware work:

```sh
../tools/jr-jam -q @nightly-anyboot
```

Batch the hardware evidence:

```sh
intel_valleyview_probe --p0-status
intel_valleyview_probe --p0-benchmark
intel_valleyview_probe --p0-test
```

A healthy BCS-present desktop has:

- native, BCS, and present status `0`;
- `kP0PresentReady` and `kP0PresentBcs`;
- increasing confirmed frame and BCS-present-copy counts;
- no BCS or present failures;
- an active scanout that matches `DSPASURFLIVE`;
- two identical, square bottom-left benchmark grids;
- increasing ARGB cursor bitmap and movement counts.

After changing DPMS or ownership code, also blank and restore the display while
the desktop is active. Build success is not hardware proof; record only observed
capabilities in tracked documentation.
