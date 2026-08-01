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

Jidō Renga builds machine-specific Haiku support without maintaining a parallel
Haiku fork. Haiku and its buildtools remain pinned, unmodified submodules;
project-owned drivers are grafted into the build through an overlay, and a
maintained Mesa fork supplies the hardware OpenGL renderer.

The name means "automatic linked verse." Each supported machine is one verse:
a focused board-support package (BSP) containing the drivers and image policy
needed to make that hardware useful.

## Winky board support

The first BSP targets the Samsung Chromebook 2 `XE500C12`, ChromeOS board
`winky`, built around Intel Bay Trail-M.

| Area | Components | Purpose |
|---|---|---|
| Storage | `sdhci_embedded` | eMMC and removable SD |
| Graphics | `intel_valleyview`, Crocus | Native display, isolated GPU rendering, and hardware OpenGL |
| Input | `cros_ec_keyboard`, `i2c_atmel_mxt` | Chromebook keyboard and Atmel maXTouch clickpad |
| Audio | `byt_max98090` | Intel SST and MAX98090 internal audio |
| USB | `byt_xhci_filter`, stock Haiku xHCI | Preserve firmware-owned Bay Trail USB routing |
| Platform services | `gpio`, `byt_gpio`, `i2c_guarded`, `iosf_mbi` | Shared GPIO, I2C, and sideband infrastructure |

The BSP composes these pieces into Haiku's normal package and runtime layout.
Drivers install at canonical Haiku paths and contain no Jidō Renga runtime
dependency.

Detailed hardware contracts and limitations live in [`docs/`](docs/).

Graphics development continues in
[`P2`](docs/design/intel_valleyview_p2.md). Its bounded timeline queue, fair
two-client scheduling, ordered fault recovery, and reset-safe BCS direct-copy
path are hardware-proven. The direct gate completed all 18 compatibility cases
with 18 successful GPU presents, no direct-present failure, and no CPU
frontbuffer fallback. Its asynchronous successor queues render and presentation
on one timeline; an eight-request hardware burst reached depth six, discarded
seven obsolete frames, and presented the newest without losing fence order.
Safe GL remains the default recovery baseline; persistent RCS contexts,
scalable residency, and performance tuning remain P2 work. EGL and browser
integration are reserved for P3.

## Why an overlay?

Machine-specific work can move quickly here while general Haiku fixes continue
to belong upstream. The overlay keeps that boundary explicit:

- `haiku/` and `buildtools/` are immutable captive submodules;
- `mesa/` is the maintained project fork used for Crocus;
- `tools/weave` writes only `<build-dir>/UserBuildConfig`;
- `overlay/` mirrors Haiku's add-on classes;
- Haiku's own Jam rules and cross-toolchain build the final package and image.

Only build-time composition is project-specific. The resulting system remains
a Haiku installation using ordinary Haiku add-on paths.

## Build and boot

Initialize the submodules and configure an x86_64 build:

```sh
git submodule update --init
mkdir generated.x86_64
cd generated.x86_64
../haiku/configure \
  --cross-tools-source ../buildtools \
  --build-cross-tools x86_64
cd ..
tools/weave generated.x86_64
```

Build the Haiku development package, the maintained Crocus renderer, and the
composed boot image:

```sh
cd generated.x86_64
../tools/jr-jam -q haiku_devel.hpkg
cd ..
tools/build-crocus generated.x86_64
cd generated.x86_64
../tools/jr-jam -q @nightly-anyboot
```

The bootable image is written to
`generated.x86_64/haiku-nightly-anyboot.iso`. Flash it with the image-writing
tool of your choice and boot the Winky from that media.

To build only the composed system package or its local repository:

```sh
../tools/jr-jam -q haiku.hpkg
../tools/jr-jam -q jido-renga-repository
```

`generated*/` directories are disposable build output and are never committed.
The first image build is a full Haiku build and may download HaikuPorts
packages; later builds are incremental.

## Repository layout

```text
overlay/    project-owned drivers and public headers
mesa/       maintained Mesa fork used by Crocus
config/     overlay template, BSP manifests, and revisions
firmware/   separately licensed firmware packaged unchanged
tools/      build composition and wrapper tools
tests/      host-side policy, parser, and concurrency tests
skills/     operational guidance for coding agents
docs/       architecture, hardware contracts, and driver references
LICENSES/   REUSE license texts
```

Useful starting points:

- [`AGENTS.md`](AGENTS.md) for repository invariants;
- [`skills/jido-renga-overlay-build/SKILL.md`](skills/jido-renga-overlay-build/SKILL.md)
  for the complete build and extension workflow;
- [`docs/drivers/`](docs/drivers/) for driver architecture;
- [`docs/design/`](docs/design/) for shared subsystem design.

## Engineering and licensing

The drivers follow Haiku conventions and use modern C++ without exceptions or
RTTI. Hardware-independent policy is separated where practical for host-side
testing. Current implementation details, limitations, and hardware rationale
belong in the focused documents under `docs/`, not in this landing page.

The project is MIT-licensed and is not part of Haiku. See [`LICENSE`](LICENSE).
Licensing metadata follows the
[REUSE](https://reuse.software/) specification. Vendored Intel SST firmware
retains its separate unmodified-binary redistribution terms.
