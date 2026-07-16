<!--
SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
SPDX-License-Identifier: MIT
SPDX-FileContributor: Generated with GitHub Copilot
-->

# Intel Bay Trail SST firmware

`fw_sst_0f28.bin` is the unmodified Intel SST firmware distributed by the
linux-firmware project:

- source: <https://git.kernel.org/pub/scm/linux/kernel/git/firmware/linux-firmware.git/plain/intel/fw_sst_0f28.bin>
- SHA-256: `5226ad60d7bf8f43f16508e13e3b4e8b53e9ab318b6825994bebeb4b96bfc64e`
- size: 701622 bytes
- license: [`LicenseRef-Intel-SST`](../../LICENSES/LicenseRef-Intel-SST.txt)

The Winky BSP installs the blob unchanged at
`data/firmware/byt_max98090/fw_sst_0f28.bin` inside `haiku.hpkg`.

Linux's Bay Trail ACPI machine table selects this unsuffixed linux-firmware
file for codec ID `193C9890`; the other `0f28` variants are not used by the
Winky MAX98090 machine entry.
