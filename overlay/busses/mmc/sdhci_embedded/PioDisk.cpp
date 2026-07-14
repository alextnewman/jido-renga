// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot

#include "Disk.h"

// Disabled PIO strategy retained as a DMA diagnostic reference. The factory
// never selects it and Transfer() rejects runtime use.

namespace jr::sdhci {


DmaRestrictions
PioDisk::Restrictions() const
{
	DmaRestrictions r;
	r.alignment = 4;
	r.maxTransferSize = 512 * 1024;
	r.maxSegmentSize = 512 * 1024;
	r.maxSegmentCount = 1;
	return r;
}


status_t
PioDisk::Transfer(off_t, const generic_io_vec*, size_t, bool, size_t&)
{
	return B_NOT_SUPPORTED;
}


} // namespace jr::sdhci
