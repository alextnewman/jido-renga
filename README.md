```
━━〘 自動連歌 · JIDŌ RENGA 〙━━━━━━━━━━━━━━

            ██╗██╗██████╗  ██████╗
            ██║██║██╔══██╗██╔═══██╗
            ██║██║██║  ██║██║   ██║
       ██   ██║██║██║  ██║██║   ██║
       ╚█████╔╝██║██████╔╝╚██████╔╝
        ╚════╝ ╚═╝╚═════╝  ╚═════╝
██████╗ ███████╗███╗   ██╗ ██████╗  █████╗
██╔══██╗██╔════╝████╗  ██║██╔════╝ ██╔══██╗
██████╔╝█████╗  ██╔██╗ ██║██║  ███╗███████║
██╔══██╗██╔══╝  ██║╚██╗██║██║   ██║██╔══██║
██║  ██║███████╗██║ ╚████║╚██████╔╝██║  ██║
╚═╝  ╚═╝╚══════╝╚═╝  ╚═══╝ ╚═════╝ ╚═╝  ╚═╝

  ⟳──◇──◇──◇   self-moving linked verse — the poem continues
               itself, stanza after stanza, with no hand.
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
```

# Jidō Renga

**Out-of-tree device drivers for [Haiku](https://www.haiku-os.org/), developed
by humans and AI together.**

Jidō Renga is a Haiku-based driver project, not a broad fork of Haiku. It keeps
Haiku and its buildtools as pinned, unmodified submodules, then grafts
project-owned kernel add-ons into Haiku's build at composition time.

The name means "automatic linked verse." Each supported machine is one verse:
a focused board-support package (BSP) with the drivers and image policy needed
to make that hardware useful.

> Contributors and coding agents should read [`AGENTS.md`](AGENTS.md) and the
> [overlay build skill](skills/jido-renga-overlay-build/SKILL.md) first.

## Current platform

The first BSP targets the Samsung Chromebook 2 `XE500C12`, ChromeOS board
`winky`, built around Intel Bay Trail-M.

| Module | Purpose | Hardware |
|---|---|---|
| `iosf_mbi` | Shared IOSF sideband access | Bay Trail transaction router |
| `sdhci_embedded` | eMMC and removable-SD host controller | ACPI `80860F14`, `80860F16` |
| `cros_ec_keyboard` | 8042-compatible EC keyboard | ACPI `GOOG000A` |
| `i2c_atmel_mxt` | Atmel maXTouch touchpad | ACPI `ATML0000` |

Winky boots Haiku from removable SD, identifies and uses its eMMC, supports
installation to eMMC, and provides working keyboard and touchpad input.
A final live Winky input regression remains outstanding.

The Winky BSP is intentionally exclusive where controllers cannot safely have
two owners. Its image omits Haiku's generic SDHCI add-on and installs
`sdhci_embedded` instead; otherwise normal Haiku image composition is left
alone.

## Relationship with Haiku

Jidō Renga is proudly based on Haiku and uses Haiku's driver APIs, build system,
package format, and runtime layout. Project add-ons install at canonical Haiku
paths and do not know that they were built from an overlay.

The project does not maintain a parallel copy of Haiku. General fixes and
human-authored improvements that belong in Haiku should be contributed
upstream. Jidō Renga provides a separate home for its machine-specific and
AI-assisted driver work while respecting Haiku's contribution policies.

At build time:

- `haiku/` and `buildtools/` remain pristine captive submodules.
- `tools/weave` writes only `<build-dir>/UserBuildConfig`.
- `overlay/` mirrors Haiku's kernel add-on classes.
- Jam builds the add-ons with Haiku's own cross-toolchain and package rules.

At runtime, only the normal Haiku add-on paths remain.

## Build

The standard x86_64 setup is:

```sh
git submodule update --init
mkdir generated.x86_64
cd generated.x86_64
../haiku/configure \
  --cross-tools-source ../buildtools \
  --build-cross-tools x86_64
cd ..
tools/weave generated.x86_64
cd generated.x86_64
../tools/jr-jam -q iosf_mbi sdhci_embedded \
  cros_ec_keyboard i2c_atmel_mxt
```

Build a bootable desktop image with:

```sh
../tools/jr-jam -q @nightly-anyboot
```

`generated*/` directories are disposable build output and are never committed.
The first image build is a full Haiku build and may download HaikuPorts
packages; later builds are incremental.

The complete build, extension, and validation procedure is in
[`skills/jido-renga-overlay-build/SKILL.md`](skills/jido-renga-overlay-build/SKILL.md).

## Image composition

Loose non-packaged drivers are useful for development, but they are not enough
for boot-media controllers. Haiku's stage-two loader exposes the root system
package before ordinary device discovery, so boot-critical JR modules and their
boot links must be composed into `haiku.hpkg`.

That package keeps Haiku's technical package identity because the loader and
package daemon depend on it. JR identifies the resulting system through its
derivative revision and BSP composition instead of pretending that an
independently named root package is interchangeable. Applications such as
WebPositive remain separate packages; JR does not rebuild or rebrand the Haiku
desktop catalog.

Set `JIDO_RENGA_BSP = none` in `UserBuildConfig` before the overlay walk to
build the add-ons without applying a BSP image policy. Set
`JIDO_RENGA_INSTALL_IN_IMAGE = 0` to keep them as loose build outputs only.

## Repository layout

```text
overlay/    project-owned kernel add-ons and public headers
config/     graft template, BSP manifests, and revision configuration
tools/      weave, revision, Jam wrapper, and terminal banner
tests/      host-side policy, parser, and concurrency tests
skills/     operational guidance for coding agents
docs/       current architecture, hardware contracts, and driver references
LICENSES/   REUSE license texts
```

Useful references:

- [`docs/drivers/cros_ec_keyboard.md`](docs/drivers/cros_ec_keyboard.md)
- [`docs/drivers/i2c_atmel_mxt.md`](docs/drivers/i2c_atmel_mxt.md)
- [`docs/design/sdhci_embedded.md`](docs/design/sdhci_embedded.md)
- [`docs/hardware/winky-bay-trail-sdhci.md`](docs/hardware/winky-bay-trail-sdhci.md)

## Engineering approach

The drivers use Haiku's conventions and modern C++ without exceptions or RTTI.
Hardware-independent policy is separated where practical so it can be tested
on the host. Hardware contracts, concurrency invariants, and current
limitations are documented; investigation transcripts and temporary build
evidence are not part of the repository documentation.

AI does substantial implementation work here, but human direction and review
remain part of the design. Generated code is expected to meet the same
correctness, licensing, and maintenance standards as any other kernel code.

## Status and license

Jidō Renga is experimental board-support software. The Winky BSP is usable, but
interfaces and implementation details may change as hardware support expands.

The project is MIT-licensed. See [`LICENSE`](LICENSE). Licensing metadata follows
the [REUSE](https://reuse.software/) specification through per-file SPDX tags,
`REUSE.toml`, and `LICENSES/`.
