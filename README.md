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

**Out-of-tree device drivers for [Haiku](https://www.haiku-os.org/), written by humans and AI together.**

Haiku is an open-source operating system that carries the BeOS legacy forward —
fast, coherent, and unapologetically focused on the personal computer. Jidō Renga
(自動連歌, *"the poem that writes itself"*) develops modern C++ **kernel drivers**
for it, so that more hardware runs Haiku well.

Those drivers live and evolve **outside** the Haiku source tree. Haiku is present
here only as a **captive git submodule**; this repository grafts its drivers onto
that submodule at build time without ever modifying it. Think of a vine encircling
a tree: the tree (Haiku) stands untouched and upstream; the vine (Jidō Renga)
climbs it, draws from it, and bears its own fruit.

> Run `tools/banner.sh` for the colored terminal lockup.

> **Contributing, or using an AI agent here?** Start with [`AGENTS.md`](AGENTS.md)
> and the [overlay build skill](skills/jido-renga-overlay-build/SKILL.md).

---

## The first verse

A renga is written one stanza at a time. Jidō Renga takes each **machine** as a
verse — one modest computer brought fully to life on Haiku, with new verses added
for new hardware as the need (or the whim) arises.

The first verse is the **Samsung Chromebook 2** (11.6″, model `XE500C12`): Chrome OS
board **`winky`**, an Intel Bay Trail-M laptop — Celeron N2840, 16 GB of eMMC, the
kind of small plastic 2014 Chromebook the world stopped thinking about years ago.

There's no grand thesis behind this first verse. Its author simply had a WINKY
gathering dust in a closet and wanted to have some fun making it work — and Haiku
turns out to be a lovely partner for that: a lean, coherent system that happily
renovates modest old machines into something fast and pleasant to use. Good hardware
wants to be used; it wants to be useful. Making it work well long after its maker
moved on is a fine reward on its own; mostly, though, it's just a fun place to build.

Bringing **WINKY** up means teaching Haiku its particular parts — the ChromeOS
Embedded-Controller keyboard, the Atmel maXTouch trackpad, and the Bay Trail SoC
sideband the platform leans on. Those are the drivers below. The internal storage it
boots from is the next stretch of the verse — still being brought up, and not yet
listed here.

---

## Goals

- **Extend Haiku; don't break it.** Every driver adds hardware support without
  touching a line of Haiku's own sources. The system stays exactly what upstream
  shipped — augmented, never forked.
- **Follow Haiku's lead.** Match its driver patterns and APIs, honor the
  BeOS-derived sensibilities the project has cultivated for decades, and write to
  Haiku's [coding guidelines](https://www.haiku-os.org/development/coding-guidelines).
  A Jidō Renga driver should read like it belongs in the tree it augments.
- **A human–AI collaboration.** The name says it: a *renga* is linked verse, composed
  by many hands, each stanza answering the last. Here those hands are humans and AI —
  people bringing direction, design judgment, and hard-won taste; AI bringing the
  muscle to research, draft, test, and iterate at pace. It's no token assistance, and
  no hands-off automation either: an e-bike, not a self-driving car — the motor is
  real, but a person is always in the saddle, steering. This is a toybox and an
  experiment as much as a driver set: a place to advance the craft of AI-assisted
  driver development, and to make old things new again. Because Haiku's own
  [`AGENTS.md`](https://github.com/haiku/haiku/blob/master/AGENTS.md) asks that
  AI-assisted work stay out of its tree, Jidō Renga honors that line precisely: every
  driver is insulated at the driver layer, kept out-of-tree, and clearly its own —
  never presenting itself as part of Haiku.
- **Earn it with proof.** AI can produce a great deal of code quickly, and kernel code
  is unforgiving — so speed is only worth having when the result is *correct*. Proof is
  a first-class pillar here, not an afterthought bolted on at the end. Drivers carry
  host-side unit tests, mocks that codify the hardware's *actual* contract (not the
  datasheet's fiction), and concurrency harnesses that exercise the worker-thread and
  interrupt handoffs on real threads under sanitizers. A driver's core assumptions live
  as pure predicates checked in two places — the host tests and, on target, the
  driver's own assertions — so a wrong assumption fails loudly instead of drifting for
  months. This is idiomatic Haiku C++ held to a high bar: not generated filler, and not
  a Linux driver bolted onto a foreign kernel.

At runtime none of this shows. Each add-on installs to its canonical Haiku path
and behaves like any other driver — a good citizen of the system it joins.

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

### Into a bootable image

The line above builds the add-ons as **loose files** under the build dir. To
fold them into a Haiku **anyboot** image that loads them at boot, ask jam for an
image profile instead of the individual targets:

```sh
../tools/jr-jam -q @nightly-anyboot        # -> haiku-nightly-anyboot.iso
```

The overlay grafts each add-on into the image's **non-packaged** kernel add-on
tree (`/boot/system/non-packaged/add-ons/kernel/…`) — the writable seam the
kernel and `device_manager` scan alongside the packaged tree. The captive's
read-only packagefs is never touched; the drivers simply ride along beside it.
The result is `generated.x86_64/haiku-nightly-anyboot.iso`: stock Haiku plus
these drivers, ready to write to a USB stick (`dd`, Etcher, …) and boot.

> The first anyboot build is a full Haiku build — it compiles the OS and pulls
> HaikuPorts packages, so it is long and needs a network. Later builds are
> incremental. `@minimum-anyboot` yields a smaller image; `@nightly-anyboot` is
> the usual desktop.

Set `JIDO_RENGA_INSTALL_IN_IMAGE = 0` in `UserBuildConfig` (before the overlay
is walked) to opt out and keep building loose add-ons only — e.g. to drop onto a
running system's own `non-packaged` tree by hand.

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

Everything here serves the first verse — the parts that make up WINKY:

| Module | Class | Binds | Notes |
|--------|-------|-------|-------|
| `iosf_mbi` | bus_manager (shared) | — | BayTrail IOSF message-bus sideband. Consumed by other overlay drivers, never referenced outside the overlay. Publishes `<common/iosf_mbi.h>`. |
| `cros_ec_keyboard` | drivers/input | ACPI HID `GOOG000A` | ChromeOS Embedded Controller keyboard (i8042-class). Self-contained. |
| `i2c_atmel_mxt` | drivers/input | Atmel maXTouch | I²C touchpad (WINKY's trackpad). The exemplar factored C++ driver: an `MxtDevice` owns transport / object-table discovery / worker thread / event ring buffer, with a separate `TouchEngine`. |

Per-driver development logs live in [`docs/development/`](docs/development/), with
deeper design notes under [`docs/`](docs/).

---

## Status

Early and experimental. The out-of-tree graft, the derivative-revision model, and
the build flow are in place, and the drivers above build against a pinned Haiku.
Expect churn: drivers land as hardware is brought up, and interfaces here are free
to change until they have earned their keep.

---

## License

MIT — see [`LICENSE`](LICENSE). Licensing metadata follows the
[REUSE](https://reuse.software/) specification; per-file SPDX tags plus
`REUSE.toml` and `LICENSES/` cover every committed file.
