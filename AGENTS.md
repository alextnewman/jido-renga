# Jidō Renga — agent guide

Jidō Renga (自動連歌) is an **out-of-tree Haiku driver overlay**. Haiku and
buildtools are present only as **captive git submodules** (`haiku/`,
`buildtools/`); this repo grafts its own modern C++ kernel drivers onto them at
**build time** and never modifies them. Think of a vine on a tree: the tree
(Haiku) is untouched; the vine (Jidō Renga) climbs it and bears its own fruit.

## Invariants — do not violate

1. **Never modify the captive submodules.** `haiku/` and `buildtools/` stay
   pristine. The *only* file ever written into a Haiku build scope is
   `<build-dir>/UserBuildConfig`, and only through `tools/weave`.
2. **Build-time only.** The `overlay/` layout, `JIDO_RENGA_TOP`, and the graft
   do not exist at runtime. Shipped add-ons live at **canonical Haiku paths**
   (`add-ons/kernel/bus_managers/`, `add-ons/kernel/drivers/bin/`, …) and behave
   like any other driver. Nothing in a shipped binary knows it came from an
   overlay.
3. **Never leak "jido-renga" into code.** `#include`s use neutral roots
   (e.g. `<common/iosf_mbi.h>`), never the overlay path. Referencing the
   captive's kernel ABI (`UsePrivateKernelHeaders`) or pristine sources
   (`$(HAIKU_TOP)`) is correct build-time vendoring, not a leak. Shared modules
   own their own headers; we do not reuse Haiku's private-header namespaces.
4. **Mirror Haiku's device classes.** `overlay/` paths mirror Haiku's tree
   (`bus_managers/`, `drivers/input/`, …) so an overlay location matches where
   the add-on lands.

## Layout

```
overlay/     build-time graft surface (headers/, bus_managers/, drivers/…)
config/      UserBuildConfig.in (graft template) + revision.conf
tools/       weave, haiku-revision, jr-jam, banner.{sh,txt}
skills/      agent skills (see below)
docs/        development logs and design notes
LICENSES/    REUSE license texts (MIT)
```

`generated*/` (throwaway Haiku build output) is never committed.

## Building and extending

Full procedure — the Jam-native graft, the derivative-revision seam, the build
env, and **how to add a new driver** — is the skill:

- **`skills/jido-renga-overlay-build/SKILL.md`**

Quick version:

```sh
git submodule update --init
mkdir generated.x86_64 && cd generated.x86_64
../haiku/configure --cross-tools-source ../buildtools --build-cross-tools x86_64
cd .. && tools/weave generated.x86_64
cd generated.x86_64 && ../tools/jr-jam -q iosf_mbi cros_ec_keyboard i2c_atmel_mxt
```

## Licensing and attribution

MIT (`LICENSE`). This project is **not** part of Haiku and must not present
itself as such. Every source, build, and tooling file carries an SPDX header;
non-code files are covered by `REUSE.toml`. When you author or modify a file,
keep it REUSE-compliant:

```
// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot
```

Use the `SPDX-FileContributor` line to name the model that generated the work
(e.g. `Generated with GitHub Copilot`). Prior Qwen-authored code is marked
`Generated with Qwen 3.6`. See `docs/` for per-driver development logs.
