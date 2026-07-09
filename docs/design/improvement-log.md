# sdhci_embedded — Improvement Log

Running log of known-broken behavior, parity compromises, and post-parity
improvement ideas for `sdhci_embedded`. Nothing here blocks boot parity with the
`dev/dense/emmc-emergency` fork; these are items to **discuss and fix after** we
can boot the drive and it works as well as the fork.

From §10 on this log also captures overlay-wide build lessons and sibling-driver
(input) findings, since those share the same "note it, discuss later" workflow.

The guiding rule during convergence: reach parity first, prefer the documented
"meow bus" ideals over the fork's actual hot-path code, and record every place
where we knowingly diverge or defer.

## 1. Confirmed-working baseline (do not regress)

From the fork author, verified on real WINKY (Bay Trail) hardware:

- **SD card initialization fully works.**
- **SDMA works well** for SD data transfer.

These two paths are the parity bar. Everything below is at or beyond the edge of
what the fork actually achieved.

## 2. Known-broken / never-achieved (inherited from the fork)

These were never working in the reference driver. We are **not** expected to fix
them to reach parity, but we should be extra critical when we revisit them.

1. **eMMC never fully worked.** It appears to initialize (CMD1 OCR loop, CID/RCA)
   but **DMA never worked** for eMMC. Suspect the data path, bus width, or the
   EXT_CSD-driven high-speed switch. Flag any eMMC data submission as unproven.
   The EXT_CSD geometry read (`MmcCard::Identify` → `SdhciEngine::ReadDataBlock`,
   a one-shot 512-byte single-buffer SDMA read) is now wired and decodes
   `SEC_COUNT` via the host-tested pure decoder, but the read *itself* rides the
   unproven eMMC data path — treat the eMMC sector count as unverified until it
   is confirmed on hardware. It falls back to the CSD estimate on read failure.
2. **ADMA2 never worked (WIP).** The descriptor-table build and/or the 32-bit
   descriptor layout is suspect. Treat ADMA2 as unverified; SDMA is the trusted
   path. Be extra critical of the descriptor `length`/`attr` encoding, the
   `0x3FFF` segment cap, and the physical-address resolution when we revisit.
3. **Bus width / voltage / speed step-up never worked → the driver is slow.**
   The fork runs data at 25 MHz, 1-bit, 3.3 V. High-speed, UHS voltage switch
   (1.8 V), and 4-/8-bit bus width were never landed. This is the biggest
   perf lever and needs a careful, critical redesign.
4. **iosf-mbi is unfinished and effectively non-functional in the fork.** The
   real MBI init was placed in an `init_module()` that is never called (the
   module's `std_ops(B_MODULE_INIT)` path does the work in a real driver). We
   are rebuilding it from the Linux reference rather than treating the fork as
   sacred. We only know that we *want* a reusable iosf-mbi bus manager.

## 3. Parity compromises made during convergence (intentional divergence)

Places where our clean skeleton knowingly differs from the fork. Each is a
candidate for later discussion.

- **Read-only polling in the convergence loop (meow ideal, not the fork's
  actual code).** Per the confirmed design, the worker does *pre-armed poll →
  CV wait (woken early by a lossy meow) → re-poll*, with **no unconditional
  snoozes in the hot path**. The fork's actual loop had grown snoozes; we
  deliberately prefer the idealized design. Watch for any behavior regression
  that the snoozes were papering over (e.g., a controller that needs a settle
  delay before its interrupt latches).
- **Bounded inhibit-grace before declaring Busy.** Where the fork immediately
  returns `B_BUSY` if `CMD_INHIBIT`/`DAT_INHIBIT` is set at issue time, we poll
  the inhibit bits for a bounded grace window first (`kInhibitGracePolls`) and
  only then surface Busy to the convergence policy. This is strictly more
  tolerant than the fork and should reduce spurious bus resets, but it is a
  behavioral change — confirm it does not mask a genuinely wedged controller.

## 4. Speed / correctness regressions to revisit post-parity

- **`RestoreAfterReset` drops the clock to 400 kHz.** After a full bus reset the
  fork (and now we) re-bring-up at the 400 kHz identification clock and do **not**
  restore the previous operating clock (25 MHz). Any transaction that triggers a
  bus reset therefore silently runs the rest of the session slow until the next
  full re-identification. Post-parity: re-apply the negotiated clock (and, later,
  bus width) at the tail of `RestoreAfterReset`.
- **Post-identification clock is hard-coded to 25 MHz.** `_IdentifyCard` steps
  400 kHz → 25 MHz to match the fork. There is no high-speed (50 MHz) or UHS
  negotiation. This is the "slow driver" the author flagged; redesign alongside
  bus-width and voltage switching.

## 5. Open uncertainties to resolve during remaining parity work

- **ACPI `_CRS` node walk-up depth — RESOLVED.** The fork's `sdhci_acpi` calls
  `get_parent_node` twice because it carries an extra SDHC-bus meta-node between
  the ACPI device and the controller. Our flattened glue binds the controller
  directly under the ACPI device (`register_node(node, kControllerModuleName…)`),
  so the ACPI device *is* our immediate parent: `_MapResources` and
  `_SelectPersonality` both walk up exactly **once**. Confirmed by reading the
  glue's node registration; resource mapping targets the right node.
- **Block-IO scheduler wiring is kcheck-only.** `Disk::DoIO` now builds a real
  `DMAResource` + `IOSchedulerSimple` from each strategy's `Restrictions()` and
  runs each decomposed operation through `Transfer()` (matching the fork's
  `mmc_disk` scheduler path). This compiles against the real kernel headers but
  cannot be host-tested — it exercises only on hardware. Verify on-target that
  operation decomposition, physical vecs, and `OperationCompleted` accounting
  behave (especially the SDMA single-segment bounce-buffer case).
- **Interrupt path is kcheck-only.** `_MapResources` → `_InstallInterrupt` now
  installs a real ISR that forwards to the engine's `HandleInterruptMeow`, and
  the worker's idle path drains storm-prone latched bits (see §7). Neither the
  ISR nor the idle-drain can be host-tested (they touch MMIO); verify on-target
  that the line falls quiet after a card event and that command latency improves
  versus the recheck-timer-only fallback.

## 6. Bugs found and fixed during convergence (within the "fix obvious bugs" mandate)

Concrete defects fixed while converging the clean skeleton. These are *fixes*,
not deferred items, but recorded so the divergence from the raw fork is auditable.

- **SD ACMD41 was being issued with eMMC CMD1's opcode.** The engine writes the
  raw `Cmd` enum value to the command register. `SdCard::Identify` used
  `Cmd::MmcSendOpCond` (opcode 1) after the CMD55 prefix, which the card reads as
  **ACMD1**, not **ACMD41** (opcode 41). This would break SD power-up negotiation
  on the known-good path. Added `Cmd::SdSendOpCond = 41`, pointed `SdCard` and
  `GetSdOpCondConstraints` at it, and added regression tests
  (`test_command.cpp: sd_op_cond_opcode_is_acmd41_not_cmd1`, `sd_op_cond_*`).
- **Host bus width was being widened without a card-side command.** `SdCard`/
  `MmcCard` called `SetBusWidth(4)`/`SetBusWidth(8)` (host register only) with no
  matching ACMD6/CMD6 to the card — the host and card would disagree on width and
  corrupt every subsequent transfer. Removed; identification and data now run at
  1-bit, matching the working reference (see §4). Proper device-first widening is
  the deferred speed item.
- **CMD1 (eMMC) argument was missing the access-mode valid marker (bit 31).** The
  reference issues CMD1 with `(1<<31)|(1<<30)|0xFF8000`; the skeleton used
  `(1<<30)|0xFF8000`. Corrected so extended-capacity (sector) addressing is
  negotiated as the reference does.
- **CMD0 result and the post-CMD0 settle were ignored.** `Card::Probe` now returns
  "no card" when CMD0 times out (empty removable slot) and honors the ~30 ms
  post-CMD0 settle the reference requires before the next command.
- **CMD8-failure did not clear the eMMC error state.** Issuing CMD8 (no data
  phase) to an eMMC leaves it wedged; the reference resets the bus before the MMC
  path. Added `SdhciEngine::RecoverBus()` and call it before building `MmcCard`.
- **The SD RCA was accepted without confirming the card entered the data state.**
  `SdCard` now requires the R6 status field `0x5xx` before latching the RCA, as
  the reference does.
- **DMA strategy was hard-coded to ADMA2 regardless of card dialect.**
  `_PublishDisk` created an `Adma2Disk` unconditionally, which would have routed
  the **working SD card through the known-broken ADMA2 path**. The fork selects
  per dialect (`useAdma2 = cardType == MMC…`): SD rides the proven SDMA path,
  eMMC uses ADMA2. Fixed to pick the strategy from `Card::Dialect()` so the
  confirmed-good SD+SDMA baseline is preserved.

## 7. Interrupt wiring — decisions and things to be critical about later

The ACPI `_CRS` walk (`_MapResources`) and the interrupt handler
(`_InstallInterrupt`) landed together, since the IRQ is the second `_CRS`
resource. Notes for a later hardware-informed pass:

- **Two `acpi.h` files, isolated to one TU (structural, not a bug).** ACPICA's
  `ACPI_RESOURCE` (capital `Type`/`Data`) and Haiku's native `acpi_resource`
  (lowercase, `<private/kernel/acpi.h>`) **share the struct tag `acpi_resource`**,
  so including both in one TU is a redefinition clash. `<ACPI.h>` (the device-
  manager module interface) only *forward-declares* the tag; ACPICA's `"acpi.h"`
  *completes* it with the layout the bus manager delivers at runtime. We
  quarantine the ACPICA side in `AcpiCrs.cpp` (returns a plain POD) so no other
  file has to care, and the overlay Jamfile + `tools/kcheck.sh` place
  `acpica/include` ahead of the private kernel headers so `"acpi.h"` resolves to
  ACPICA. (The case-sensitive `/Volumes/Developer` volume keeps `ACPI.h` and
  `acpi.h` distinct — on a case-insensitive volume this would be intractable.)
- **The meow ISR returns `B_HANDLED_INTERRUPT`, never `B_INVOKE_SCHEDULER`.**
  Matches the fork. `HandleInterruptMeow` does a lossy `NotifyAll` and the woken
  worker is `B_REAL_TIME_PRIORITY`, so latency is already low; but if command
  latency ever looks scheduler-bound, returning `B_INVOKE_SCHEDULER` when the CV
  actually had a waiter is the lever. Be critical: measure before changing.
- **The meow ISR reads no registers, so it cannot disown a *shared* line.** A
  purist meow returns "handled" unconditionally. On Bay Trail each SDHCI
  controller has its own dedicated interrupt line, so this is correct here. If
  this driver is ever reused on hardware that shares an SDHCI IRQ across slots,
  the ISR must read `interruptStatus` to return `B_UNHANDLED_INTERRUPT` for a
  line that isn't ours — which slightly compromises the "no reads" ideal.
- **Storm-safety idle-drain added (divergence from the raw skeleton, parity with
  the fork).** The line is level-triggered and `kSignalMask` keeps card
  insert/remove signal-enabled, but no command drain clears those bits. Without
  a drain, a single card event would re-fire the ISR forever once a live handler
  is installed. `_ServiceOnce` now clears storm-prone bits (`StormSafeIdleClear`,
  host-tested) when it wakes with no pending transaction — mirroring the fork's
  idle-path `interrupt_status` clear. Detection stays the Controller's slow
  present-state poll; the idle-drain only silences the line.
- **Brief signal-enabled-but-no-handler window at boot.** `fEngine.Init` enables
  interrupt signaling and starts the worker *before* `_InstallInterrupt` runs
  (Init cannot install — it has no IRQ). For the ~10 ms of power-on bring-up in
  between, a card event would latch with no ISR installed; the running worker's
  idle-drain still clears it within a recheck interval, and boot-time hotplug is
  not a real scenario. If we ever want the window gone, move IRQ ownership into
  the engine so install can precede signal-enable (costs an `Init` signature
  change and re-touches the tested engine — deferred deliberately).

## 8. Hot-plug watcher — conditional spawn and the punted loop body

- **The watcher is now spawned only for removable slots** (`MatchProfile::
  removable`, resolved in `_SelectPersonality`, checked in `_StartWatcher`). A
  soldered eMMC part has no card-detect line and can never change, so it runs
  with exactly one long-lived thread of its own (the engine worker); a removable
  SD slot adds the one poller. This makes the thread topology a function of the
  slot, exactly as intended, and guarantees nothing hot-plug-related runs at
  boot. See `docs/design/sdhci_embedded.md` §9.1 for the full topology table.
- **The watcher loop body is still a trace-only stub (punt, by agreement).** It
  detects insert/remove edges but does not yet re-identify a freshly inserted
  card or tear down a removed one. That is deliberate: boot is a one-shot active
  init, and the remaining work (re-run identify on insert, publish/unpublish the
  `Disk`, quiesce in-flight IO on remove) touches the engine and devfs lifecycle
  and deserves its own hardware-informed pass. Removable-media correctness (what
  happens to an open `Disk` when the card leaves) is the thing to be critical
  about here.

## 9. IOSF-MBI rebuild — decisions and the behavior change vs the fork

The `iosf_mbi` bus manager was rebuilt clean-room against Linux
(`arch/x86/platform/intel/iosf_mbi.c`), not ported from the fork. Two fork bugs
are fixed, and one behavior genuinely changes — flag it for discussion.

- **The fork's OCP fixup never actually ran (dead init).** The fork initialized
  its singleton from free `init_module()` / `uninit_module()` functions — a
  Linux idiom Haiku never calls. Its real std_ops `B_MODULE_INIT` therefore saw
  a null singleton and returned `B_ERROR`, so `get_module()` failed and
  `sdhci_byt_iosf_mbi_ocp_fixup()` silently no-op'd. **This means the reference's
  "working" SD + SDMA path worked WITHOUT the OCP fixup.** Our rewrite moves
  discovery into `B_MODULE_INIT`, so on real Bay Trail the fixup will now run for
  the first time. That is the intended fix, but it is a behavior change from the
  binary that was proven on hardware — if the SD path ever regresses, this is the
  first suspect. Kept as a **soft dependency** (missing module => log + continue)
  precisely so it cannot fail a boot that used to work.
- **The fork bound the wrong PCI function.** Its device list put the PMC
  (8086:0F1C) first and even included the SDHCI controllers (0F14/15/16). The
  message-bus registers (MCR/MDR/MCRX at 0xD0..0xD8) live ONLY in the SoC
  transaction-router / host-bridge config space (0:0:0). Writing 0xD0 on the PMC
  or an SDHCI function is not a sideband access — at best inert, at worst poking
  a real register. We bind strictly to the host bridge (BYT 8086:0F00,
  CHT 8086:2280), matching Linux's `iosf_mbi_pci_ids`.
- **The MCR/MDR encoding was the one correct part and is preserved** — now in a
  pure, host-tested header (`IosfMbiProtocol.h`: `FormMcr`, `McrxFor`), verified
  against Linux's `mbi_form_mcr` + `MBI_MASK_HI/LO` split.
- **Things to be critical about later:** (1) the SCCEP OCP magic (unit 0x63,
  reg 0x1078, mask [10:8]) is inherited BYT-specific lore — confirm on metal that
  clearing it is what unblocks long/eMMC/high-speed commands, and whether the
  working SD path is truly indifferent to it. (2) The fixup runs once at boot and
  releases the module immediately; if other consumers (audio/thermal/PUnit) ever
  need it, promote it to a longer-lived hold. (3) No locking around the MCR/MDR
  handshake yet — fine for a single boot-time caller, but the real Linux driver
  serializes because the sideband is shared; add a lock before a second consumer
  lands.

## 10. First real cross-build — findings the syntax-only kcheck could not surface

The overlay was carried to a **real jam cross-build** (not just `-fsyntax-only`):
both `iosf_mbi` and the full 13-TU `sdhci_embedded` now **compile and link** with
the captive toolchain, producing loadable ELF add-ons. Two problems surfaced that
kcheck (which never links and never parses the Jamrules tree) structurally could
not catch.

### 10.1 Revision determination fires *before* UserBuildConfig — inject via BuildConfig

- **Symptom:** `jam sdhci_embedded` died at parse time with `ERROR: Haiku revision
  could not be determined.`, before compiling anything.
- **Root cause (two independent facts that only bite together):**
  1. The GitHub mirror carries no `hrev*` tags and the submodule `.git` is a
     gitlink **file**, so Haiku's git-based detection (`build/jam/FileRules`
     `DetermineHaikuRevision`) finds neither `HAIKU_REVISION` nor a usable
     `.git/index`, and calls `Exit`.
  2. The graft originally recorded `HAIKU_REVISION` in `UserBuildConfig`, which
     Jamrules includes at ~line 120 — but the `RemotePackageRepository` include
     at ~line 111 already calls `DetermineHaikuRevision` (via `PackageRules`) at
     **parse** time. So the override was installed one include too late.
- **Fix:** `tools/weave` now records `HAIKU_REVISION ?= "<rev>" ;` in a managed
  marker block inside `<output>/build/BuildConfig`, which Jamrules includes at
  ~line 62 — before DefaultBuildProfiles and the repository walk. `UserBuildConfig`
  deliberately no longer sets it (a comment there explains why). `?=` is used so an
  explicit `jam -sHAIKU_REVISION=...` still wins. Both seams stay inside the
  generated build dir; the submodule source tree is still never touched. `weave`
  warns if `build/BuildConfig` is missing (configure hasn't run yet).
- **Be critical about later:** BuildConfig is regenerated by `configure`, so
  `weave` must be re-run after every reconfigure (same lifecycle the UserBuildConfig
  graft already had). If a future toolbox drives configure automatically, chain
  `weave` right after it.

### 10.2 `GetPersonality` function-local statics broke the kernel link

- **Symptom:** everything compiled; the link failed with `undefined reference to`
  `__cxa_guard_acquire` / `__cxa_guard_release` / `atexit`, all from
  `Personality.o`.
- **Root cause:** `GetPersonality()` returned references to two **function-local
  `static`** personality objects. Function-local statics of non-POD types pull in
  `__cxa_guard_*` (thread-safe one-time-init guards) and, because the base had a
  public **virtual destructor**, `atexit`/`__cxa_atexit` (static-storage dtor
  registration). The kernel runtime provides none of these.
- **Fix (textbook idiom for never-deleted polymorphic singletons):** moved the two
  objects to **anonymous-namespace file scope** (removes the guards — namespace
  init is unguarded and runs from the add-on's `.init_array`, which the module
  loader calls) and made `HostPersonality`'s destructor **protected and
  non-virtual** (makes the whole hierarchy trivially destructible → no atexit,
  while still preventing `delete basePtr`). No behavior change; the objects remain
  stateless singletons returned by `const&`.
- **General rule for this overlay (worth promoting into the SKILL):** in kernel
  C++, avoid function-local statics with non-trivial init, and give any polymorphic
  base that is never deleted through the base a protected non-virtual destructor.
  kcheck cannot catch either because it never links.

## 11. Input drivers (cros_ec_keyboard, i2c_atmel_mxt) — quality-pass findings

A test/quality pass mirroring the sdhci_embedded work was run over the two input
drivers before the first anyboot attempt. Both **build and link cleanly**, pass the
kernel-C++ hygiene scan (no function-local statics, RTTI, or exceptions — the class
of bug that bit `Personality`), carry full SPDX, and register correctly with
device_manager. Two trace-only `-Wunused-but-set-variable` warnings were fixed
(cros_ec `Driver.cpp` `id[2]`, mxt `MxtDevice.cpp` T100 `amplitude`) with the
existing `(void)var;` idiom, and a stale `ObjectTable.h` comment that called the
start address "big-endian" was corrected (the wire format is little-endian; the
parser and the new tests confirm it). Host tests were added for the mxt
object-table parser (`tests/i2c_atmel_mxt/test_object_table.cpp`). The items below
are deferred for discussion.

### 11.1 maXTouch CRC-24 is dead, self-inconsistent, and likely mis-specified

- **Status:** latent, **not boot-affecting.** The controller in the WINKY runs with
  message CRC disabled (`MxtDevice.cpp` ~L792, "CRC disabled on this device"), and
  `ObjectTable::VerifyCRC` has **no caller anywhere in the driver** — the whole
  CRC-24 surface is currently dead code.
- **Three independent problems** (all pinned as characterization tests so any change
  trips review):
  1. **Batch vs. streaming disagree.** `mxt_crc24_compute` (batch, pair-based) and
     `mxt_crc24_update`/`finish` (streaming) return different results for the same
     bytes — `{01 02 03 04}` gives batch `0x000001` but streaming `0x020003`. At
     most one can be correct.
  2. **Polynomial looks wrong.** The code uses `0x001B0001` and claims it is the
     "reflected form of Atmel's 0x80001B". Linux's `atmel_mxt_ts` `mxt_crc24` uses
     `crcpoly = 0x80001B`, tests bit `0x1000000`, and masks `0x00FFFFFF` **once at
     the end**; the bit-reflection of `0x80001B` is not `0x001B0001`, and the
     per-iteration mask here does not match the reference structure either.
  3. **VerifyCRC hashes the wrong bytes.** It computes over `(uint8*)fObjects,
     count * sizeof(mxt_object_entry)` — the **padded in-memory struct array**
     (8 bytes/entry after alignment), not the 6-byte-packed wire format the
     controller checksums. Even with a correct polynomial it could never match.
- **Deferred:** do not rewrite blind. Validate against a real object-table read and
  its reported CRC on the WINKY, then port Linux's `mxt_crc24` verbatim and CRC the
  raw wire bytes (not the struct). No hardware to verify against right now.

### 11.2 T100 touch report aux-byte order is a fixed-offset assumption

- `MxtDevice::_ParseT100Message` reads `amplitude`/`area` at hard-coded message
  offsets and reports `area` as pressure. Real T100 aux bytes are governed by the
  `TCHAUX` config: only the enabled fields (vector, amplitude, area) are present, in
  a fixed order, so a device configured differently shifts every offset.
- **Impact is minor today:** the clickpad button comes from T19 GPIO, not pressure,
  so pressure fidelity is cosmetic. **Deferred:** parse `TCHAUX` from the T100 config
  and compute aux offsets dynamically when pressure/vector actually matter.

### 11.3 Input drivers were not architected for host testing → shim seam

- Unlike sdhci_embedded (whose pure core deliberately includes only `<stdint.h>` and
  a self-contained `Types.h`), the mxt/cros_ec sources pull in kernel headers
  transitively (`Driver.h` → `Drivers.h`/`KernelExport.h`/`OS.h`/`kernel_cpp.h` plus
  the vendored `DeviceList.h`). To unit-test the object-table parser off-target we
  added a minimal kernel-header shim under `tests/shims/` (fixed-width types, a
  `dprintf` no-op, an empty `DeviceList`) and compile `ObjectTable.cpp` **unmodified**
  against it.
- **Note for the SKILL / future drivers:** either keep parseable logic in a
  self-contained pure core (sdhci's approach — cleanest) or accept a small shim.
  cros_ec_keyboard actually *has* unit-testable logic — the `_cros_ec_decode_byte`
  scancode state machine (E0/E1 extended prefixes, the E1 Pause sequence, F0 break
  handling, keymap range checks) — but it is a `static` function buried in the
  monolithic ~1400-line `Driver.cpp` and coupled to a `driver_cookie` that carries
  a kernel `ConditionVariable` and the key ring. Testing it host-side would mean
  compiling the whole kernel-heavy TU (device_manager, ACPI, KBC port I/O,
  ConditionVariable, spinlocks). **Deferred improvement:** extract the decode
  state machine into its own pure translation unit (mirroring sdhci's
  Matcher/Personality split) so it can be unit-tested against captured EC byte
  streams; until then it stays covered by build + hygiene + registration checks.
