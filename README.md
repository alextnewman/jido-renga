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

**Out-of-tree device drivers for [Haiku](https://www.haiku-os.org/).**

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

## Goals

- **Extend Haiku; don't break it.** Every driver adds hardware support without
  touching a line of Haiku's own sources. The system stays exactly what upstream
  shipped — augmented, never forked.
- **Follow Haiku's lead.** Match its driver patterns and APIs, honor the
  BeOS-derived sensibilities the project has cultivated for decades, and write to
  Haiku's [coding guidelines](https://www.haiku-os.org/development/coding-guidelines).
  A Jidō Renga driver should read like it belongs in the tree it augments.
- **Keep AI-assisted work in its own home.** These drivers are developed with AI
  assistance. Haiku's own [`AGENTS.md`](https://github.com/haiku/haiku/blob/master/AGENTS.md)
  asks that such work stay out of its tree, and Jidō Renga respects that: the code
  is insulated at the driver layer, kept out-of-tree, and clearly its own — not
  part of Haiku, and never presenting itself as such.

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
