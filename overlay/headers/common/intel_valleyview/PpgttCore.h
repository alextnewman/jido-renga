// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot
#ifndef INTEL_VALLEYVIEW_PPGTT_CORE_H
#define INTEL_VALLEYVIEW_PPGTT_CORE_H

#include <common/intel_valleyview/GpuCore.h>

#include <stddef.h>


namespace valleyview {

constexpr uint64 kPpgttVirtualAddressBytes = 0x80000000ull;
constexpr uint32 kPpgttAddressBits = 31;
constexpr uint32 kPpgttPageBytes = 4096;
constexpr uint32 kPpgttPageCount = 524288;
constexpr uint32 kPpgttPdeCount = 512;
constexpr uint32 kPpgttPtesPerTable = 1024;
constexpr uint32 kPpgttPdeShift = 22;
constexpr uint32 kPpgttPdeSpanBytes = 4 * 1024 * 1024;
constexpr uint32 kPpgttDirectoryGgttPages = 512;
constexpr uint32 kPpgttDirectoryBytes
	= kPpgttDirectoryGgttPages * kPpgttPageBytes;
constexpr uint32 kPpgttDirectoryAlignment = 64 * 1024;
constexpr size_t kPpgttBitmapBytes = 65536;
constexpr uint32 kInvalidPpgttPage = UINT32_MAX;
constexpr uint64 kGen6GttPhysicalAddressBytes = 1ull << 40;


inline bool
ValidatePpgttVaRange(uint64 virtualAddress, uint64 byteCount)
{
	if (byteCount == 0 || virtualAddress < kPpgttPageBytes
		|| (virtualAddress & (kPpgttPageBytes - 1)) != 0
		|| (byteCount & (kPpgttPageBytes - 1)) != 0
		|| virtualAddress >= kPpgttVirtualAddressBytes) {
		return false;
	}

	return byteCount <= kPpgttVirtualAddressBytes - virtualAddress;
}


inline uint32
PpgttPdeIndex(uint32 virtualAddress)
{
	return virtualAddress >> kPpgttPdeShift;
}


inline uint32
PpgttPteIndex(uint32 virtualAddress)
{
	return (virtualAddress >> 12) & (kPpgttPtesPerTable - 1);
}


inline bool
EncodePpgttDirectoryBase(uint64 ggttByteOffset, uint64 ggttByteCount,
	uint32& ppDirBase)
{
	if ((ggttByteOffset & (kPpgttDirectoryAlignment - 1)) != 0
		|| ggttByteOffset > UINT32_MAX
		|| ggttByteOffset
			> 0x100000000ull - kPpgttDirectoryBytes
		|| ggttByteOffset > ggttByteCount
		|| kPpgttDirectoryBytes > ggttByteCount - ggttByteOffset) {
		return false;
	}

	ppDirBase = static_cast<uint32>(ggttByteOffset);
	return true;
}


inline bool
EncodeGen6GttAddress(uint64 physicalAddress, uint32& encodedAddress)
{
	if (physicalAddress >= kGen6GttPhysicalAddressBytes
		|| (physicalAddress & (kPpgttPageBytes - 1)) != 0) {
		return false;
	}

	encodedAddress = static_cast<uint32>(physicalAddress)
		| static_cast<uint32>((physicalAddress >> 28) & 0xff0);
	return true;
}


inline bool
EncodePpgttPde(uint64 physicalAddress, uint32& pde)
{
	uint32 encodedAddress;
	if (!EncodeGen6GttAddress(physicalAddress, encodedAddress))
		return false;

	pde = encodedAddress | kGen7PtePresent;
	return true;
}


inline bool
EncodePpgttDataPte(uint64 physicalAddress, bool writable, bool snooped,
	uint32& pte)
{
	uint32 encodedAddress;
	if (!EncodeGen6GttAddress(physicalAddress, encodedAddress))
		return false;

	pte = encodedAddress | kGen7PtePresent;
	if (writable)
		pte |= kBytPteWriteable;
	if (snooped)
		pte |= kBytPteSnooped;
	return true;
}


inline bool
GetPpgttBitmapBit(const uint8* bitmap, size_t bitmapByteCount, uint32 page,
	bool& allocated)
{
	const size_t byte = page / 8;
	if (bitmap == NULL || page >= kPpgttPageCount
		|| byte >= bitmapByteCount) {
		return false;
	}

	allocated = (bitmap[byte] & (1u << (page & 7))) != 0;
	return true;
}


inline bool
SetPpgttBitmapBit(uint8* bitmap, size_t bitmapByteCount, uint32 page)
{
	const size_t byte = page / 8;
	if (bitmap == NULL || page >= kPpgttPageCount
		|| byte >= bitmapByteCount) {
		return false;
	}

	bitmap[byte] |= static_cast<uint8>(1u << (page & 7));
	return true;
}


inline bool
ClearPpgttBitmapBit(uint8* bitmap, size_t bitmapByteCount, uint32 page)
{
	const size_t byte = page / 8;
	if (bitmap == NULL || page >= kPpgttPageCount
		|| byte >= bitmapByteCount) {
		return false;
	}

	bitmap[byte] &= static_cast<uint8>(~(1u << (page & 7)));
	return true;
}


inline uint32
AlignPpgttPage(uint32 page, uint32 alignmentPages)
{
	return (page + alignmentPages - 1) & ~(alignmentPages - 1);
}


inline bool
FindFreePpgttRun(const uint8* bitmap, size_t bitmapByteCount,
	uint32 requiredPages, uint32 alignmentPages, uint32& firstPage)
{
	firstPage = kInvalidPpgttPage;
	if (bitmap == NULL || bitmapByteCount < kPpgttBitmapBytes
		|| requiredPages == 0 || requiredPages >= kPpgttPageCount
		|| alignmentPages == 0 || alignmentPages > kPpgttPageCount
		|| (alignmentPages & (alignmentPages - 1)) != 0) {
		return false;
	}

	uint32 candidate = AlignPpgttPage(1, alignmentPages);
	const uint32 lastCandidate = kPpgttPageCount - requiredPages;
	while (candidate <= lastCandidate) {
		uint32 offset = 0;
		for (; offset < requiredPages; offset++) {
			bool allocated;
			if (!GetPpgttBitmapBit(bitmap, bitmapByteCount,
					candidate + offset, allocated)) {
				return false;
			}
			if (allocated)
				break;
		}

		if (offset == requiredPages) {
			firstPage = candidate;
			return true;
		}

		const uint32 nextPage = candidate + offset + 1;
		if (nextPage > lastCandidate)
			break;
		candidate = AlignPpgttPage(nextPage, alignmentPages);
	}
	return false;
}

} // namespace valleyview

#endif
