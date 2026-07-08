// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot
#pragma once

#include <stdint.h>
#include <stddef.h>

// ADMA2 scatter/gather descriptor layout and a pure builder that turns a list
// of physical segments into a descriptor table. Hardware-defined layout; the
// builder has no kernel dependencies so it is fully unit testable.
//
// SDHCI spec v3.00 Sec 4.4.1: 8 bytes per descriptor, table 64-byte aligned,
// 16-bit length where 0 encodes the maximum (65536) bytes.

namespace jr::sdhci {


enum class Adma2Attr : uint8_t {
	Nop		= 0x00,
	Valid	= 0x06,	// valid data segment, continue
	End		= 0x0C,	// valid data segment, last entry (raises transfer-complete)
};


struct Adma2Descriptor {
	uint16_t	length;		// bytes; 0 encodes 65536
	uint8_t		attributes;	// Adma2Attr
	uint8_t		reserved;	// must be zero
	uint32_t	address;	// 32-bit physical, little-endian
} __attribute__((packed));

static_assert(sizeof(Adma2Descriptor) == 8, "ADMA2 descriptor must be 8 bytes");


// Largest byte count a single descriptor can express (16-bit field, 0 == max).
constexpr uint32_t kAdma2MaxSegmentBytes = 65536;

// Descriptor table capacity. 512 descriptors == one 4 KiB page (naturally
// 64-byte aligned), bounding a single transfer's scatter/gather list.
constexpr uint32_t kAdma2MaxDescriptors = 512;


// Builds an ADMA2 descriptor table in caller-provided storage. Splits
// oversized segments across multiple descriptors and marks the final entry
// with the End attribute. Reports overflow rather than writing past the table.
class Adma2Builder {
public:
	Adma2Builder(Adma2Descriptor* table, uint32_t capacity) noexcept
		:
		fTable(table),
		fCapacity(capacity),
		fCount(0),
		fOverflow(false)
	{
	}

	// Append a physical segment, splitting into <= kAdma2MaxSegmentBytes
	// chunks. Returns false (and latches overflow) if the table is full.
	bool
	AddSegment(uint32_t physicalAddress, uint32_t bytes) noexcept
	{
		if (bytes == 0)
			return true;

		uint32_t offset = 0;
		while (bytes > 0) {
			if (fCount >= fCapacity) {
				fOverflow = true;
				return false;
			}

			const uint32_t chunk
				= bytes > kAdma2MaxSegmentBytes ? kAdma2MaxSegmentBytes : bytes;

			Adma2Descriptor& d = fTable[fCount++];
			d.length = static_cast<uint16_t>(chunk & 0xffff);
				// chunk == 65536 wraps to 0, which the hardware reads as 65536.
			d.attributes = static_cast<uint8_t>(Adma2Attr::Valid);
			d.reserved = 0;
			d.address = physicalAddress + offset;

			offset += chunk;
			bytes -= chunk;
		}
		return true;
	}

	// Mark the table complete. Returns false if no data segments were added or
	// the table overflowed. Must be called before handing the table to the
	// controller.
	bool
	Finalize() noexcept
	{
		if (fOverflow || fCount == 0)
			return false;
		fTable[fCount - 1].attributes = static_cast<uint8_t>(Adma2Attr::End);
		return true;
	}

	uint32_t	Count() const noexcept { return fCount; }
	bool		Overflowed() const noexcept { return fOverflow; }

private:
	Adma2Descriptor*	fTable;
	uint32_t			fCapacity;
	uint32_t			fCount;
	bool				fOverflow;
};


} // namespace jr::sdhci
