# sdhci_embedded architecture

The normative Winky hardware, electrical, timing, IOSF, and DMA contract is in
[`../hardware/winky-bay-trail-sdhci.md`](../hardware/winky-bay-trail-sdhci.md).
This document defines the software architecture and concurrency rules of the
`sdhci_embedded` add-on.

## Scope

`sdhci_embedded` owns the Bay Trail eMMC and removable-SD controllers selected
by the Winky BSP. It is one flat kernel add-on with a small object hierarchy:

```text
BYT ACPI loader
  -> SdhciController
       -> HostPersonality
       -> SdhciEngine
       -> Card (MmcCard or SdCard)
       -> Disk (Adma2Disk, SdmaDisk, or PioDisk)
```

- The ACPI loader applies generic SDHCI preconditions and maps a supported HID
  to a controller profile.
- `SdhciController` owns resources, boot sequencing, card lifetime, disk
  publication, and removable-media monitoring.
- `SdhciEngine` owns the register block, command worker, transaction mailbox,
  status accumulation, and completion signaling.
- `HostPersonality` contains silicon-specific reply, OCR, reset, and timing
  policy.
- `Card` implements SD or MMC identification and mode selection.
- `Disk` implements the selected data-transfer strategy.

There is one slot per controller. The driver does not model a multi-slot PCI
card cage.

## Pure policy and hardware mechanism

The engine keeps register access separate from policy that can be tested on a
host:

| Component | Responsibility |
|---|---|
| `Convergence.h` | Interrupt-status interpretation and retry decisions |
| `Contract.h` | Written hardware assumptions and assertions |
| `Transaction.h` | Ref-counted ticket, mailbox, virtual controller state |
| `Command.h` | Per-opcode response, timeout, and retry traits |
| `Matcher.h` | ACPI HID profiles |
| `Personality.h` | Per-host quirks |
| `Csd.h` | SD/MMC capacity and capability decoding |
| `StagedRequest.h` | Removable-media staging-window planning |

`SdhciEngine.cpp` applies those decisions to MMIO and kernel primitives. Policy
that determines success, failure, or retry should remain outside the register
access layer when possible.

## The meow bus

Bay Trail interrupt delivery is not trustworthy enough to carry semantic
meaning. It can be late, duplicated, spurious, or absent. The driver therefore
models the interrupt line as a cat and names the resulting wake-hint
architecture the **meow bus**.

A meow says only: **something may have happened; look at the controller.** It
does not say which command completed, whether data moved, whether an error won
the race, or even whether useful status remains by the time the worker wakes.
The sound summons the worker; controller state tells the truth.

The ISR:

1. reads raw interrupt status;
2. rejects zero, all-ones, and sources outside the managed signal mask;
3. wakes the worker through a condition variable;
4. does not acknowledge status or interpret command state.

The worker is the sole semantic and mutating SDHCI register owner. It snapshots
and acknowledges status, accumulates evidence in software, reads responses, and
updates controller state.

Two signaling primitives have different contracts:

- `fMeowCV` is a lossy ISR/caller-to-worker pulse. Missed or duplicate pulses
  are harmless because the worker also performs timed rechecks.
- `fCompletion` is a counting worker-to-caller semaphore. Transaction
  completion must not be lost.

This asymmetry is essential. A missed meow merely delays the next inspection
until the polling backstop. Several meows may collapse into one inspection
because the worker drains all available evidence. Banking every meow in a
counting semaphore would preserve noise rather than truth, replaying stale
sounds as phantom controller work.

The meow bus is therefore neither an interrupt-driven state machine nor
ordinary polling. It is polling that can be politely interrupted by a cat.

## Transaction lifecycle

`Execute()` serializes callers with the bus lock, creates a ref-counted
`Transaction`, posts it to a single-slot atomic mailbox, wakes the worker, and
waits for completion.

The worker never takes the caller's bus lock. It claims the transaction from the
mailbox, drives all attempts to a terminal result, writes that result into the
ticket, and releases the completion semaphore.

Results never live in caller-owned stack storage. If a caller times out or an
I/O request is canceled, the worker can finish safely because the ticket
remains alive until both references are released. A caller that times out tries
to reclaim an unclaimed mailbox entry; otherwise the worker retains ownership.

The caller checks `Transaction::IsDone()` after every semaphore wake so a stale
completion from an abandoned request cannot satisfy a newer transaction.

## Convergence loop

Each command attempt follows this sequence:

1. wait for required command/data inhibit bits to clear;
2. drain stale managed interrupt bits;
3. issue the command;
4. arm the condition-variable entry before polling;
5. snapshot, acknowledge, and accumulate managed status bits;
6. classify the accumulated evidence;
7. complete, retry, or wait for a wake pulse or timed recheck.

Arming before polling closes the lost-wakeup window. Accumulation preserves a
command-complete bit that arrives before transfer-complete, while allowing a
later error to outrank partial completion.

The worker uses a bounded per-attempt deadline and a lazy recheck interval. It
must never busy-spin the controller.

### Stale status

Bay Trail can retain completion bits from an earlier command. Before every
issue, the engine write-1-clears managed status, reads it back, and repeats until
stable-clear or the bounded drain fails.

Issuing while stale completion remains could turn the previous command's status
into an immediate false success. A failed drain therefore prevents command
issue and enters recovery.

### Command completion

Command constraints are attached to the transaction and remain stable for its
entire lifetime. They define:

- response type;
- data-line inhibit requirements;
- retry budget;
- timeout;
- OCR validation;
- reset requirements after failure.

Data commands require Transfer Complete. R1b commands require both command and
data lines to become available. An underlying error always outranks completion.

## Virtual controller state and recovery

The worker maintains a `VirtualControllerState` containing the command/data
inhibit state, card presence, and regulator readiness observed at attempt
boundaries. Retry policy uses this state to avoid replaying work when the media
or controller state no longer permits it.

Recovery is proportional to the state that may have been lost:

- retry without reset for filtered transient evidence such as garbage OCR;
- reset command/data lines for replay-safe protocol failures;
- power-cycle and fully re-identify after destructive state loss;
- never automatically replay a write whose completion is uncertain.

DMA memory remains owned until the controller is complete or has been reset and
quiesced.

## Host personalities

`HostPersonality` isolates silicon policy from the engine:

- `ValidateOcr()` rejects impossible card voltage responses.
- `OverrideReplyType()` handles host-specific response behavior.
- `PostResetInit()` applies reset-time host configuration.
- profile data defines fixed media, removable media, clocks, voltage behavior,
  DMA mode, and timing ceilings.

The generic personality is permissive. The Winky matcher selects explicit Bay
Trail eMMC and SD profiles from ACPI HIDs; `_UID` is not used to redefine a
known controller role.

## Data paths

### eMMC

The eMMC profile uses 32-bit ADMA2 with `IOSchedulerSimple`. Descriptor and
payload constraints come from the hardware contract. Identification and runtime
I/O both execute through the same transaction engine.

### Removable SD

The removable-SD profile uses one serialized request worker and a physically
contiguous SDMA staging buffer below 4 GiB. It does not expose staged operations
to `IOSchedulerSimple`.

Each queued request captures a media epoch. Removal, transfer failure, or
re-identification invalidates the epoch so stale requests cannot continue on a
replacement card. Partial writes use read-modify-write within the staging
window.

## Boot and hot-plug

`SdhciController::Boot()` runs synchronously during device-manager
initialization:

1. select the host profile;
2. map ACPI MMIO and interrupt resources;
3. apply the IOSF OCP correction before SDHCI MMIO;
4. initialize reset, power, clock, and the engine worker;
5. install the interrupt handler;
6. identify present media;
7. publish the disk.

The Winky BSP installs `sdhci_embedded`, `iosf_mbi`, and their boot links inside
`haiku.hpkg` because stage two exposes only that package before the kernel and
boot-media discovery.

Soldered eMMC does not create a hot-plug watcher. The removable-SD profile starts
a low-priority watcher only after initial publication. The watcher polls
Present State for future insertion or removal and performs complete
re-identification before returning media online.

## Thread model

| Context | Presence | Responsibility |
|---|---|---|
| device-manager init | transient | Resource mapping, boot sequence, publication |
| engine worker | every controller | Sole semantic MMIO owner |
| SDMA request worker | removable SD | Serialized staged block I/O |
| hot-plug watcher | removable SD | Future insertion/removal |
| ISR | every controller | Filter raw status and wake worker |

The eMMC ADMA2 path also uses Haiku scheduler threads.

## Invariants

1. The ISR filters but does not decide.
2. Interrupt wakes are lossy hints; completion delivery is counted.
3. The worker is the sole semantic and mutating SDHCI register owner.
4. Status is acknowledged per snapshot and accumulated in software.
5. Stale status is drained before issue.
6. The controller is never busy-spun.
7. Caller serialization and worker execution do not share a lock.
8. Results live in ref-counted tickets, not caller storage.
9. Command policy travels with the transaction.
10. Data commands require Transfer Complete.
11. DMA memory outlives active or unquiesced hardware.
12. Boot and runtime commands use the same execution path.

Host tests cover policy, matching, capacity decoding, transaction lifetime,
mailbox concurrency, staged-request planning, and IOSF encoding. MMIO timing and
electrical behavior require validation on Winky.
