# Jidō Renga agent guide

Jidō Renga is an **out-of-tree Haiku driver overlay**. Haiku and buildtools are
present only as captive git submodules (`haiku/`, `buildtools/`). Project-owned
drivers are grafted into Haiku at build time without modifying either submodule.

## Invariants

1. **Never modify the captive submodules.** The only file written into a Haiku
   build scope is `<build-dir>/UserBuildConfig`, through `tools/weave`.
2. **Keep the overlay build-time only.** `overlay/`, `JIDO_RENGA_TOP`, and the
   graft do not exist at runtime. Add-ons ship at canonical Haiku paths.
3. **Never leak `jido-renga` into driver code.** Includes use neutral roots such
   as `<common/iosf_mbi.h>`. Referencing Haiku's ABI or pristine source through
   `UsePrivateKernelHeaders` and `$(HAIKU_TOP)` is valid build-time vendoring.
4. **Mirror Haiku's device classes.** An overlay path should match the runtime
   class where the add-on is installed.
5. **Keep comments operational.** Comments explain contracts, invariants,
   hardware meaning, units, or non-obvious mechanisms. Investigation history,
   discarded approaches, and author narration do not belong in code comments.

## Layout

```text
overlay/    build-time graft surface
config/     UserBuildConfig template, BSP manifests, revision configuration
tools/      weave, haiku-revision, jr-jam, and banner tools
tests/      host-side policy and concurrency tests
skills/     agent operational guidance
docs/       current architecture, hardware contracts, and driver references
LICENSES/   REUSE license texts
```

`generated*/` is disposable Haiku build output and is never committed.

## Documentation boundary

- `README.md` introduces the project to people who want to understand or build
  it.
- `skills/` records durable operational rules for agents.
- `docs/` describes the current implementation, its contracts, rationale, and
  known limitations.
- Build hashes, KDL transcripts, local artifact paths, investigation timelines,
  and discarded hypotheses belong in session-local storage, not committed
  documentation.
- Git history carries previous repository states. Do not preserve obsolete
  states as narrative inside current docs.

## Building and extending

The full procedure is in
[`skills/jido-renga-overlay-build/SKILL.md`](skills/jido-renga-overlay-build/SKILL.md).

Quick build:

```sh
git submodule update --init
mkdir generated.x86_64 && cd generated.x86_64
../haiku/configure --cross-tools-source ../buildtools --build-cross-tools x86_64
cd .. && tools/weave generated.x86_64
cd generated.x86_64
../tools/jr-jam -q iosf_mbi sdhci_embedded \
  cros_ec_keyboard i2c_atmel_mxt
```

## Licensing and attribution

The project is MIT-licensed and is not part of Haiku. Source, build, and tooling
files carry SPDX headers; files that cannot carry headers are covered by
`REUSE.toml`.

```text
// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot
```

Use the contributor line that identifies the model responsible for the work.
Existing Qwen-authored files retain `Generated with Qwen 3.6`.
