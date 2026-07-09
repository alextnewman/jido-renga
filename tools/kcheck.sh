#!/bin/sh
# SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
# SPDX-License-Identifier: MIT
# SPDX-FileContributor: Generated with GitHub Copilot
#
# Syntax-check a kernel add-on translation unit against the real Haiku private
# kernel headers, using the captive submodule's cross-toolchain. This is NOT a
# full jam build (no link, no generated attribute headers) -- it is a fast
# -fsyntax-only smoke test to catch kernel-API misuse in code that otherwise
# can only be reasoned about on this host. Usage: tools/kcheck.sh <file.cpp> ...
set -e

HAIKU="$(cd "$(dirname "$0")/../haiku" && pwd)"
CXX="$(cd "$(dirname "$0")/../.." && pwd)/generated/cross-tools-x86_64/bin/x86_64-unknown-haiku-g++"
H="$HAIKU/headers"
OVERLAY="$(cd "$(dirname "$0")/../overlay" && pwd)"

exec "$CXX" -std=c++17 -fno-rtti -fno-exceptions -fsyntax-only \
	-D_KERNEL_MODE \
	-I"$OVERLAY/headers" \
	-I"$H/os" -I"$H/os/drivers" -I"$H/os/kernel" -I"$H/os/support" \
	-I"$H/os/storage" -I"$H/os/device" \
	-I"$HAIKU/src/add-ons/kernel/bus_managers/acpi/acpica/include" \
	-I"$HAIKU/src/add-ons/kernel/bus_managers/acpi/acpica/include/platform" \
	-I"$H/private" -I"$H/private/kernel" -I"$H/private/drivers" \
	-I"$H/private/shared" -I"$H/private/system" \
	-I"$H/private/kernel/arch/x86" -I"$H/private/system/arch/x86_64" \
	-I"$H" -I"$H/posix" \
	-I"$HAIKU/build/config_headers" \
	-I"$HAIKU/src/system/kernel/device_manager" \
	-I"$HAIKU/headers/private/kernel/boot/platform/efi" \
	"$@"
