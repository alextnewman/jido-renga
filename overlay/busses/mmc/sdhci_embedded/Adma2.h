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
// SDHCI spec v3.00 Sec 4.4.1: 8 bytes per 32-bit descriptor, table at least
// 8-byte aligned, and a 16-bit length where 0 encodes 65536 bytes.

namespace jr::sdhci {


enum class Adma2Attr : uint16_t {
	Transfer	= 0x0021,	// VALID | ACT_TRAN
	TransferEnd	= 0x0023,	// VALID | END | ACT_TRAN
};


struct Adma2Descriptor {
	uint16_t	attributes;	// Adma2Attr, little-endian
	uint16_t	length;		// bytes; 0 encodes 65536, little-endian
	uint32_t	address;	// 32-bit physical, little-endian
} __attribute__((packed));

static_assert(sizeof(Adma2Descriptor) == 8, "ADMA2 descriptor must be 8 bytes");


// Largest byte count a single descriptor can express (16-bit field, 0 == max).
constexpr uint32_t kAdma2MaxSegmentBytes = 65536;

// One request is limited to 128 descriptors of at most 65536 bytes each.
constexpr uint32_t kAdma2MaxDescriptors = 128;


constexpr uint16_t
AdmaLittle16(uint16_t value) noexcept
{
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
	return value;
#else
	return __builtin_bswap16(value);
#endif
}


constexpr uint32_t
AdmaLittle32(uint32_t value) noexcept
{
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
	return value;
#else
	return __builtin_bswap32(value);
#endif
}


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
		if ((physicalAddress & 3u) != 0
			|| static_cast<uint64_t>(physicalAddress) + bytes
				> 0x100000000ull) {
			fOverflow = true;
			return false;
		}

		uint32_t offset = 0;
		while (bytes > 0) {
			if (fCount >= fCapacity) {
				fOverflow = true;
				return false;
			}

			const uint32_t chunk
				= bytes > kAdma2MaxSegmentBytes ? kAdma2MaxSegmentBytes : bytes;

			Adma2Descriptor& d = fTable[fCount++];
			d.attributes = AdmaLittle16(static_cast<uint16_t>(Adma2Attr::Transfer));
			d.length = AdmaLittle16(static_cast<uint16_t>(chunk & 0xffff));
				// chunk == 65536 wraps to 0, which the hardware reads as 65536.
			d.address = AdmaLittle32(physicalAddress + offset);

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
		fTable[fCount - 1].attributes
			= AdmaLittle16(static_cast<uint16_t>(Adma2Attr::TransferEnd));
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
