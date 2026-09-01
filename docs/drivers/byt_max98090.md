<!--
SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
SPDX-License-Identifier: MIT
SPDX-FileContributor: Generated with GitHub Copilot
-->

# Bay Trail SST + MAX98090 audio

`byt_max98090` is the Haiku audio-card driver for Winky's Intel Bay Trail
SST/LPE DSP and MAX98090 codec:

- LPE ACPI HID `80860F28`;
- codec ACPI HID `193C9890`;
- codec I2C address `0x10`; and
- 48 kHz stereo `S16_LE` playback and capture.

The driver publishes:

```text
/dev/audio/hmulti/byt_max98090/0
```

It is a device-manager `KernelAddon` installed at
`add-ons/kernel/drivers/audio/hmulti/byt_max98090`. Its module names use the
same `drivers/audio/hmulti/byt_max98090` prefix.

## Device model

ACPI enumerates the LPE and codec independently. A singleton card coordinates
both nodes, so attachment order does not matter. The multi_audio device is
published from the LPE node and returns `B_DEV_NOT_READY` until both the codec
and DSP firmware are initialized.

The driver exposes a focused Haiku interface rather than the Linux control
graph:

- fixed stereo playback and capture;
- independent contiguous DMA rings with shared period geometry;
- speaker and headphone volume and mute controls;
- GPIO-selected output and microphone sources; and
- `B_MULTI_BUFFER_EXCHANGE` position and period reporting.

DSP cells, switch matrices, SSP controls, and raw codec registers remain
private.

## Winky hardware contract

Winky routes internal audio through SSP2. The DSP provides BCLK and FSYNC;
MAX98090 is the I2S clock consumer. The format is 48 kHz stereo, 16 bits per
slot, two slots, one-bit I2S delay, and active slot mask `0x3`.

Coreboot leaves physical `PLT_CLK0` at 25 MHz. The driver preserves that parent
selection and changes only control bits 1:0 to `FORCE_ON`. MAX98090 uses
`PSCLK_DIV1`; consumer-mode clock registers are cleared.

The codec is `\_SB.PCI0.I2C2` at 400 kHz. Its ACPI resources describe headphone
and microphone GPIOs 14 and 15 in the Bay Trail SCORE community. They share
controller GSI 49. The MAX98090 IRQ is 67, outside Haiku's usable IO-APIC range,
so jack detection uses the GPIO service instead.

The LPE resources include:

- 2 MiB BAR0 for IRAM, DRAM, SHIM, and mailbox windows;
- 4 KiB BAR1 for PCI configuration;
- 1 MiB BAR2/IMR at physical `0x20000000`; and
- DMA0, DMA1, SSP0, SSP1, SSP2, and IPC2HOST IRQs 24 through 29.

The internal SST/MAX98090 path is distinct from PCI HDA `8086:0f04`, which
remains available to Haiku's HDA driver for HDMI audio.

## Firmware

The Winky BSP installs the unmodified Intel SST image at:

```text
/boot/system/data/firmware/byt_max98090/fw_sst_0f28.bin
```

The driver checks the matching non-packaged path first. It accepts firmware by
strictly validating the `$SST` container, module and block bounds, destination
ranges, and 32-bit MMIO copy operations. A missing or invalid image leaves the
device published but not ready.

The image is separately licensed under
`LICENSES/LicenseRef-Intel-SST.txt`. Package metadata installs that license as
`data/licenses/Intel (SST firmware)`.

Firmware loading:

1. asserts DSP reset and runstall;
2. copies validated IRAM, DRAM, and DDR blocks;
3. writes the IMR base and BSS-reset feature to DSP DRAM;
4. enables snooping and releases reset/runstall with posted readback; and
5. waits up to five seconds for firmware-init IPC.

The canonical image contains a final two-byte DDR tail. The loader follows the
legacy 32-bit access contract and does not synthesize a partial MMIO word.

## MRFLD IPC

The 64-bit SHIM envelope carries payload length in its low word. Its high word
contains category, task, driver ID, and response/large/done/busy flags. Stream
mailboxes begin with an 8-byte packed DSP header.

Host requests are serialized and use driver ID 1. Asynchronous firmware events
use driver ID 0. Relevant stream commands are:

| Command | ID |
|---|---:|
| allocate | `0x02` |
| free | `0x03` |
| pause | `0x04` |
| resume | `0x05` |
| start | `0x06` |
| drop | `0x07` |
| drain | `0x08` |
| set parameters | `0x12` |

Firmware init is an asynchronous driver-zero MRFLD command `0x01`, not a
process-bit message. Both the 38-byte Winky response and the defined 48-byte
form are parsed without reading beyond the advertised payload. Firmware-error
command `0x11` fails initialization immediately.

A successful stream allocation may return a short zero-result IPC with no
mailbox body. This is a complete success response.

IPC and period servicing are polling-based and host interrupts remain masked.
Polling is bounded, and persistent allocation or routing failures are latched
instead of retried on every buffer exchange.

## Playback

Playback uses stream ID 1, pipe `0x90`, task 3, and timestamp address
`0xff34484c`. Its 100-byte allocation body contains the ring descriptors,
fragment size, timestamp address, and PCM parameters. Periods are multiples of
48 frames and the period count is even.

Startup orders dependencies as the DSP requires:

1. start the SBA virtual bus;
2. configure SSP2 and its slot map;
3. allocate `media1_in`;
4. route `media1_in` to `media0_out`;
5. enable `pcm0_in`;
6. route `pcm0_in` to `codec_out0`; and
7. apply explicit 0 dB gains to each active path.

Firmware defaults are muted at -144 dB, so all required gains are programmed
explicitly. Buffer exchange derives playback position from the SSP hardware
counter and firmware period notifications.

Speaker routing sends the left and right DACs to their matching speaker mixers.
Headphone insertion mutes the speakers, switches `OUTPUT_ENABLE` to HPL/HPR,
and then unmutes the headphone path. Removal performs the inverse sequence.

## Capture

Winky's camera has no USB Audio interface. Microphones belong to the
MAX98090/SST card:

- the internal digital pair uses DMICL, codec SDOUT, SSP2 receive slots,
  `codec_in0`, and `pcm1_out`;
- a headset microphone uses MIC2/IN34 and both ADC paths before the same SST
  route.

Capture uses stream ID 3, pipe `0x0e`, task 3, and timestamp address
`0xff3448e4`. The DSP route sets the SSP slot map, zeroes `codec_in0` DCR
parameters, applies 0 dB gain, connects `codec_in0` to `pcm1_out`, and enables
the destination.

The internal codec image uses DMIC divisor 8,
`DIGITAL_MIC_ENABLE = 0x53`, `DIGITAL_MIC_CONFIG = 0x60`, record DC blocking,
and raw ADC-biquad attenuation `0x0f`. Headset selection disables both sources
before enabling MIC2, MICBIAS, and the ADCs.

Input enables are latched through MAX98090's shutdown sequencer: assert
`DEVICE_SHUTDOWN` for 40 ms, release it, and verify the complete capture image
by readback.

Buffer exchange reports capture position from bytes committed to the DDR ring.

## Controls and GPIO

SCORE pin 14 is active-high headphone presence. Pin 15 is active-low headset
microphone presence. The driver subscribes to both edges with 200 ms debounce
and reads initial levels before publishing the ready card.

Speaker volume defaults to logical 10 (-14 dB), is capped at logical 20 (0 dB),
and uses a fixed logical mixer level 2 (-6 dB). Headphone volume defaults to and
is capped at logical 19 (-9 dB).

The normal codec path programs the complete clock, interface, filter, routing,
volume, and output-enable image. Configuration writes that define the active
contract are read back before use.

## Speaker equalization

The Winky profile contains a seven-band MAX98090 speaker EQ encoded as Q4.20
Direct Form I coefficients. The filter graph uses the ChromeOS frequencies and
gains, with CRAS's high-pass calculation for its zero-resonance input. Poles
remain inside the unit circle and the quantized response stays below 0 dB.

Each 15-byte band is written in one I2C transaction while the codec is shut
down, then all coefficients are read back. The path uses 4 dB EQ
preattenuation.

The stored DRC parameters are not enabled. The active speaker contract is the
direct-sign seven-band EQ, 4 dB preattenuation, 0 dB speaker-volume ceiling,
and -6 dB speaker mixer.

## Test utilities

The Winky image includes:

```sh
jr_mic_probe /boot/home/microphone.wav 5
jr_mic_collect /boot/home/artifacts 5
```

`jr_mic_probe` requires stereo 48 kHz `S16_LE`, writes a WAV, and reports
callbacks, bytes, frames, peak, RMS, and nonzero-sample percentage. It succeeds
only after receiving nonzero samples without a full-scale rail.

`jr_mic_collect` stores the WAV, console output, exit status, and before/after
syslogs in a timestamped directory and zip.

## Diagnostics

Kernel messages begin with `byt_max98090:`. A normal initialization reports:

1. the preserved and forced-on PMC clock;
2. mapped LPE and IMR resources;
3. the selected firmware path and init-complete IPC;
4. MAX98090 revision and fixed format;
5. initial jack GPIO state; and
6. the published multi_audio path.

Firmware timeout diagnostics include CSR, ISRX, IMRX, IPCX, and IPCD. Stream
errors include command, pipe, task, ring geometry, timestamp address, and the
raw firmware result. A failed buffer exchange sleeps after releasing driver
locks because Haiku's `MultiAudioNode` otherwise retries immediately.

## Supported behavior and limitations

The Winky profile supports:

- internal-speaker playback;
- headphone detection, switching, playback, volume, and mute;
- internal digital-microphone capture;
- fixed stereo period exchange; and
- automatic microphone-source selection.

Analog headset-microphone capture is not qualified. IPC and period notification
remain polling-based. The driver has one Winky profile and does not match other
Bay Trail/MAX98090 boards.
