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

**An out-of-tree driver overlay for [Haiku](https://www.haiku-os.org/).**

Jidō Renga (自動連歌, *"the poem that writes itself"*) is a set of modern C++
Haiku kernel drivers that live and evolve **outside** the Haiku source tree.
Haiku is present here only as a **captive git submodule**; this repository grafts
its own drivers onto that submodule at build time without ever modifying it. All
out-of-tree driver history lives only here.

Think of it as a vine encircling a tree: the tree (Haiku) is untouched and
upstream; the vine (Jidō Renga) climbs it, draws from it, and bears its own
fruit.

> Run `tools/banner.sh` for the colored terminal lockup.

> **Contributing, or using an AI agent here?** Start with [`AGENTS.md`](AGENTS.md)
> and the [overlay build skill](skills/jido-renga-overlay-build/SKILL.md).

---

## Why

Haiku has adopted a "no AI code" contribution policy. Jidō Renga is a place to
build and maintain AI-assisted drivers for real hardware **separately**, taking
advantage of Haiku's clean add-on architecture and its 1990s-lineage modular
kernel — where a driver is just a shared object the kernel discovers and loads.
The goal is simple: make the device work well, and keep that work in its own
home. This project is **not** part of Haiku and does not present itself as such.

---

## The two rules that shape everything

**1. Jidō Renga is a *build-time* concept only.** The `overlay/` layout, the
`JIDO_RENGA_TOP` variable, the graft — none of it exists at runtime. Shipped
add-ons deploy to their **canonical Haiku paths** (`add-ons/kernel/bus_managers/`,
`add-ons/kernel/drivers/bin/`, …) and behave like any other driver. Nothing in
the shipped binaries knows it came from an overlay.

**2. Vendoring happens at build time; it never leaks into code.** Overlay
`#include`s never name "jido-renga" — shared modules publish their API under a
neutral include root (e.g. `<common/iosf_mbi.h>`). Referencing the captive's
kernel ABI or its pristine sources is correct build-time vendoring, not a leak.

The directory layout under `overlay/` mirrors **Haiku's device-class
categories** on purpose, so a driver's overlay location matches where it lands in
the tree.

---

## Repository layout

```
jido-renga/
├── overlay/            # the build-time graft surface
│   ├── Jamfile         # walks the overlay (SubInclude of each module)
│   ├── headers/common/ # Jidō Renga's own public include root
│   ├── bus_managers/   # shared modules (e.g. iosf_mbi)
│   └── drivers/        # leaf drivers (e.g. input/cros_ec_keyboard, input/i2c_atmel_mxt)
├── config/             # UserBuildConfig.in (graft template) + revision.conf
├── tools/              # weave, haiku-revision, jr-jam, banner.{sh,txt}
├── skills/             # agent skills (overlay build/extend playbook)
├── docs/               # development logs + design notes
└── LICENSES/           # REUSE license texts (MIT)
```

`generated*/` (the throwaway Haiku build output) is **not** part of the repo.
`haiku/` and `buildtools/` are pinned captive submodules.

---

## Building

Jidō Renga follows Haiku's standard **out-of-tree** build model. In short:

```sh
git submodule update --init                     # fetch the captive
mkdir generated.x86_64 && cd generated.x86_64   # a throwaway build dir
../haiku/configure --cross-tools-source ../buildtools --build-cross-tools x86_64
cd .. && tools/weave generated.x86_64           # install the graft
cd generated.x86_64
../tools/jr-jam -q iosf_mbi cros_ec_keyboard i2c_atmel_mxt
```

`tools/weave` grafts the overlay by writing one managed block into the build
dir's `UserBuildConfig`; `tools/jr-jam` presets the derivative revision (below).
The full mechanics — the Jam-native graft, the host build environment, and the
recipe for adding a new driver — are in the
[overlay build skill](skills/jido-renga-overlay-build/SKILL.md).

---

## Revision model

Haiku stamps a **revision** into `libroot` (surfaced by `uname`/AboutSystem)
derived from `hrev` git tags, which the captive's GitHub mirror does not carry.
Rather than fake an upstream `hrev`, Jidō Renga records an honest **derivative**
identity — `<upstream>+jido-renga-<version>` (e.g. `hrev57937+jido-renga-0.1.0`)
— composed by `tools/haiku-revision` and delivered by `tools/jr-jam`. See the
[skill](skills/jido-renga-overlay-build/SKILL.md) for the delivery seam.

---

## The drivers

| Module | Class | Binds | Notes |
|--------|-------|-------|-------|
| `iosf_mbi` | bus_manager (shared) | — | BayTrail IOSF message-bus sideband. Consumed by other overlay drivers, never referenced outside the overlay. Publishes `<common/iosf_mbi.h>`. |
| `cros_ec_keyboard` | drivers/input | ACPI HID `GOOG000A` | ChromeOS Embedded Controller keyboard (i8042-class). Self-contained. |
| `i2c_atmel_mxt` | drivers/input | Atmel maXTouch | I²C touchscreen. The exemplar factored C++ driver: an `MxtDevice` owns transport / object-table discovery / worker thread / event ring buffer, with a separate `TouchEngine`. |

Per-driver development logs live in [`docs/development/`](docs/development/). The
design study for the eventual SDHCI/MMC boot-disk driver — the real challenge,
since SD/MMC is the boot medium and must ship as a true early-boot core driver —
is in [`docs/design/sdhci-worker-architecture.md`](docs/design/sdhci-worker-architecture.md).

---

## Status

**Proven**

- Jam-native foreign-`TOP` graft: `overlay/**` sources compile with the Haiku
  kernel cross-toolchain via `DeferredSubInclude` + the `UserBuildConfig` seam.
  The captive tree is never modified.
- **Full out-of-tree build against a pristine captive:** a fresh `haiku` master
  (where these sources do *not* exist) plus `buildtools`, configured as a sibling
  `generated.x86_64/`, builds all three overlay add-ons to valid `x86-64` ELF
  kernel objects — proving the overlay is their sole provider.
- Captive `haiku` and `buildtools` are pinned git **submodules**; the overlay is
  grafted purely at build time.
- The shared `<common/iosf_mbi.h>` vendoring compiles clean — no `jido-renga`
  leak in any `#include`.
- **Derivative revision** `<upstream>+jido-renga-<version>` composed and
  delivered end-to-end (clears *"revision could not be determined"*).
- `tools/weave` installs the graft idempotently and non-destructively.

**Next**

- Extract and refactor the SDHCI/MMC driver (the early-boot boot-disk case).
- Produce the two build outputs: a runtime-loadable add-on pack, and an
  `anyboot` image with early-boot drivers inserted.
- Write the skill-ready driver-authoring guide (`device_manager` + ACPI
  layering) that motivated this workspace.

---

## License

MIT — see [`LICENSE`](LICENSE). Licensing metadata follows the
[REUSE](https://reuse.software/) specification; per-file SPDX tags plus
`REUSE.toml` and `LICENSES/` cover every committed file.
