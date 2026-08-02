// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot
#ifndef INTEL_VALLEYVIEW_RENDER_MEMORY_CORE_H
#define INTEL_VALLEYVIEW_RENDER_MEMORY_CORE_H

#include <common/intel_valleyview/GpuCore.h>
#include <common/intel_valleyview/RenderProtocol.h>


namespace valleyview {

constexpr uint64 kRenderMaxBufferSize = 64ull * 1024 * 1024;
constexpr uint64 kRenderMaxClientBytes = 256ull * 1024 * 1024;
constexpr uint32 kRenderMaxClientBuffers = 256;
constexpr uint32 kRenderFirstGgttPage = 1;
constexpr uint32 kInvalidRenderGgttOffset = UINT32_MAX;
constexpr uint32 kInvalidRenderPpgttOffset = UINT32_MAX;

constexpr uint32 kRenderMemoryTestWidth = 16;
constexpr uint32 kRenderMemoryTestHeight = 64;
constexpr uint32 kRenderMemoryTestStride
	= kRenderMemoryTestWidth * sizeof(uint32);
constexpr uint32 kRenderMemoryTestBytes
	= kRenderMemoryTestStride * kRenderMemoryTestHeight;
constexpr uint32 kRenderMemoryTestWords
	= kRenderMemoryTestBytes / sizeof(uint32);
constexpr uint32 kRenderMemoryTestDefaultSeed = 0x43524f43;


inline bool
NormalizeRenderBufferSize(uint64 requestedSize, uint64& size)
{
	if (requestedSize == 0 || requestedSize > kRenderMaxBufferSize
		|| requestedSize > UINT64_MAX - kPageMask) {
		return false;
	}

	size = (requestedSize + kPageMask) & ~static_cast<uint64>(kPageMask);
	return size >= requestedSize && size <= kRenderMaxBufferSize;
}


struct RenderGgttSearch {
	uint32	firstPage;
	uint32	endPage;
	uint32	requiredPages;
	uint32	alignmentPages;
	uint32	freePte;
	uint32	candidatePage;
	uint32	runPages;
	uint32	previousPage;
	uint32	offset;
	bool	found;
};


inline bool
InitializeRenderGgttSearch(RenderGgttSearch& search, uint32 firstPage,
	uint32 endPage, uint32 requiredPages, uint32 freePte,
	uint32 alignmentPages = 1)
{
	if (requiredPages == 0 || firstPage >= endPage
		|| requiredPages > endPage - firstPage || alignmentPages == 0
		|| (alignmentPages & (alignmentPages - 1)) != 0) {
		return false;
	}

	search.firstPage = firstPage;
	search.endPage = endPage;
	search.requiredPages = requiredPages;
	search.alignmentPages = alignmentPages;
	search.freePte = freePte;
	search.candidatePage = 0;
	search.runPages = 0;
	search.previousPage = 0;
	search.offset = kInvalidRenderGgttOffset;
	search.found = false;
	return true;
}


inline bool
AdvanceRenderGgttSearch(RenderGgttSearch& search, uint32 page, uint32 pte)
{
	if (search.found)
		return true;
	if (page < search.firstPage || page >= search.endPage
		|| pte != search.freePte) {
		search.runPages = 0;
		return false;
	}

	if (search.runPages == 0 || page != search.previousPage + 1) {
		if ((page & (search.alignmentPages - 1)) != 0) {
			search.runPages = 0;
			search.previousPage = page;
			return false;
		}
		search.candidatePage = page;
		search.runPages = 1;
	} else
		search.runPages++;
	search.previousPage = page;

	if (search.runPages == search.requiredPages) {
		search.offset = search.candidatePage * kPageSize;
		search.found = true;
	}
	return search.found;
}


inline bool
IsRenderBufferDomain(RenderBufferDomain domain)
{
	return domain == kRenderDomainCpu || domain == kRenderDomainBcs
		|| domain == kRenderDomainRcs;
}


inline bool
CanTransitionRenderBufferDomain(RenderBufferDomain current,
	RenderBufferDomain requested, uint32 flags)
{
	return IsRenderBufferDomain(current)
		&& IsRenderBufferDomain(requested)
		&& current != kRenderDomainRcs
		&& requested != kRenderDomainRcs
		&& (flags & ~kRenderSupportedBufferFlags) == 0
		&& (flags & kRenderBufferCpuCached) != 0;
}


inline uint32
RenderMemoryTestWord(uint32 index, uint32 seed)
{
	uint32 value = seed ^ (index * 0x9e3779b9u);
	return (value << (index & 15)) | (value >> ((32 - index) & 31));
}


inline uint32
RenderMemoryTestDestinationWord(uint32 index, uint32 seed)
{
	return ~RenderMemoryTestWord(index, seed);
}

} // namespace valleyview

#endif
