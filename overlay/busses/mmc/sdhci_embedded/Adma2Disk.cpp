// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot

#include "Disk.h"

#include "Adma2.h"
#include "Card.h"
#include "SdhciController.h"
#include "SdhciEngine.h"

#include <string.h>
#include <vm/vm.h>

// ADMA2 scatter/gather strategy -- the Bay Trail eMMC target. A single command
// can move a whole vectored request because the descriptor table expresses the
// scatter list to the controller. The table build uses the host-tested
// Adma2Builder, so the tricky 65536-wraps-to-0 encoding is already proven.

namespace jr::sdhci {


Adma2Disk::~Adma2Disk()
{
	if (fDescriptorArea >= B_OK)
		delete_area(fDescriptorArea);
}


status_t
Adma2Disk::InitStrategy()
{
	const size_t tableBytes = sizeof(Adma2Descriptor) * kAdma2MaxDescriptors;
	const size_t areaBytes = ROUNDUP(tableBytes, B_PAGE_SIZE);

	virtual_address_restrictions virtualRestrictions = {};
	virtualRestrictions.address_specification = B_ANY_KERNEL_ADDRESS;
	physical_address_restrictions physicalRestrictions = {};
	physicalRestrictions.high_address = 0x100000000ull;
	physicalRestrictions.alignment = 64;

	void* address = nullptr;
	fDescriptorArea = create_area_etc(B_SYSTEM_TEAM, "sdhci_emb adma2",
		areaBytes, B_CONTIGUOUS, B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA, 0, 0,
		&virtualRestrictions, &physicalRestrictions, &address);
	if (fDescriptorArea < B_OK)
		return fDescriptorArea;

	physical_entry entry;
	if (get_memory_map(address, areaBytes, &entry, 1) != B_OK
		|| entry.size < tableBytes || entry.address >= 0x100000000ull
		|| (entry.address & 63) != 0) {
		delete_area(fDescriptorArea);
		fDescriptorArea = -1;
		return B_BAD_DATA;
	}

	fDescriptors = static_cast<Adma2Descriptor*>(address);
	fDescriptorPhysical = entry.address;
	memset(fDescriptors, 0, tableBytes);
	return B_OK;
}


DmaRestrictions
Adma2Disk::Restrictions() const
{
	DmaRestrictions r;
	r.alignment = 4;						// 32-bit address descriptors
	r.boundary = 0;							// ADMA2 has no 512K SDMA boundary
	r.maxTransferSize = 512 * 1024;
	r.maxSegmentSize = kAdma2MaxSegmentBytes;
	r.maxSegmentCount = kAdma2MaxDescriptors;
	r.highAddress = 0x100000000ull;			// 32-bit descriptor addresses
	return r;
}


status_t
Adma2Disk::Transfer(off_t position, const generic_io_vec* vecs, size_t vecCount,
	bool isWrite, size_t& bytesTransferred)
{
	if (fDescriptors == nullptr || fDescriptorPhysical == 0 || vecs == nullptr
		|| vecCount == 0 || vecCount > kAdma2MaxDescriptors) {
		return B_BAD_VALUE;
	}

	// Build the descriptor table from the scatter list (tested pure code).
	Adma2Builder builder(fDescriptors, kAdma2MaxDescriptors);
	size_t total = 0;
	for (size_t i = 0; i < vecCount; i++) {
		if (vecs[i].base >= 0x100000000ull
			|| vecs[i].length > UINT32_MAX) {
			return B_BAD_ADDRESS;
		}
		if (!builder.AddSegment(static_cast<uint32_t>(vecs[i].base),
				static_cast<uint32_t>(vecs[i].length))) {
			return B_BUFFER_OVERFLOW;
		}
		total += vecs[i].length;
	}
	if (!builder.Finalize())
		return B_BAD_VALUE;
	if (total == 0 || total % fCard.SectorSize() != 0
		|| position < 0 || position % fCard.SectorSize() != 0) {
		return B_BAD_VALUE;
	}
	if (total > 512 * 1024)
		return B_BUFFER_OVERFLOW;

	const bool sectors = fCard.UsesSectorAddressing();
	const uint64_t block = static_cast<uint64_t>(position) / fCard.SectorSize();
	const uint64_t commandAddress = sectors
		? block : static_cast<uint64_t>(position);
	if (commandAddress > UINT32_MAX)
		return B_BAD_VALUE;
	const uint32_t address = static_cast<uint32_t>(commandAddress);
	const uint32_t blocks = static_cast<uint32_t>(total / fCard.SectorSize());
	if (blocks == 0 || blocks > 65535)
		return B_BAD_VALUE;

	const Cmd command = isWrite
		? (blocks > 1 ? Cmd::WriteMultipleBlocks : Cmd::WriteSingleBlock)
		: (blocks > 1 ? Cmd::ReadMultipleBlocks : Cmd::ReadSingleBlock);

	// Stage the retained physical descriptor-table address and geometry.
	DataTransfer data;
	data.blockSize = static_cast<uint16_t>(fCard.SectorSize());
	data.blockCount = blocks;
	data.adma2Table = fDescriptors;
	data.adma2Address = fDescriptorPhysical;
	data.adma2Entries = builder.Count();

	memory_write_barrier();
	CommandOutcome outcome;
	status_t status = fEngine.ExecuteData(command, address, ReplyType::R1, data,
		outcome);
	if (status == B_OK && R1HasError(outcome.response[0]))
		status = B_IO_ERROR;
	if (status == B_DEV_NOT_READY)
		fController.RecoverCard();
	if (status != B_OK) {
		JR_WARN(fEngine.Label(), "ADMA2 %s failed: CMD%u pos=%" B_PRIdOFF
			" bytes=%" B_PRIuSIZE " blocks=%" B_PRIu32 " vecs=%" B_PRIuSIZE
			" status=%s R1=%#08" B_PRIx32 "\n",
			isWrite ? "write" : "read", static_cast<unsigned>(command), position,
			total, blocks, vecCount, strerror(status), outcome.response[0]);
		return status;
	}

	bytesTransferred = total;
	return B_OK;
}


} // namespace jr::sdhci
