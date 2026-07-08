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

Jidō Renga (自動連歌, *"the poem that writes itself"*) is the home of a set of
modern C++ Haiku kernel drivers that live and evolve **outside** the Haiku
source tree. Haiku is present here only as a **captive git submodule**; this
repository grafts its own drivers onto that submodule at build time without
ever modifying it. All out-of-tree driver history lives only here.

Think of it as a vine encircling a tree: the tree (Haiku) is untouched and
upstream; the vine (Jidō Renga) climbs it, draws from it, and bears its own
fruit.

> Run `tools/banner.sh` for the colored terminal lockup.

---

## Why

Haiku has adopted a "no AI code" contribution policy. Jidō Renga is a place to
build and maintain AI-assisted drivers for real hardware **separately**, taking
advantage of Haiku's clean add-on architecture and its 1990s-lineage modular
kernel — where a driver is just a shared object the kernel discovers and loads.
The goal is simple: make the device work well, and keep that work in its own
home.

---

## The two rules that shape everything

### 1. Jidō Renga is a *build-time* concept only

The `overlay/` layout, the `JIDO_RENGA_TOP` variable, the graft — none of that
exists at runtime. At runtime the add-ons deploy to their **canonical Haiku
paths** (`add-ons/kernel/bus_managers/`, `add-ons/kernel/drivers/bin/`, …) and
behave like any other driver — *Good Driver Citizens*. Nothing in the shipped
binaries knows it came from an overlay.

### 2. Vendoring happens at build time; it never leaks into code

- Overlay `#include`s **never** name "jido-renga". Shared modules publish their
  public API under a neutral include root and are consumed as, e.g.,
  `<common/iosf_mbi.h>`.
- Referencing the captive submodule's kernel ABI (`UsePrivateKernelHeaders`)
  or its pristine shared sources (via `$(HAIKU_TOP)`) is *correct build-time
  vendoring*, not a leak.
- Shared Jidō Renga modules own their own headers with their own identifiers —
  we do **not** reuse Haiku's private-header namespaces (`UsePrivateHeaders
  drivers`). We are intentionally vendored, everywhere.

The directory layout under `overlay/` mirrors **Haiku's device-class
categories** on purpose (`bus_managers/`, `drivers/input/`, …), so a driver's
overlay location matches where it lands in the tree.

---

## Repository layout

```
jido-renga/
├── overlay/                     # the build-time graft surface
│   ├── Jamfile                  # walks the overlay (SubInclude of each module)
│   ├── headers/                 # Jidō Renga's own public include root
│   │   └── common/
│   │       └── iosf_mbi.h       # -> included as <common/iosf_mbi.h>
│   ├── bus_managers/
│   │   └── iosf_mbi/            # shared module: BayTrail IOSF-MBI sideband
│   └── drivers/
│       └── input/
│           ├── cros_ec_keyboard/   # ACPI GOOG000A ChromeOS EC keyboard
│           └── i2c_atmel_mxt/      # Atmel maXTouch I²C touchscreen
├── config/
│   ├── UserBuildConfig.in       # the checked-in graft template
│   └── revision.conf            # derivative-revision inputs (upstream pin + version)
├── tools/
│   ├── weave                    # installs the graft into a Haiku build dir
│   ├── haiku-revision           # composes <upstream>+jido-renga-<version>
│   ├── jr-jam                   # jam wrapper that presets the derivative revision
│   └── banner.{sh,txt}          # branding
└── design/
    └── sdhci_embedded/          # design seed for the future SDHCI/MMC refactor
```

`generated*/` (the throwaway Haiku build output) is **not** part of the repo.

---

## How the graft works (Jam-native)

Haiku's build system is Jam. Jidō Renga hooks into exactly **one** seam:
`UserBuildConfig`, a file Haiku's `Jamrules` automatically includes from the
build output directory. That file is the *only* thing ever written into the
captive Haiku build scope — the submodule source tree stays pristine.

The mechanism, in three moves:

1. **Set a foreign top variable.** `UserBuildConfig` is read very early (before
   Jam's per-architecture/platform scaffolding exists), so it does only one
   safe thing: `JIDO_RENGA_TOP = <abs path to this repo> ;`. Jam's `SubDir` /
   `SubInclude` rules dereference their first token as a variable name, so a
   *foreign* top like `JIDO_RENGA_TOP` roots sources **outside** `HAIKU_TOP`.

2. **Defer the walk.** A plain `SubInclude` this early silently no-ops
   (`KernelAddon` bails when `PLATFORM` is empty). So we use
   `DeferredSubInclude JIDO_RENGA_TOP overlay ;`, which runs at the **end of
   the root Jamfile**, where `PLATFORM=haiku` is valid and `KernelAddon` works.

3. **Walk the overlay.** `overlay/Jamfile` `SubInclude`s each module. Shared
   modules first (leaf drivers may depend on what they publish), then the
   leaves. Each module's `Jamfile` uses ordinary `KernelAddon` — the objects
   land in the normal Haiku output tree and link with the kernel toolchain.

The result: `jido-renga/overlay/.../foo.cpp` compiles with the full Haiku
kernel cross-toolchain, from a source path Jam otherwise would never look in.

---

## Building

Jidō Renga follows Haiku's standard **out-of-tree** build model (the same one
the official [x86_64 guide](https://www.haiku-os.org/guides/building/compiling-x86_64)
uses) — the build output dir can live anywhere; putting it in the repo is just
convention.

```sh
# 0. Fetch the captive Haiku + buildtools (pinned submodules).
git submodule update --init

# 1. Configure a Haiku build output dir (throwaway, not committed),
#    as a sibling of the captive submodules. (configure needs Python >= 3.10.)
mkdir generated.x86_64 && cd generated.x86_64
../haiku/configure --cross-tools-source ../buildtools --build-cross-tools x86_64
cd ..

# 2. Weave the graft into that build dir (idempotent, non-destructive).
tools/weave generated.x86_64

# 3. Build a driver (or all of them) from the build dir, via the jr-jam wrapper
#    (it presets the derivative HAIKU_REVISION — see "Revision model").
cd generated.x86_64
../tools/jr-jam -q iosf_mbi cros_ec_keyboard i2c_atmel_mxt
```

`tools/weave` renders `config/UserBuildConfig.in` (substituting this repo's
absolute path) and merges it into `<build-dir>/UserBuildConfig` inside a
managed marker block, so it coexists with any settings you keep there and can
be re-run safely.

> **Layout note:** in the end-state this repo carries captive `haiku` and
> `buildtools` submodules with `generated.x86_64/` beside them — the OOT build
> above is proven at that scope (see Status). Any surrounding Haiku checkout is
> only an extraction source and is never referenced by the build.

---

## Revision model

Haiku stamps a **revision** (normally an `hrev` number) into `libroot`, surfaced
by `uname` and AboutSystem. It derives that string from `hrev` git tags — which
the GitHub mirror the captive tracks **does not carry**. Left alone, an OOT build
aborts with *"Haiku revision could not be determined."*

Rather than fake an upstream `hrev`, Jidō Renga records an honest **derivative**
identity — `<upstream>+jido-renga-<version>`:

- `<upstream>` is the pinned captive's own label: the recorded `hrev` of the
  release you pin to (`UPSTREAM_REVISION` in `config/revision.conf`), or, when
  unpinned, the captive `HEAD` short commit as `g<sha>`.
- `<version>` is this overlay's version (`JIDO_RENGA_VERSION`).

`tools/haiku-revision` composes it — e.g. `hrev57937+jido-renga-0.1.0` (pinned)
or `gb5fd959d60+jido-renga-0.1.0` (unpinned) — kept under Haiku's 128-char limit.

**Delivery.** jam imports environment variables as Jam variables at startup,
*before* `Jamrules` runs. Haiku determines the revision during `Jamrules`
(`DefaultBuildProfiles`), which is *earlier* than where it includes the per-build
`UserBuildConfig` — so setting `HAIKU_REVISION` there is too late. `tools/jr-jam`
therefore presets `HAIKU_REVISION` in the environment and `exec`s jam; all other
env (toolchain paths, `HOST_PYTHON`), flags, and targets pass straight through.
Export `HAIKU_REVISION` yourself to override the composed value.

---

## The drivers

| Module | Class | Binds | Notes |
|--------|-------|-------|-------|
| `iosf_mbi` | bus_manager (shared) | — | BayTrail IOSF message-bus sideband. A shared Jidō Renga module: consumed by other overlay drivers (e.g. the future SDHCI), never referenced outside the overlay. Publishes `<common/iosf_mbi.h>`. |
| `cros_ec_keyboard` | drivers/input | ACPI HID `GOOG000A` | ChromeOS Embedded Controller keyboard (i8042-class). Self-contained. |
| `i2c_atmel_mxt` | drivers/input | Atmel maXTouch | I²C touchscreen. The exemplar factored C++ driver: an `MxtDevice` owns transport / object-table discovery / worker thread / event ring buffer, with a separate `TouchEngine`. Compiles the pristine `hid_shared/DeviceList` from the captive tree. |

`design/sdhci_embedded/` is a non-building **design seed** — a flat, idiomatic
`device_manager` model (static HID/UID matcher → engine → SD/MMC bus) intended
to guide the eventual extraction and refactor of the SDHCI/MMC boot-disk
driver. That one is the real challenge: because SD/MMC is the boot medium, it
must ship as a true early-boot core driver, not a runtime-installable leaf.

---

## Status

**Proven**

- Jam-native foreign-`TOP` graft: `jido-renga/overlay/**` sources compile with
  the Haiku kernel cross-toolchain via `DeferredSubInclude` + the
  `UserBuildConfig` seam. The captive tree is never modified.
- **Full out-of-tree build against a pristine captive:** a fresh `haiku` master
  (where these driver sources do *not* exist) plus `buildtools`, configured as a
  sibling `generated.x86_64/`, builds all three overlay add-ons to valid
  `x86-64` ELF kernel objects — proving the overlay is their sole provider.
- Captive `haiku` and `buildtools` are pinned git **submodules** of this
  superproject; the overlay is grafted purely at build time.
- The shared `<common/iosf_mbi.h>` vendoring compiles clean into a real
  `x86-64` kernel object — no `jido-renga` leak in any `#include`.
- **Derivative revision** `<upstream>+jido-renga-<version>` composed by
  `tools/haiku-revision` and delivered by `tools/jr-jam`; verified landing in
  the build's `haiku-revision` (clears *"revision could not be determined"*).
- `tools/weave` installs the graft idempotently and non-destructively.

**Next**

- Extract and refactor the SDHCI/MMC driver (the early-boot boot-disk case)
  onto the flat model seeded in `design/`.
- Produce the two build outputs: a runtime-loadable add-on pack, and an
  `anyboot` image with early-boot drivers inserted.
- Write the skill-ready driver-authoring guide (`device_manager` + ACPI
  layering) that motivated this workspace.
