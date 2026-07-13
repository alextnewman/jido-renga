# Winky Bay Trail SDHCI Hardware Contract

This is the living hardware and platform contract for the `winky` BSP. It
describes what the composed image and `sdhci_embedded` driver must assume,
provide, verify, and publish for the Intel Bay Trail storage controllers in the
Samsung Chromebook 2 (Winky).

The terms **must**, **should**, and **may** are normative. Where code, firmware,
and this document disagree, the discrepancy must be resolved explicitly; the
driver must not silently weaken this contract to preserve boot.

## 1. Scope and ownership

The Winky BSP owns the platform's SDHCI implementation.

- The supported Haiku targets are `x86` and `x86_64`.
- The composed image must omit Haiku's stock `sdhci` add-on and boot link.
- `sdhci_embedded` is the sole owner of the supported Bay Trail SDHCI ACPI
  devices.
- Runtime add-ons must use canonical Haiku paths. No shipped binary may depend
  on the overlay layout or identify itself as an overlay.
- This contract is not a generic promise for every SDHCI controller. A new
  platform must add an explicit Jido Renga BSP and hardware profile.

## 2. Platform identity and topology

| ACPI HID | Role | Media | Bus width target | Hot-plug |
|---|---|---|---:|---|
| `80860F14` | SCC eMMC host | Soldered eMMC | 8-bit | No |
| `80860F16` | SCC SD host | Removable SD | 4-bit | Yes |
| `80860F15` | SCC SDIO host | Unsupported | - | - |

The following matching rules apply:

- The node must be an ACPI device compatible with `PNP0D40`.
- `_HID` determines the controller role.
- `_UID` is an instance identifier and may be logged, but must not redefine the
  role of a known HID.
- MMIO and interrupt resources come from the ACPI `_CRS` of the matched node.
- Each supported controller is assumed to have a dedicated interrupt line. The
  eMMC controller has been observed on IRQ 44.

The SDHCI controller is described by ACPI rather than by its PCI function. PCI
is still required for the IOSF sideband transaction router described below.

## 3. Early-boot dependency order

These controllers may host the boot volume. Their complete dependency chain
must therefore be available in the system package and boot-module set:

```text
sdhci_embedded
  -> iosf_mbi
     -> Haiku device_manager
     -> Haiku PCI bus manager
     -> Haiku x86 PCI controller driver
```

On the current captive Haiku image, device_manager scans `bus_managers` before
`busses/pci`. ACPI may consequently initialize an SDHCI boot host before the
normal root scan has registered the x86 PCI controller and PCI domain.

`iosf_mbi` must resolve this ordering through formal module dependencies and
Haiku's normal device-manager registration path:

1. Probe PCI configuration address `0:0.0` through `pci_module_info`.
2. If no PCI domain exists, invoke the dependency-provided x86 PCI driver's
   `register_device()` hook on the device-manager root.
3. Allow that normal driver path to create the PCI root, add the controller
   domain, and enumerate PCI.
4. Re-probe `0:0.0` through the PCI bus manager.

The overlay must not patch Haiku, issue raw CF8/CFC cycles, or carry a private
PCI configuration implementation.

## 4. IOSF-MBI transaction router

The IOSF-MBI registers live only in PCI function `0:0.0`.

| Platform | Vendor:device | Function |
|---|---|---|
| Bay Trail | `8086:0f00` | SoC transaction router |
| Cherry Trail | `8086:2280` | SoC transaction router |

The PMC and SDHCI functions are not valid IOSF-MBI endpoints.

### 4.1 Mailbox registers

| PCI config offset | Name | Purpose |
|---:|---|---|
| `0xd0` | MCR | Opcode, unit, low register byte, byte enables |
| `0xd4` | MDR | Transaction data |
| `0xd8` | MCRX | Upper register-address bits |

MCR is encoded as:

```text
[31:24] opcode
[23:16] unit/port
[15:8]  register offset [7:0]
[7:0]   0xf0 byte-enable pattern
```

MCRX carries `register_offset & 0xffffff00`.

Read ordering must be:

```text
write MCRX -> write MCR -> read MDR
```

Write ordering must be:

```text
write MDR -> write MCRX -> write MCR
```

MCRX must be written for every transaction, including zero, so an extended
address cannot leak into a later low-offset access. The complete handshake must
be serialized across all consumers.

### 4.2 Mandatory SCC OCP correction

Before the driver reads or writes any SDHCI register, it must correct the SCC
over-current-protection timeout:

| Field | Value |
|---|---:|
| IOSF unit | `0x63` (SCCEP) |
| Register | `0x1078` |
| Timeout field | bits `[10:8]`, mask `0x00000700` |
| Read opcode | `0x06` |
| Write opcode | `0x07` |

The required sequence is:

1. Read the register.
2. Clear bits `[10:8]` while preserving every other bit.
3. Write the modified value.
4. Read the register again.
5. Fail controller initialization unless the field reads back as zero.

IOSF-MBI is a required BSP facility, not a best-effort optimization.

## 5. SDHCI host characteristics

The known eMMC controller capability signature is:

```text
CAPABILITIES   = 0x446cc801
CAPABILITIES_1 = 0x00000807
```

This signature describes a 200 MHz base clock and advertises the facilities
needed for the Winky target, including 32-bit ADMA2, high-speed operation,
8-bit width, 1.8 V signaling, SDR50, SDR104, and DDR50.

For this signature:

- The capability timeout field reports 1 kHz but must be overridden to 1 MHz by
  the eMMC controller profile.
- Preset-value mode is broken and must remain disabled.
- The controller must start in SDMA, one-bit, legacy timing at 400 kHz.
- Clock dividers must never produce a clock above the requested frequency.
- After programming a clock, the driver must enable the internal clock, wait for
  stability, enable the Bay Trail PLL stage, wait for stability again, and only
  then enable SDCLK.
- The driver must not claim HS400 without a separately validated host and board
  contract. Winky's current eMMC ceiling is HS200.

Capability values for the removable SD controller must be logged and added to
this document before they are used as a new fixed signature.

## 6. Power and signal voltage

### 6.1 Common bring-up

After a full host reset:

1. Configure interrupt status latching and the worker's signal mask.
2. Select a supported bus voltage.
3. On SDHCI 4.10+ hosts, wait for the bit-25 regulator-stable indication and
   fail if it does not assert within the bounded poll. Bay Trail predates that
   register contract; no reserved Present State bit may be treated as fatal.
4. Observe the 10 ms Bay Trail post-power settle interval.
5. Force SDMA, one-bit, legacy timing.
6. Set the identification clock to 400 kHz using the internal-clock/PLL/SDCLK
   sequence above, and fail if either stability check times out.
7. Program a bounded data-timeout period.

No reset, power, clock, or command loop may wait forever.

### 6.2 eMMC rail

The Winky eMMC rail is fixed at 1.8 V.

- Card-detect is not meaningful; the host must treat the device as present and
  force bus power on.
- The dedicated eMMC reset signal must be asserted for at least 9 microseconds,
  deasserted, and followed by the device reset-settle interval.
- A board DSM voltage mux is not required to select the fixed 1.8 V rail.

### 6.3 Removable SD rail and DSM

The removable slot must start at its advertised 3.3 V or 3.0 V power level.
UHS operation is permitted only when both the host and the platform advertise
it.

The Intel SDHCI DSM uses GUID:

```text
f6c13ea5-65cd-461f-ab7a-29f7e8d5bd61
```

| Function | Meaning |
|---:|---|
| 0 | Supported-function bitmap |
| 3 | Switch platform signaling to 1.8 V |
| 4 | Switch platform signaling to 3.3 V |
| 8 | Permitted UHS mode mask |

The UHS mask is interpreted as:

| Bit | Permitted mode |
|---:|---|
| 0 | SDR25 |
| 1 | DDR50 |
| 2 | SDR50 |
| 3 | SDR104 |

If DSM discovery is absent, the removable slot must not attempt UHS. It may
still negotiate 3.3 V SD high-speed mode.

For CMD11 voltage switching, DAT[3:0] must be low before the switch and high
afterward. The host must gate SDCLK, change Host Control 2, invoke the platform
rail switch, settle, re-enable SDCLK, and verify the data lines. Failure after
CMD11 requires a full power cycle and fresh card identification; continuing in
the old protocol state is invalid.

## 7. Card protocol and performance targets

All width and timing changes must be made card first, host second.

### 7.1 eMMC identification

The required sequence is:

1. Host reset, power, 400 kHz, one-bit legacy timing.
2. Hardware eMMC reset.
3. CMD0.
4. CMD1 with argument zero to read the card OCR.
5. Intersect card OCR with host-supported voltage windows.
6. Repeat CMD1 with the selected voltage and sector-mode bit until ready.
7. CMD2, host-assigned RCA 1 through CMD3, CMD9, then CMD7 with a plain R1
   response, matching Linux's MMC core.
8. Read the mandatory 512-byte EXT_CSD with CMD8.
9. Require non-zero EXT_CSD `SEC_COUNT`; publish 512-byte logical sectors.

Relevant EXT_CSD fields are:

| Offset | Field |
|---:|---|
| 32 | Flush cache |
| 33 | Cache control |
| 183 | Bus width |
| 185 | HS timing |
| 187 | Power class |
| 192 | EXT_CSD revision |
| 196 | Card type |
| 200 | HS52 1.8 V power class |
| 201 | HS26 1.8 V power class |
| 212-215 | Sector count |
| 236 | HS200 1.8 V power class |
| 238 | DDR52 1.8 V power class |
| 248 | Generic CMD6 time |
| 249-252 | Cache size |

The preferred operating-mode ladder is:

1. HS200, 8-bit, 1.8 V, up to 200 MHz.
2. DDR52, 8-bit, 1.8 V.
3. HS52, 8-bit.
4. HS26, 8-bit.
5. Legacy, 8-bit.

HS200 must enter at a safe intermediate clock, switch to full speed, then tune
with CMD21 using a 128-byte block. Tuning permits at most 40 attempts and
succeeds only when Execute Tuning clears and Tuned Clock is set.

If the card advertises a cache, the driver should enable it. A successful
`B_FLUSH_DRIVE_CACHE` request must issue the EXT_CSD cache flush.

### 7.2 SD identification

The required sequence is:

1. CMD0 and the post-reset settle interval.
2. CMD8 when probing an SD v2 card.
3. CMD55/ACMD41 argument-zero OCR probe.
4. Select an overlapping OCR window and request high capacity as applicable.
5. Request 1.8 V only when host, platform, and card can support UHS.
6. CMD11 and the verified signal-voltage sequence when accepted.
7. CMD2, CMD3, CMD9, CMD7 with a plain R1 response.
8. ACMD51 to read SCR.
9. ACMD6 to select four-bit width when supported.
10. CMD6 to query and select timing modes.
11. CMD19 tuning for SDR104 and for SDR50 when the host requires it.

The preferred mode ladder is:

1. SDR104.
2. DDR50.
3. SDR50.
4. SDR25.
5. SDR12.
6. 3.3 V high-speed.
7. Default speed.

At 1.8 V, SDR12 is the safe floor. If the platform cannot provide UHS, the
driver must remain in the 3.3 V ladder.

## 8. DMA contract

32-bit ADMA2 is the Winky performance target. Until ADMA2 command completion
and payload integrity have both been confirmed on hardware, the boot-critical
removable SD profile uses SDMA through a dedicated, contiguous 512 KiB staging
buffer below 4 GiB. Hardware DMA never targets scheduler-owned translated
buffers directly. The SDMA profile also bypasses `IOSchedulerSimple`: one
overlay-owned request worker copies between the `IORequest` and whole-sector
staging windows, advances the request only after successful media transfer, and
reports the real error exactly once. Partial writes use read-modify-write.

Every queued request captures an atomic media epoch. Removal, transfer failure,
or recovery invalidates that epoch; old requests cannot continue on a
re-identified or replacement card. Recovery invalidates the epoch before
waiting on the SDMA media-I/O gate, so re-identification cannot race an in-flight
request. The non-boot eMMC profile remains the ADMA2 and
`IOSchedulerSimple` validation path; production composition must not claim
ADMA2 for either controller without metal validation.

The descriptor format is exactly:

```text
uint16 attributes
uint16 length
uint32 address
```

| Descriptor | Attributes |
|---|---:|
| Ordinary transfer | `0x0021` |
| Final transfer | `0x0023` |

Additional requirements:

- A descriptor is 8 bytes.
- The final transfer descriptor itself carries END; no trailing END NOP is
  used.
- A zero length encodes 65,536 bytes.
- Data addresses are four-byte aligned and below 4 GiB.
- The descriptor table is physically contiguous, at least 64-byte aligned, and
  entirely below 4 GiB.
- The table's physical address must be retained; a virtual-address fallback is
  forbidden.
- The controller's capability halves must be read as independent 32-bit MMIO
  registers.
- In 32-bit ADMA mode, only the low address register at offset `0x58` is
  written. Offset `0x5c` is reserved for advertised 64-bit ADMA support.
- Descriptor writes must be published with a memory barrier before command
  issue.
- One operation is limited to 512 KiB, 128 descriptors, 65,536 bytes per
  descriptor, and 65,535 blocks.
- A data command completes on Transfer Complete, never Command Complete alone.
- ADMA faults must report the ADMA error state, current descriptor pointer,
  table base, and descriptors through END.

Haiku's DMA resource layer may bounce payloads to satisfy alignment and address
limits. Caller memory must remain pinned until the worker has observed
completion or has reset and quiesced the controller.

## 9. Interrupt and worker contract

Bay Trail interrupt delivery is not authoritative.

- One worker is the sole semantic and mutating SDHCI register owner.
- The ISR may read raw interrupt status only to reject zero, all-ones, and
  sources outside the driver's signal mask. It must not acknowledge or interpret
  command state.
- A plausible ISR source emits only a lossy wake hint. If that wakes the worker,
  the ISR should request prompt scheduling so a level-triggered source cannot
  starve its owner.
- The worker must snapshot status, write-1-clear each managed snapshot, retain
  the evidence in an attempt-local software accumulator, and determine truth
  from the accumulated state.
- An interrupt may be early, late, duplicated, spurious, associated with an old
  command, or absent.
- In-command polling uses a lazy 2 ms timed recheck; idle dispatch uses a
  roughly 100 ms backstop.
- The driver must never busy-spin the controller.
- The worker runs at normal priority; an interrupt defect must not monopolize
  early boot.
- Only command completion, transfer completion, tuning-buffer readiness, and
  managed error sources may be status/signal-enabled. Card insertion/removal
  events are excluded because hot-plug is determined from Present State.
- Interrupt status is drained before every command so stale completion bits
  cannot become false success.
- A short-busy response requires both command and data lines idle before issue,
  even when the command has no payload.
- Any underlying error outranks completion when both are latched.
- Data commands require Transfer Complete.
- The idle worker must clear any late managed completion/error source.
- R1b busy may be polled for up to five seconds.

There is no caller-side timeout that may release active DMA memory. Per-attempt
deadlines and retry decisions belong to the worker.

## 10. Recovery and replay safety

Recovery is selected by what state may have been lost:

- A transient inhibit or replay-safe command/data fault may reset the command
  and data lines, then retry within budget.
- Garbage OCR may retry without reset.
- A destructive power cycle invalidates RCA, timing, width, voltage, and card
  selection. It must be followed by complete card re-identification.
- A write whose completion is uncertain must never be replayed automatically.
- If line reset cannot prove the data path quiescent, the driver must remove
  power before releasing DMA memory.
- Retry backoff must be bounded and gentle rather than hammering the bus.

## 11. Disk-device contract

- Logical sectors are 512 bytes.
- Published capacity must come from validated card data.
- Every request must be bounds checked without integer wrap.
- Immediate request rejection must notify the Haiku I/O request.
- The SDMA path owns one serialized request queue, pins virtual request memory
  until notification, and does not expose an operation-level failure to
  `IOSchedulerSimple`.
- Geometry must be produced with Haiku's geometry helper rather than truncating
  capacity into a legacy field.
- Soldered eMMC is always non-removable.
- The SD slot publishes an offline disk when empty, transitions offline on
  removal, and re-identifies before returning online after insertion.
- Scheduler and DMA resources must be destroyed before the controller worker and
  MMIO mapping are torn down.

## 12. Validation ledger

This table prevents implementation status from being mistaken for hardware
proof.

| Contract area | Current status |
|---|---|
| Winky-exclusive package composition | Built and inspected |
| Stage-two visibility and ACPI driver binding | Confirmed on Winky |
| ACPI resource mapping | Confirmed; eMMC observed on IRQ 44 |
| Formal `sdhci_embedded -> iosf_mbi` dependency | Built and ELF-inspected |
| Formal IOSF PCI/device-manager dependency | Built and ELF-inspected |
| Early x86 PCI registration before ACPI boot media | Confirmed on Winky |
| IOSF host bridge access and OCP readback | Confirmed on Winky; OCP timeout field observed already clear |
| Host reset, power, and card identification | Confirmed for both controllers: SD identifies, negotiates 4-bit high-speed at 50 MHz, discovers partitions, and boots; eMMC completes identification and publishes a writable disk |
| Filtered ISR and snapshot-acknowledged convergence | Confirmed far enough on Winky to identify SD, negotiate high speed, and publish the disk |
| ADMA2 descriptor policy | Host-tested |
| Removable-SD `IOSchedulerSimple` path | Rejected: KDL showed `R15 = 0xdeadbeefdeadbeef` at `owner->operations.RemoveHead()`, proving a freed `RequestOwner` after a partial-operation error |
| Staged removable-SD SDMA request worker | Confirmed on Winky through partition discovery and boot after correcting the `IORequest::CopyData()` direction |
| 32-bit capability and ADMA MMIO access widths | Confirmed by Winky capability logs and cross-compiled object inspection |
| eMMC CMD7 response | Linux-aligned plain R1 selection confirmed by successful eMMC identification and sustained block I/O; the former `0xef924000` stale-CMD9 failure is gone |
| eMMC ADMA2 data transfer | Confirmed on Winky by installing Haiku onto the eMMC through the ADMA2 path; sustained performance was reported as fast |
| eMMC HS200/DDR52/HS52/HS26 ladder | Negotiation completes and supports the validated installation workload; the exact selected rung was not captured in the milestone log |
| SD UHS/high-speed ladder | High-speed 4-bit 50 MHz confirmed on Winky; UHS remains disabled because Intel DSM is unavailable |
| Removable-media recovery | Implemented; awaiting hardware validation |
| Winky I2C input composition | Confirmed on Winky: the guarded I2C manager, `i2c_atmel_mxt`, stock `i2c_elan`, and the ChromeOS EC keyboard coexist and the input devices work |

The hardware-validated milestone artifact is:

```text
generated.x86_64/haiku-nightly-anyboot.iso
SHA-256 556e56770597630955a08884930d53e8edc1b2609de26916f8ba24fb8a2744fb
```

Its `haiku.hpkg` checksum is
`0ab25cd782afde011309f9af6219e4cf64a34b0f1851a904ca7fd5c1bcdc6ce7`.
The staged `sdhci_embedded` is byte-identical to the inspected build output
(`cb991c9cc26b23ad6785581cd836c132896d20beaed37586834b5878bdcc1aa8`);
the package contains its boot link and no stock `sdhci` add-on. The canonical
`bus_managers/i2c` package entry is byte-identical to the guarded overlay build
(`0f864ba271cc49f4c22a2a2cf0604b7b59188b84f3c5f1e220a2b1fb869d5934`).
The input directory contains both `i2c_atmel_mxt` and stock `i2c_elan`.

### 12.1 July 2026 Winky milestone

The artifact above crossed the line from bring-up image to usable system:

- Haiku boots from the removable SD path backed by the serialized SDMA worker.
- The soldered eMMC identifies successfully after the Linux-aligned CMD7 fix.
- A complete Haiku installation was written to eMMC through ADMA2, with
  subjectively high throughput and no storage fault.
- The composed I2C/input stack initializes and the Winky keyboard and touchpad
  work.

This is the first end-to-end proof of the BSP contract: composition, boot-media
discovery, both embedded storage hosts, DMA block I/O, and machine-specific
input all operate together in one image.

## 13. Extending the contract

A new BSP must declare its own:

- image ownership and stock-driver omissions;
- ACPI or bus identities;
- host bridge and sideband prerequisites;
- voltage rails and platform switching method;
- controller capability signatures and quirks;
- card roles, widths, mode ceilings, and fallback ladders;
- DMA address and descriptor limits;
- interrupt-sharing assumptions;
- hardware validation ledger.

Do not broaden an existing matcher merely because a controller looks similar.
Explicit platform contracts are the mechanism by which Jido Renga remains
predictable.
