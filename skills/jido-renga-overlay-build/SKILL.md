---
name: jido-renga-overlay-build
description: Build, weave, and extend the Jidō Renga out-of-tree Haiku driver overlay. Covers the Jam-native graft into a captive Haiku checkout, the derivative-revision seam, the host build environment, and the step-by-step recipe for adding a new overlay driver. Use when working in the jido-renga repository on kernel drivers, Jamfiles, the weave/haiku-revision/jr-jam tooling, the UserBuildConfig graft, or when building the captive Haiku from the overlay.
---

# Building and extending the Jidō Renga overlay

Jidō Renga grafts out-of-tree Haiku kernel drivers onto a **captive** Haiku
checkout at build time. This skill is the operational playbook. For the
project's non-negotiable invariants (never modify the captive; build-time only;
no `jido-renga` in `#include`s; mirror Haiku's device classes) read the root
`AGENTS.md` first — everything below assumes them.

## Mental model

- `haiku/` and `buildtools/` are pinned git **submodules** (the captive). They
  are pristine upstream and must stay that way.
- The overlay's sources live under `overlay/`. They are compiled by Haiku's own
  Jam cross-toolchain, from a source path Jam would otherwise never visit.
- The single integration seam is `<build-dir>/UserBuildConfig`, a file Haiku's
  `Jamrules` auto-includes from the build output directory. `tools/weave`
  renders and installs it. Nothing else in the captive scope is ever written.

## The host build environment

The build runs anywhere Haiku's out-of-tree build runs (Linux/WSL is the
common case). Two host-portability points:

- **Python ≥ 3.10** is required by `configure`. If the host default is older or
  named oddly, pass `HOST_PYTHON=/path/to/python3` in the environment.
- If your **host** C++ toolchain's headers/libraries live outside default
  search paths (pkgsrc, Homebrew, Nix, or any non-standard prefix), export
  `CPLUS_INCLUDE_PATH` and `LIBRARY_PATH` to point at that prefix's
  `include`/`lib`. These affect only the *host* tools Jam builds, not the
  cross-compiled kernel objects. Example for a prefix at `/opt/pkg`:

  ```sh
  CPLUS_INCLUDE_PATH=/opt/pkg/include LIBRARY_PATH=/opt/pkg/lib \
    ../tools/jr-jam -q -j2 iosf_mbi
  ```

Do not hardcode any host OS assumptions into committed files.

## How the graft works (Jam-native)

Haiku's build system is Jam. The graft hooks exactly one seam and works in
three moves:

1. **Set a foreign top variable.** `UserBuildConfig` is read very early (before
   Jam's per-architecture/platform scaffolding exists), so it does one safe
   thing: `JIDO_RENGA_TOP = <abs path to this repo> ;`. Jam's `SubDir` /
   `SubInclude` dereference their first token as a variable name, so a *foreign*
   top roots sources **outside** `HAIKU_TOP`.
2. **Defer the walk.** A plain `SubInclude` this early silently no-ops
   (`KernelAddon` bails when `PLATFORM` is empty). So we use
   `DeferredSubInclude JIDO_RENGA_TOP overlay ;`, which runs at the **end of the
   root Jamfile**, where `PLATFORM=haiku` is valid and `KernelAddon` works.
3. **Walk the overlay.** `overlay/Jamfile` `SubInclude`s each module — shared
   modules first (leaves may depend on what they publish), then the leaf
   drivers. Each module's `Jamfile` uses ordinary `KernelAddon`; objects land
   in the normal Haiku output tree and link with the kernel toolchain.

`config/UserBuildConfig.in` is the checked-in template for move (1). `weave`
substitutes this repo's absolute path and merges the result into
`<build-dir>/UserBuildConfig` inside a managed marker block, so it coexists with
any hand-kept settings and is safe to re-run.

## Build procedure

```sh
# 0. Fetch the captive Haiku + buildtools (pinned submodules).
git submodule update --init

# 1. Configure a throwaway Haiku build output dir, sibling to the submodules.
mkdir generated.x86_64 && cd generated.x86_64
../haiku/configure --cross-tools-source ../buildtools --build-cross-tools x86_64
cd ..

# 2. Weave the graft into that build dir (idempotent, non-destructive).
tools/weave generated.x86_64

# 3. Build from inside the build dir, via the jr-jam wrapper.
cd generated.x86_64
../tools/jr-jam -q iosf_mbi i2c_guarded sdhci_embedded \
  cros_ec_keyboard i2c_atmel_mxt

# 4. (optional) Fold the selected BSP's add-ons into a bootable anyboot image
#    instead of building them loose. The overlay composes each into haiku.hpkg
#    and applies that BSP's declared upstream omissions without editing captive
#    sources or package recipes.
../tools/jr-jam -q @nightly-anyboot           # -> haiku-nightly-anyboot.iso
```

`generated*/` is throwaway and never committed. Any surrounding Haiku checkout
outside the submodules is only an extraction source and is never referenced.
The default BSP is `winky`; set `JIDO_RENGA_BSP = none` in `UserBuildConfig`
before the overlay walk to build the add-ons without a BSP image policy.

### Proven Winky composition

The hardware-validated Winky image applies three surgical policies:

1. Omit stock `add-ons/kernel/busses/mmc/sdhci`, package
   `sdhci_embedded` in its canonical bus directory, and create its boot link.
2. Package `iosf_mbi` and create its boot link so the SDHCI dependency is
   available before boot-media discovery.
3. Omit stock `add-ons/kernel/bus_managers/i2c`, but package the internal
   `i2c_guarded` target under the canonical filename
   `add-ons/kernel/bus_managers/i2c`. The filename matters because
   `get_module("bus_managers/i2c/...")` maps to it; the distinct Jam target name
   avoids colliding with the captive target.

The third policy is a defensive normalization of optional HID/CID attributes,
not a claim that the stock manager can never work. It preserves all stock
consumers, including `i2c_elan`.

## The derivative-revision seam

Haiku stamps a **revision** (normally an `hrev` number) into `libroot`, surfaced
by `uname`/AboutSystem, derived from `hrev` git tags. The GitHub mirror the
captive tracks **does not carry those tags**, so a bare build aborts with
*"Haiku revision could not be determined."*

Rather than fake an upstream `hrev`, Jidō Renga records an honest **derivative**
identity, `<upstream>+jido-renga-<version>`:

- `<upstream>` — the pinned captive's label: the recorded `hrev` of the release
  you pin (`UPSTREAM_REVISION` in `config/revision.conf`), or, unpinned, the
  captive `HEAD` short commit as `g<sha>`.
- `<version>` — this overlay's version (`JIDO_RENGA_VERSION`).

`tools/haiku-revision` composes it (e.g. `hrev57937+jido-renga-0.1.0` pinned, or
`gb5fd959d60+jido-renga-0.1.0` unpinned), kept under Haiku's 128-char limit.

**Why a wrapper is needed.** jam imports environment variables as Jam variables
at startup, *before* `Jamrules`. Haiku determines the revision during `Jamrules`
(`DefaultBuildProfiles`) — earlier than where it includes `UserBuildConfig`, so
setting `HAIKU_REVISION` there is too late. `tools/jr-jam` therefore presets
`HAIKU_REVISION` in the environment and `exec`s jam; all other env (toolchain
paths, `HOST_PYTHON`), flags, and targets pass straight through. Export
`HAIKU_REVISION` yourself to override the composed value.

## How to add a new driver

1. **Pick the overlay path** to mirror Haiku's device class, e.g.
   `overlay/drivers/input/<name>/` for an input driver or
   `overlay/bus_managers/<name>/` for a shared bus module. The path should match
   where the add-on lands in a real Haiku tree.
2. **Add sources** (`.cpp`/`.h`) with the SPDX header:

   ```
   // SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
   // SPDX-License-Identifier: MIT
   // SPDX-FileContributor: Generated with GitHub Copilot
   ```

   Name the model that actually generated the file on the contributor line.
3. **Write the module `Jamfile`** starting with `# SPDX` comments, then
   `SubDir JIDO_RENGA_TOP overlay <class> <name> ;` and a `KernelAddon`
   declaration. Reference the captive's ABI with `UsePrivateKernelHeaders` and
   pristine shared sources via `$(HAIKU_TOP)` when needed — this is correct
   vendoring. Never `#include` an overlay path.
4. **Publish shared headers** (if the module is consumed by others) under
   `overlay/headers/<root>/` and include them with a neutral root such as
   `<common/foo.h>` — never anything containing `jido-renga`.
5. **Register the module** by adding a `SubInclude JIDO_RENGA_TOP overlay
   <class> <name> ;` line to the appropriate `Jamfile` (shared modules before
   the leaves that consume them).
6. **Wire it into a BSP manifest** under `config/bsp/`. Declare the add-on in
   that BSP's directory-specific image list and, if it exclusively replaces an
   upstream add-on, add the exact `haiku.hpkg` path to the BSP omission list.
   The shared image integration in `overlay/Jamfile` installs those lists into
   `haiku.hpkg`. Match the driver's runtime add-on path —
   `busses/<bus>` for a host controller, `bus_managers` for a shared bus
   module, `drivers/<class>` for a leaf. A `bus_manager` reached by name must
   sit where `get_module` maps it (the file `bus_managers/<name>` answers
   `get_module("bus_managers/<name>/v1")`). If its overlay Jam target needs a
   distinct name to avoid colliding with the captive target, add an explicit
   package-time rename to the canonical filename; never expose the private
   overlay target name as a runtime path.
   If the add-on or one of its modules is needed to discover boot media, also
   declare its Jam target in the BSP's boot-module target list. The shared
   image integration then uses Haiku's native package helper to create the
   `add-ons/kernel/boot` link to its canonical packaged location.
7. **Build it** with `../tools/jr-jam -q <name>` from the build dir and confirm
   it produces a valid kernel ELF object.
8. **Document it** with a short note under `docs/development/<name>.md` if the
   driver has non-obvious hardware behavior.

## Release validation

Before shipping a BSP milestone:

```sh
# Pure policy/concurrency coverage.
make -C tests

# Cross-link every overlay module used by the BSP.
cd generated.x86_64
../tools/jr-jam -q iosf_mbi i2c_guarded sdhci_embedded \
  cros_ec_keyboard i2c_atmel_mxt

# Compose the real package/image, not only loose add-ons.
../tools/jr-jam -q @nightly-anyboot

# Inspect the system package using the host package tool.
package_tool=$(find objects/linux -path '*/tools/package/package' \
  -type f -print -quit)
"$package_tool" list -p \
  objects/haiku/x86_64/packaging/packages/haiku.hpkg \
  | grep -E 'add-ons/kernel/(boot|bus_managers|busses/mmc|drivers/input)'
```

For Winky, require all of these:

- `add-ons/kernel/boot/sdhci_embedded`
- `add-ons/kernel/bus_managers/i2c`
- `add-ons/kernel/bus_managers/iosf_mbi`
- `add-ons/kernel/busses/mmc/sdhci_embedded`
- `add-ons/kernel/drivers/input/i2c_atmel_mxt`
- `add-ons/kernel/drivers/input/i2c_elan`
- no stock `add-ons/kernel/busses/mmc/sdhci`

When a target is renamed during packaging, extract the canonical package entry
and compare it with the built target. Record SHA-256 values for the anyboot,
`haiku.hpkg`, and critical replacement binaries in the hardware validation
ledger. Build success is not hardware proof: update that ledger only after the
actual boot, identification, I/O, and device behavior have been observed.

## Files you will touch

- `overlay/**` — driver sources, module Jamfiles, shared headers.
- `overlay/Jamfile` — the overlay walk (module registration order).
- `config/UserBuildConfig.in` — graft template and default BSP selection.
- `config/bsp/` — declarative BSP replacement, omission, and image add-on lists.
- `config/revision.conf` — `UPSTREAM_REVISION`, `JIDO_RENGA_VERSION`.
- `tools/weave` — installs/refreshes the graft; run after re-configuring a build
  dir.
- `tools/jr-jam` — the build entry point; presets the derivative revision.
- `tools/haiku-revision` — composes the revision string (rarely run directly).
