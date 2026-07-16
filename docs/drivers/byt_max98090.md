<!--
SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
SPDX-License-Identifier: MIT
SPDX-FileContributor: Generated with GitHub Copilot
-->

# Bay Trail SST + MAX98090 audio

`byt_max98090` is the Winky audio-card add-on for the Samsung Chromebook 2
XE500C12. It binds two independently enumerated devices:

- Intel Bay Trail SST/LPE DSP, ACPI HID `80860F28`;
- MAX98090 codec, I2C ACPI HID `193C9890`.

The add-on publishes `audio/hmulti/byt_max98090/0` from the LPE half. A singleton
card object coordinates both halves, so either device may attach first. Opening
the multi_audio device returns `B_DEV_NOT_READY` until the codec is initialized
and the external SST firmware has completed its init handshake.

This is a device-manager `KernelAddon`, packaged directly at
`add-ons/kernel/drivers/audio/hmulti/byt_max98090`. It must not use Haiku's
legacy `drivers/bin` plus `drivers/dev/audio/hmulti` symlink layout; that path
loads the binary through devfs and requires the incompatible legacy driver API.
Its exported module names use the matching `drivers/audio/hmulti/byt_max98090`
prefix so the kernel module resolver can map each module back to this add-on.

## Current implementation

The driver provides a complete playback path from buffer exchange through the
Intel SST DSP to the MAX98090 speaker codec:

- enables Winky's PMC `PLT_CLK_0` as a forced-on 19.2 MHz clock;
- maps the Bay Trail IRAM, DRAM, SHIM, mailbox, and ACPI IMR resources;
- strictly parses the legacy `$SST` firmware container and bounds every module,
  block, destination offset, and 32-bit MMIO copy;
- writes the ACPI IMR physical base and BSS-reset feature into DSP DRAM before
  releasing runstall, matching the MRFLD post-download contract;
- resets and starts the DSP and waits up to five seconds for firmware
  init-complete IPC;
- preserves Linux's two-write/readback MRFLD start sequence: assert reset and
  runstall, read back, then enable snooping and release reset/runstall;
- configures MAX98090 as an I2S clock consumer for 48 kHz stereo 16-bit speaker
  output;
- reports a playback-only multi_audio description, fixed format, enabled
  channels, speaker volume/mute controls, and a physically contiguous 32-bit
  playback ring;
- accepts Haiku's mandatory enabled-channel writeback as a validated no-op
  when both fixed stereo channels remain enabled;
- starts the virtual-bus scheduler and configures SSP2 first, allocates the
  `media1_in` stream, and only then enables its dependent DAPM route, matching
  Linux's startup/hw_params/prepare ordering;
- sends the exact 10-command playback route configuration sequence:
  SBA virtual-bus start, SSP configure, SSP slot map, MMX media1 gain 0 dB,
  MMX SWM media1_in → media0_out, MMX enable media0 path, SBA enable pcm0
  input, SBA pcm0 input gain 0 dB, SBA SWM pcm0_in → codec_out0, and SBA
  codec_out0 gain 0 dB;
- latches a route-programming failure after its first diagnostic instead of
  retrying and flooding the kernel log on every buffer exchange;
- latches a stream-allocation failure until buffers are recreated or the DSP is
  reloaded, preventing exchange-driven allocation log floods;
- allocates the playback stream (100-byte MRFLD allocation, pipe 0x90, task 3,
  PCM 48 kHz stereo S16_LE, timestamp address 0xff34484c);
- starts, drops, and frees the DSP stream on demand;
- implements `B_MULTI_BUFFER_EXCHANGE` with period-elapsed polling, firmware
  timestamp reading, and Haiku HDA-compatible buffer cycle derivation;
- acquires the codec's two ACPI GPIO resources through the common GPIO service,
  subscribes to both edges with 200 ms debounce, and reads the initial levels;
- treats SCORE pin 14 as active-high headphone presence and pin 15 as
  active-low microphone presence;
- mutes the old MAX98090 path before switching `OUTPUT_ENABLE`, then unmutes
  the selected speaker or headphone path;
- exposes separate speaker/headphone volume and mute controls plus a
  GPIO-driven active-output mixer selector.

**IPC/period servicing is currently polling-based** with all host interrupts
masked. The driver polls SHIM registers whenever waiting for DSP responses or
period-elapsed notifications. IRQ-driven IPC handling is a future refinement.

Audible internal-speaker playback is validated on Winky hardware. The driver
does not publish capture channels.

## Hardware status

On 2026-07-16, Winky completed firmware boot, stream allocation, DSP route
configuration, stream start, period exchange, and audible playback through its
internal speakers. The successful allocation reply is the firmware's short
zero-result form with no mailbox body.

Headphone-jack detection and output switching are implemented but not yet
validated on Winky hardware. The path uses the Bay Trail SCORE community's
shared GSI 49 rather than MAX98090 IRQ 67. Insertion selects HPL/HPR and mutes
SPL/SPR; removal restores the internal speakers. Microphone presence is tracked
for future capture support, but the driver still publishes no input channels.

## MRFLD IPC protocol

The 64-bit SHIM envelope carries payload length in its low word and the IPC
category (`CMD=1` or `SET_PARAMS=2`), task/driver IDs, and
response/large/done/busy bits in its high word. A stream-command mailbox body
then begins with the packed 8-byte DSP header containing pipe and command IDs.
The polling engine serializes host requests and therefore reuses Linux's first
available private driver ID, `1`; asynchronous firmware events use driver ID
`0`.
Those DSP command IDs are allocate `0x02`, free `0x03`, pause `0x04`, resume
`0x05`, start `0x06`, drop `0x07`, drain `0x08`, and set parameters `0x12`;
allocate `0x20` and the other older generic IDs are not the Bay Trail protocol.
Control byte streams use the same MRFLD envelope but carry their complete
control command as the mailbox body.

Firmware init is an asynchronous driver-zero message, not a process-bit
message. Its SHIM envelope has driver ID `0`, a large mailbox payload, and
firmware task ID `3` on Winky. The mailbox begins with the 8-byte DSP header,
whose command is MRFLD firmware-init `0x01`. The canonical
`fw_sst_0f28.bin` reports a 38-byte success message, while Linux also defines
a 48-byte form with a 40-byte init body and its result field at message offset
44. Linux accepts the short form because its mailbox copy is zero-initialized;
this driver models both forms explicitly without reading beyond the advertised
message. It also treats asynchronous firmware-error command `0x11` as an
immediate boot failure. It validates both layers and acknowledges by clearing
busy, setting done, and returning a zero low-word status.

The fixed Winky control sequence begins with SBA virtual-bus start command 85,
then SSP command 117 and SSP slot-map command 130, before any route or gain
commands. The SSP command selects logical `SSP_CODEC` (`3`, mapped to physical
SSP2), switch state `3`, 16 bits per slot, two slots, provider mode, full
duplex, active TX/RX masks `0x03` with reserved map bytes `0xff`, 48 kHz enum
`3`, active-high frame sync, normal data polarity, 16-clock frame-sync width,
I2S protocol, and one-clock start delay. The following slot-map command has a
22-byte body, parameter ID 130, parameter length 18, selection 3, and identity
receive/transmit maps `{1, 2, 4, 8, 16, 32, 64, 128}`.

`media1_in -> media0_out` route and explicit 0 dB gain commands use MMX task 3.
`pcm0_in -> codec_out0`, SSP, and backend gains use SBA task 1. Defaults are
muted at -144 dB, so every required gain must be sent explicitly. Playback
allocation uses MRFLD command `0x02`, stream ID 1, pipe `0x90`, task 3, the
100-byte allocation body, timestamp offset `mailbox + 0x800 + 1 * 76`, and a
fragment size expressed in bytes. Linux defines `struct snd_sst_tstamp` as a
packed 76-byte structure. Buffer periods are constrained to multiples of 48
frames and the period count to even values. Within the allocation body, the
ring descriptors begin at byte 4, fragment size at byte 68, timestamp address
at byte 72, and the 24-byte PCM parameter union at byte 76. Winky's LPE-view
timestamp address for stream 1 is therefore `0xff34484c`.

The firmware may acknowledge a successful allocation with a short zero-result
IPC and no mailbox payload. This is a complete success response, not a malformed
allocation body; Linux's MRFLD reply path wakes the request with `data = NULL`,
and its allocation path accepts that result.

The ring allocation follows the firmware's 32-bit address fields and Linux's
device-DMA behavior. It may occupy any range wholly below 4 GiB; the firmware
download IMR at `0x20000000` is a separate reserved resource, not a PCM-ring
placement rule.

## Firmware boundary

The Intel firmware is separately licensed rather than MIT. The Winky BSP
vendors the canonical linux-firmware blob unchanged and installs it in
`haiku.hpkg` at:

```text
/boot/system/data/firmware/byt_max98090/fw_sst_0f28.bin
```

Linux's Bay Trail machine table selects that unsuffixed blob for ACPI codec
`193C9890`; the other linux-firmware `0f28` variants are not the Winky
MAX98090 contract.

The driver first checks the matching non-packaged path, allowing developers to
test another exact firmware image without rebuilding the system package. It
logs the path it loads and remains published but not ready when the file is
absent, invalid, targets memory outside the ACPI resources, or fails to
initialize.

Redistribution is governed by `LICENSES/LicenseRef-Intel-SST.txt`, which permits
unmodified binary redistribution while prohibiting reverse engineering,
decompilation, and disassembly. The Winky package installs that text as
`data/licenses/Intel (SST firmware)` and records the license and Intel copyright
in its metadata.

The canonical artifact is 701622 bytes with SHA-256
`5226ad60d7bf8f43f16508e13e3b4e8b53e9ab318b6825994bebeb4b96bfc64e`.
The driver does not require that size or hash; acceptance is based on bounded
`$SST` structure and destination validation.

The legacy Linux loader writes every loadable block through 32-bit MMIO. Jidō
Renga follows that access-width contract, including Linux's behavior of
omitting a final partial word; the canonical image has one DDR block with a
two-byte tail.

MrChromebox firmware is not inherently incompatible with this SST image. An
[archived Winky boot](https://raw.githubusercontent.com/linuxhw/Dmesg/master/Notebook/Google/Winky/Winky/A593940E635F/ENDLESS-3.6.3/5.0.0-25-GENERIC/X86_64/BA55726DB0)
using MrChromebox 4.10 reports the same LPE/IRAM/DRAM/mailbox/DDR layout and
successfully reaches SST firmware version `01.0c.00.01`. The Haiku timeout
therefore points first to host reset/copy/IPC mechanics, not a ChromeOS-only
firmware contract.

## Hardware contract

Winky uses SSP2 with the DSP providing BCLK and FSYNC. The backend format is
48 kHz stereo `S16_LE`, I2S one-bit delay, two 16-bit slots, and active slot
mask `0x3`. MAX98090 consumes both clocks. Speaker routing selects left DAC to
left speaker mixer and right DAC to right speaker mixer, with conservative
speaker volume 10 and mixer gain 3.

Codec setup uses the normal full-register path. For the 19.2 MHz MCLK,
`SYSTEM_CLOCK` register `0x1b` receives `PSCLK_DIV1` (`0x10`);
consumer-mode clock registers `0x1c` through `0x1e` are cleared; `MASTER_MODE`
`0x21` receives `0`; `INTERFACE_FORMAT` `0x22` receives only the I2S delay bit
(`0x04`, with 16-bit word size and normal polarities); TDM format/control
registers `0x24`/`0x23` are cleared; and `IO_CONFIGURATION` `0x25` enables only
SDIN (`0x01`). The driver does not write quick-system-clock register `0x04`,
quick-sample-rate register `0x05`, or capture-only SDOUT.

The fixed playback controls follow their ALSA value mappings rather than
writing user-facing numbers directly. Logical speaker volume 10 maps through
the ranged control's raw minimum 24 to `0x22` in registers `0x31` and `0x32`;
mute bits remain clear. Logical left/right mixer volume 3 is inverted over
0..3, so `SPK_CONTROL` `0x30` receives `0x00`. The speaker mixers receive
`0x01` and `0x02`, `FILTER_CONFIG` `0x26` receives Music mode plus playback DC
blocking (`0xa0`), and `OUTPUT_ENABLE` `0x3f` enables DACL, DACR, SPL, and SPR
((`0x33`). Headphone control remains on the direct-DAC path, registers
`0x2c`/`0x2d` start at raw volume `0x1a` with their mute bits set, and
`DEVICE_SHUTDOWN` `0x45 = 0x80` is written last. Jack insertion changes
`OUTPUT_ENABLE` to DACL, DACR, HPL, and HPR (`0xc3`) after muting the speakers.

The codec is ACPI child `\_SB.PCI0.I2C2`, HID `193C9890`, at 7-bit I2C address
`0x10` and 400 kHz. The captured boot maps that controller to Haiku I2C bus 1
(MMIO `0xfd8db000`, IRQ 33) and confirms the codec HID there. Codec `_CRS`
also describes level-triggered, active-low IRQ 67 and GPSC GPIO pins 14 and 15.
Haiku currently limits IO-APIC use to IRQ 63, so IRQ 67 is unusable and this
playback milestone intentionally neither requires nor installs it.

The codec and LPE `_CRS` tables both describe those GPIOs as
jack/microphone indices 0/1. The native GPIO service resolves the codec
resources to SCORE, where live Linux evidence identifies child IRQs for
`BYT-GPIO 14 hp` and `BYT-GPIO 15 mic` behind shared controller GSI 49. The
separate LPE IPC2HOST IRQ 29 is usable; the current playback implementation
records it but masks host interrupts and polls the IPC register, avoiding an
unhandled IRQ until stream IPC has a proper interrupt path.

Coreboot exposes a 2 MiB LPE BAR0, 4 KiB PCI-configuration BAR1, and 1 MiB
firmware/IMR BAR2. Winky reserves BAR2 at physical `0x20000000`; on C0 and
later silicon coreboot records that base and size at BAR0 offsets `0x144000`
and `0x144004`. The driver validates both BAR2 and this mailbox configuration.
The six level-triggered, active-low, exclusive IRQ resources are DMA0 24, DMA1
25, SSP0 26, SSP1 27, SSP2 28, and IPC2HOST 29, making the IPC interrupt
IRQ-resource index 5.

PMC `PLT_CLK_0` is register `0x60` in the 0x100-byte mapping selected by PCI
configuration register `0x44 & 0xfffffe00`. The driver preserves unrelated
bits, selects the 19.2 MHz PLL with bit 2, and writes control bits 1:0 as
`FORCE_ON` (`01b`), producing low bits `0x5`. This replaces coreboot's initial
25 MHz selection and leaves the clock forced on for the driver lifetime.

The ACPI `80860F28` function is SST/LPE, not HDA. It is distinct from Winky's
real PCI `00:1b.0` HDA controller (`8086:0f04`, class `040300`), which coreboot
configures for the HDMI codec `8086:2882`. The Winky BSP therefore packages
both drivers: `byt_max98090` owns the internal SST/MAX98090 path, while Haiku's
HDA driver remains available for potential HDMI audio.

The SST driver deliberately presents a small Haiku-native facade rather than
the Linux control graph: one fixed 48 kHz, 16-bit stereo endpoint, separate
speaker/headphone volume and mute controls, and an automatically selected
active-output route. DSP cells, switch matrices, SSP controls, and raw codec
register controls remain private implementation details; no UCM-style
userspace policy is required.

The captured Winky boot log confirms Haiku detects and reserves the
`8086:0f04` HDA controller independently, and separately enumerates the I2C
child HID `193C9890`. That boot recorded no active HDA codec, so it is runtime
topology evidence rather than a claim that HDMI playback is validated.

## Boot diagnostics

Bring-up tracing is controlled by `TRACE_BYT_MAX98090` in `Debug.h`; it is
currently enabled. Routine probe and lifecycle messages use `TRACE`, while
hardware and protocol failures use the always-on `ERROR` path.

Useful kernel log lines begin with `byt_max98090:`. A successful bring-up should
show:

1. PMC `PLT_CLK_0` enabled at 19.2 MHz;
2. LPE and IMR resources mapped, including IPC IRQ index 5;
3. external firmware path selected;
4. `SST firmware init-complete received`;
5. MAX98090 revision and fixed playback format;
6. jack GPIO initial state and 200 ms debounce;
7. `/dev/audio/hmulti/byt_max98090/0` published.

Missing firmware logs the required path. Firmware parser failures name the
failed validation class. An open before both halves are ready logs the readiness
failure and returns `B_DEV_NOT_READY`. Stream allocation and route configuration
failures are logged with the failing command name. A firmware-init timeout dumps
CSR, ISRX, IMRX, IPCX, and IPCD so the next boot distinguishes a DSP that never
ran from one that ran but failed to post init-complete.

With tracing enabled, allocation logs the virtual-bus/SSP lifecycle state, pipe,
task, ring address and sizes, timestamp address, and the exact 100-byte request.
A failed DSP reply includes the complete raw 64-bit IPCD value. Allocation
failures are then latched until buffers are recreated or the DSP is reloaded.
Every failed buffer exchange also sleeps for 100 ms after releasing driver
locks. Haiku's `MultiAudioNode` ignores the exchange return status and
immediately retries, so a driver that returns a persistent error without
blocking otherwise creates an unbounded userspace/kernel ioctl spin.

Firmware reply result `1` means `SST_ERR_INVALID_STREAM_ID`. In the first
media-stack exercise, `media0_out` produced that result because route setup ran
before the allocation that creates `media1_in`. Playback startup now preserves
Linux's ordering, and any remaining route failure is reported once and latched
rather than retried on every buffer exchange.
