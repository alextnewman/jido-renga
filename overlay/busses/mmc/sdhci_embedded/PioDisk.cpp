// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot

#include "Disk.h"

// PIO strategy -- intentionally UNUSED. It exists only as a correctness oracle:
// a dead-simple buffer-port path to cross-check DMA results against during
// debugging. The factory never selects it; Transfer() refuses to run so it can
// never be reached by accident on the hot path.

namespace jr::sdhci {


DmaRestrictions
PioDisk::Restrictions() const
{
	DmaRestrictions r;
	r.alignment = 4;
	r.maxSegmentSize = 512 * 1024;
	r.maxSegmentCount = 1;
	return r;
}


status_t
PioDisk::Transfer(off_t, const generic_io_vec*, size_t, bool, size_t&)
{
	// Deliberately unimplemented: PIO is a reference path, never a runtime one.
	return B_NOT_SUPPORTED;
}


} // namespace jr::sdhci
