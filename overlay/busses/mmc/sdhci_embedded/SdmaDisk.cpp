// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot

#include "Disk.h"

#include "Card.h"
#include "SdhciEngine.h"

// Single-buffer SDMA strategy -- the fallback when ADMA2 is unavailable. One
// physically-contiguous buffer per transfer, bounded by the 512K SDMA buffer
// boundary, so the IOScheduler must decompose requests accordingly.

namespace jr::sdhci {


DmaRestrictions
SdmaDisk::Restrictions() const
{
	DmaRestrictions r;
	r.alignment = 4;
	r.boundary = 512 * 1024;			// the SDMA buffer-boundary
	r.maxSegmentSize = 512 * 1024;
	r.maxSegmentCount = 1;				// single-buffer DMA
	r.highAddress = 0x100000000ull;
	return r;
}


status_t
SdmaDisk::Transfer(off_t position, const generic_io_vec* vecs, size_t vecCount,
	bool isWrite, size_t& bytesTransferred)
{
	if (vecCount != 1)
		return B_BAD_VALUE;			// single-segment strategy

	const uint32_t bytes = static_cast<uint32_t>(vecs[0].length);
	const uint32_t blocks = bytes / fCard.SectorSize();

	const bool sectors = fCard.UsesSectorAddressing();
	const uint32_t address = sectors
		? static_cast<uint32_t>(position / fCard.SectorSize())
		: static_cast<uint32_t>(position);

	const Cmd command = isWrite
		? (blocks > 1 ? Cmd::WriteMultipleBlocks : Cmd::WriteSingleBlock)
		: (blocks > 1 ? Cmd::ReadMultipleBlocks : Cmd::ReadSingleBlock);

	// Stage the single SDMA buffer address + geometry onto the ticket; the
	// worker programs the DMA registers and issues the command as one step
	// inside its bus lock, so we never poke hardware from this thread.
	DataTransfer data;
	data.blockSize = static_cast<uint16_t>(fCard.SectorSize());
	data.blockCount = blocks;
	data.sdmaAddress = static_cast<uint32_t>(vecs[0].base);

	CommandOutcome outcome;
	status_t status = fEngine.ExecuteData(command, address, ReplyType::R1, data,
		outcome);
	if (status != B_OK)
		return status;

	bytesTransferred = bytes;
	return B_OK;
}


} // namespace jr::sdhci
