// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot

#include "Disk.h"

#include "Adma2.h"
#include "Card.h"
#include "SdhciEngine.h"

// ADMA2 scatter/gather strategy -- the Bay Trail eMMC target. A single command
// can move a whole vectored request because the descriptor table expresses the
// scatter list to the controller. The table build uses the host-tested
// Adma2Builder, so the tricky 65536-wraps-to-0 encoding is already proven.

namespace jr::sdhci {


DmaRestrictions
Adma2Disk::Restrictions() const
{
	DmaRestrictions r;
	r.alignment = 4;						// 32-bit address descriptors
	r.boundary = 0;							// ADMA2 has no 512K SDMA boundary
	r.maxSegmentSize = kAdma2MaxSegmentBytes;
	r.maxSegmentCount = kAdma2MaxDescriptors;
	r.highAddress = 0x100000000ull;			// 32-bit descriptor addresses
	return r;
}


status_t
Adma2Disk::Transfer(off_t position, const generic_io_vec* vecs, size_t vecCount,
	bool isWrite, size_t& bytesTransferred)
{
	// Build the descriptor table from the scatter list (tested pure code).
	Adma2Builder builder(fDescriptors, kAdma2MaxDescriptors);
	size_t total = 0;
	for (size_t i = 0; i < vecCount; i++) {
		if (!builder.AddSegment(static_cast<uint32_t>(vecs[i].base),
				static_cast<uint32_t>(vecs[i].length))) {
			return B_BUFFER_OVERFLOW;
		}
		total += vecs[i].length;
	}
	if (!builder.Finalize())
		return B_BAD_VALUE;

	const bool sectors = fCard.UsesSectorAddressing();
	const uint32_t address = sectors
		? static_cast<uint32_t>(position / fCard.SectorSize())
		: static_cast<uint32_t>(position);
	const uint32_t blocks = static_cast<uint32_t>(total / fCard.SectorSize());

	const Cmd command = isWrite
		? (blocks > 1 ? Cmd::WriteMultipleBlocks : Cmd::WriteSingleBlock)
		: (blocks > 1 ? Cmd::ReadMultipleBlocks : Cmd::ReadSingleBlock);

	// Stage the descriptor table (virtual pointer + entry count) and geometry
	// onto the ticket. The worker resolves the table's physical base via
	// get_memory_map, switches to ADMA2 mode, programs the address, and issues
	// the command -- all inside its bus lock, so DMA setup and command issue are
	// one serialized step and cannot interleave with another caller.
	DataTransfer data;
	data.blockSize = static_cast<uint16_t>(fCard.SectorSize());
	data.blockCount = blocks;
	data.adma2Table = fDescriptors;
	data.adma2Entries = builder.Count();

	CommandOutcome outcome;
	status_t status = fEngine.ExecuteData(command, address, ReplyType::R1, data,
		outcome);
	if (status != B_OK)
		return status;

	bytesTransferred = total;
	return B_OK;
}


} // namespace jr::sdhci
