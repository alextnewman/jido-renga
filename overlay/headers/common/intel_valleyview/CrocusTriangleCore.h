// SPDX-FileCopyrightText: 2010 Jakob Bornecrantz
// SPDX-FileCopyrightText: 2017 Intel Corporation
// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot
#ifndef INTEL_VALLEYVIEW_CROCUS_TRIANGLE_CORE_H
#define INTEL_VALLEYVIEW_CROCUS_TRIANGLE_CORE_H

#include <common/intel_valleyview/RenderCommandCore.h>

#include <stddef.h>
#include <stdint.h>
#include <string.h>


namespace valleyview {

// Mesa 22.0.5 Crocus output for PCI 0x0f31 and a linear 300x300 B8G8R8A8 target.
constexpr uint32 kCrocusTriangleBoCount = 7;
constexpr uint32 kCrocusTriangleCommandBo = 0;
constexpr uint32 kCrocusTriangleStateBo = 2;
constexpr uint32 kCrocusTriangleTargetBo = 4;
constexpr uint32 kCrocusTriangleFenceBo = 6;
constexpr uint32 kCrocusTriangleBatchBytes = 2036;
constexpr uint32 kCrocusTriangleWidth = 300;
constexpr uint32 kCrocusTriangleHeight = 300;
constexpr uint32 kCrocusTriangleStride = 1200;
constexpr uint32 kCrocusTriangleTargetBytes = 393216;
constexpr uint32 kCrocusTriangleSentinel = 0xa55a3cc3;

constexpr uint32 kCrocusTriangleBoSizes[kCrocusTriangleBoCount] = {
	24576, 4096, 16384, 16384, kCrocusTriangleTargetBytes, 4096, 4096
};

constexpr uint32 kCrocusTriangleCommandWords[] = {
	0x7a000003, 0x00101021, 0x00000000, 0x00000000, 0x00000000, 0x7a000003,
	0x00100c0e, 0x00000000, 0x00000000, 0x00000000, 0x69040000, 0x7a000003,
	0x00104000, 0x00000070, 0x00000000, 0x00000000, 0x7b000005, 0x00000001,
	0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x61020000,
	0x00000000, 0x7a000003, 0x00100020, 0x00000000, 0x00000000, 0x00000000,
	0x7a000003, 0x00100c0e, 0x00000000, 0x00000000, 0x00000000, 0x7a000003,
	0x00100020, 0x00000000, 0x00000000, 0x00000000, 0x11000001, 0x0000b010,
	0x00d30000, 0x11000001, 0x0000b020, 0x00880038, 0x11000001, 0x0000b024,
	0x00000000, 0x11000001, 0x000020c0, 0x00400040, 0x790a0001, 0x00000000,
	0x00000000, 0x79060000, 0x00000000, 0x79120000, 0x00000003, 0x79130000,
	0x00030003, 0x79140000, 0x00060003, 0x79150000, 0x00090003, 0x79160000,
	0x000c0004, 0x7a000003, 0x00002000, 0x00000000, 0x00000000, 0x00000000,
	0x7a000003, 0x00000001, 0x00000000, 0x00000000, 0x00000000, 0x7a000003,
	0x00102000, 0x00000000, 0x00000000, 0x00000000, 0x79000002, 0x00000000,
	0x012b012b, 0x00000000, 0x7a000003, 0x00105021, 0x00000070, 0x00000000,
	0x00000000, 0x61010008, 0x00000111, 0x00000101, 0x00000101, 0x00000101,
	0x00000101, 0x00000001, 0xfffff001, 0x00000001, 0x00000001, 0x7a000003,
	0x00104c0c, 0x00000070, 0x00000000, 0x00000000, 0x78080007, 0x0001400c,
	0x00000040, 0x00000063, 0x00000000, 0x04114000, 0x00000080, 0x0000009f,
	0x00000000, 0x78090005, 0x06000000, 0x16220000, 0x02400000, 0x11130000,
	0x06000010, 0x11110000, 0x680b0000, 0x7a000003, 0x00006000, 0x00000070,
	0x00000000, 0x00000000, 0x78300000, 0x04000200, 0x78310000, 0x04000000,
	0x78320000, 0x04000000, 0x78330000, 0x04000000, 0x78240000, 0x000000c1,
	0x780e0000, 0x00000101, 0x78250000, 0x00000141, 0x78150005, 0x00000000,
	0x00000000, 0x00000001, 0x00000000, 0x00000000, 0x00000000, 0x78190005,
	0x00000000, 0x00000000, 0x00000001, 0x00000000, 0x00000000, 0x00000000,
	0x781a0005, 0x00000000, 0x00000000, 0x00000001, 0x00000000, 0x00000000,
	0x00000000, 0x78160005, 0x00000000, 0x00000000, 0x00000001, 0x00000000,
	0x00000000, 0x00000000, 0x78170005, 0x00000000, 0x00000000, 0x00000001,
	0x00000000, 0x00000000, 0x00000000, 0x790d0002, 0x00000000, 0x00000088,
	0x00000000, 0x78180000, 0x00000001, 0x78100004, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x781b0005, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x781c0002, 0x00000000,
	0x00000000, 0x00000000, 0x781d0004, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x781e0001, 0x00000000, 0x00000000, 0x78110005,
	0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x78120002, 0x00000000, 0x00000200, 0x00000000, 0x78130005, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x781f000c,
	0x00400810, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000001, 0x00000000,
	0x00000000, 0x78140001, 0x20000000, 0x00000000, 0x78200006, 0x00000100,
	0x00000000, 0x00000000, 0x2f000403, 0x00020002, 0x00000100, 0x00000140,
	0x78230000, 0x00000160, 0x78260000, 0x00000000, 0x78270000, 0x00000000,
	0x78280000, 0x00000000, 0x78290000, 0x00000000, 0x782a0000, 0x00000180,
	0x78050005, 0xe0040000, 0x00000000, 0x00000000, 0x00000001, 0x00000000,
	0x00000000, 0x78060001, 0x02000001, 0x00000000, 0x78070001, 0x02000000,
	0x00000000, 0x78040001, 0x00000000, 0x00000000, 0x7b000005, 0x0000000f,
	0x00000003, 0x00000000, 0x00000001, 0x00000000, 0x00000000, 0x680b0001,
	0x78230000, 0x000001c0, 0x78210000, 0x00000200, 0x78300000, 0x04000200,
	0x78310000, 0x04000000, 0x78320000, 0x04000000, 0x78330000, 0x04000000,
	0x78240000, 0x00000241, 0x780e0000, 0x00000281, 0x78150005, 0x00000000,
	0x00000000, 0x00000001, 0x00000000, 0x00000000, 0x00000000, 0x78170005,
	0x00000000, 0x00000000, 0x00000001, 0x00000000, 0x00000000, 0x00000000,
	0x78260000, 0x00000000, 0x782a0000, 0x000002c0, 0x782b0000, 0x00000000,
	0x782f0000, 0x00000000, 0x790d0002, 0x00000000, 0x00000088, 0x00000000,
	0x78180000, 0x00000001, 0x78200006, 0x00000080, 0x00040000, 0x00000000,
	0x2f000403, 0x00040006, 0x00000080, 0x000000c0, 0x781e0001, 0x00000000,
	0x00000000, 0x78120002, 0x00050400, 0x9c000026, 0x0003ffe0, 0x78100004,
	0x00000000, 0x00000000, 0x00000000, 0x00100800, 0x46000401, 0x78110005,
	0x00000000, 0x00000000, 0x00000000, 0x00000401, 0x00000400, 0x00000000,
	0x781b0005, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x781c0002, 0x00000000, 0x00000000, 0x00000000, 0x781d0004,
	0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x78130005,
	0x00000402, 0x20000800, 0x4c004800, 0x00000000, 0x00000000, 0x00000000,
	0x78140001, 0xa0000844, 0x00000000, 0x781f000c, 0x00600810, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x78250000,
	0x00000301, 0x780f0000, 0x00000340, 0x7a000003, 0x00002000, 0x00000000,
	0x00000000, 0x00000000, 0x7a000003, 0x00000001, 0x00000000, 0x00000000,
	0x00000000, 0x7a000003, 0x00102000, 0x00000000, 0x00000000, 0x00000000,
	0x78050005, 0xe0040000, 0x00000000, 0x00000000, 0x00000001, 0x00000000,
	0x00000000, 0x78060001, 0x02000001, 0x00000000, 0x78070001, 0x02000000,
	0x00000000, 0x78040001, 0x00000000, 0x00000000, 0x7907001f, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x79080001, 0x00000000, 0x00000000, 0x79000002, 0x00000000,
	0x012b012b, 0x00000000, 0x78080003, 0x00014020, 0x00000000, 0x0000007f,
	0x00000000, 0x78090003, 0x02000000, 0x11110000, 0x02000010, 0x11110000,
	0x7b000005, 0x00000004, 0x00000003, 0x00000000, 0x00000001, 0x00000000,
	0x00000000, 0x7a000003, 0x00100002, 0x00000000, 0x00000000, 0x00000000,
	0x7a000003, 0x00100202, 0x00000000, 0x00000000, 0x00000000, 0x7a000003,
	0x00005021, 0x00000000, 0x00000001, 0x00000000, 0x05000000
};

constexpr uint32 kCrocusTriangleWorkaroundWords[] = {
	0xbbaa9988, 0xffeeddcc, 0x33221100, 0x77665544, 0xbbaa9988, 0xffeeddcc,
	0x33221100, 0x77665544, 0x00000002, 0x0000002e, 0x636f7243, 0x32207375,
	0x2e302e32, 0x75622035, 0x20646c69, 0x69672820, 0x38312d74, 0x62313966,
	0x35393835, 0x00030029, 0x00100000, 0x00000000, 0x00000000, 0x00010000,
	0x00080000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000
};

constexpr uint32 kCrocusTriangleStateWords[] = {
	0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x43960000, 0x43960000,
	0x00000000, 0x00000000, 0x43960000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x3e99999a, 0x3dcccccd, 0x3e99999a, 0x3f800000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x0000000b, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x3f800000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x000001a0, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x3300043f, 0x00000000, 0x012b012b, 0x000004af,
	0x00000000, 0x00010000, 0x00000000, 0x00000000, 0x00000000, 0x3f800000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x43160000, 0x43160000, 0x3f000000, 0x43160000,
	0x43160000, 0x3f000000, 0x00000000, 0x00000000, 0xc2da740e, 0x42da740e,
	0xc2da740e, 0x42da740e, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x0000200b, 0x00000000, 0x0000200b, 0x00000000, 0x0000200b,
	0x00000000, 0x0000200b, 0x00000000, 0x0000200b, 0x00000000, 0x0000200b,
	0x00000000, 0x0000200b, 0x00000000, 0x0000200b, 0x00000001, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x3300043f, 0x00000000, 0x012b012b, 0x000004af, 0x00000000, 0x00010000,
	0x00000000, 0x00000000, 0x000002a0, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x10001000, 0x00000000, 0x08000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x012b012b
};

constexpr uint32 kCrocusTriangleProgramWords[] = {
	0x200c6c01, 0x00007200, 0x00600101, 0x2e6f03bd, 0x006e0024, 0x00000000,
	0x00600101, 0x2e8f03bd, 0x006e0044, 0x00000000, 0x00600301, 0x2e2f0021,
	0x006e0004, 0x00000000, 0x00000206, 0x2e340c21, 0x00000014, 0x0000ff00,
	0x06600131, 0x200f0fbc, 0x006e0e24, 0x8a08c000, 0x2000007e, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x20024b5a, 0x02047ce0, 0x20224b5a, 0x02047de0,
	0x20024b5a, 0x02057ee0, 0x20224b5a, 0x02057fe0, 0x05600032, 0x20000c28,
	0x008d0f80, 0x88031400, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x2002565a, 0x020678e0, 0x2022565a, 0x02067ae0, 0x2002565a, 0x02077ce0,
	0x2022565a, 0x02077ee0, 0x05800032, 0x20000c28, 0x008d0f00, 0x90031000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x201d0b01, 0x00027c00,
	0x00600001, 0x2fa003bd, 0x0000005c, 0x00000000, 0x201d0b01, 0x00037e00,
	0x00600001, 0x2fe003bd, 0x0000007c, 0x00000000, 0x05600032, 0x20000c28,
	0x008d0f80, 0x88031400, 0x201d1601, 0x00027800, 0x00800001, 0x2f4003bd,
	0x0000005c, 0x00000000, 0x201d1601, 0x00037c00, 0x00800001, 0x2fc003bd,
	0x0000007c, 0x00000000, 0x05800032, 0x20000c28, 0x008d0f00, 0x90031000
};

constexpr uint32 kCrocusTriangleVertexWords[] = {
	0x00000000, 0xbf666666, 0x00000000, 0x3f800000, 0x3f800000, 0x00000000,
	0x00000000, 0x3f800000, 0xbf666666, 0x3f666666, 0x00000000, 0x3f800000,
	0x00000000, 0x3f800000, 0x00000000, 0x3f800000, 0x3f666666, 0x3f666666,
	0x00000000, 0x3f800000, 0x00000000, 0x00000000, 0x3f800000, 0x3f800000
};

struct CrocusTriangleRelocation {
	uint32	sourceBo;
	uint32	byteOffset;
	uint32	targetBo;
	uint32	delta;
};

constexpr CrocusTriangleRelocation kCrocusTriangleRelocations[] = {
	{kCrocusTriangleCommandBo, 52, 1, 112},
	{kCrocusTriangleCommandBo, 352, 1, 112},
	{kCrocusTriangleCommandBo, 372, 2, 257},
	{kCrocusTriangleCommandBo, 376, 2, 257},
	{kCrocusTriangleCommandBo, 384, 3, 257},
	{kCrocusTriangleCommandBo, 412, 1, 112},
	{kCrocusTriangleCommandBo, 432, 2, 64},
	{kCrocusTriangleCommandBo, 436, 2, 99},
	{kCrocusTriangleCommandBo, 448, 2, 128},
	{kCrocusTriangleCommandBo, 452, 2, 159},
	{kCrocusTriangleCommandBo, 500, 1, 112},
	{kCrocusTriangleCommandBo, 1912, 5, 0},
	{kCrocusTriangleCommandBo, 1916, 5, 127},
	{kCrocusTriangleCommandBo, 2020, 6, 0},
	{kCrocusTriangleStateBo, 420, 4, 0},
	{kCrocusTriangleStateBo, 676, 4, 0}
};

inline bool
InitializeCrocusTriangleBo(uint32 index, void* address, size_t byteCount)
{
	if (address == NULL || index >= kCrocusTriangleBoCount
		|| byteCount < kCrocusTriangleBoSizes[index]) {
		return false;
	}
	memset(address, 0, kCrocusTriangleBoSizes[index]);
	const void* source = NULL;
	size_t sourceBytes = 0;
	switch (index) {
		case 0:
			source = kCrocusTriangleCommandWords;
			sourceBytes = sizeof(kCrocusTriangleCommandWords);
			break;
		case 1:
			source = kCrocusTriangleWorkaroundWords;
			sourceBytes = sizeof(kCrocusTriangleWorkaroundWords);
			break;
		case 2:
			source = kCrocusTriangleStateWords;
			sourceBytes = sizeof(kCrocusTriangleStateWords);
			break;
		case 3:
			source = kCrocusTriangleProgramWords;
			sourceBytes = sizeof(kCrocusTriangleProgramWords);
			break;
		case 5:
			source = kCrocusTriangleVertexWords;
			sourceBytes = sizeof(kCrocusTriangleVertexWords);
			break;
		case kCrocusTriangleTargetBo:
		{
			uint32* words = static_cast<uint32*>(address);
			for (size_t word = 0;
					word < kCrocusTriangleTargetBytes / sizeof(uint32); word++) {
				words[word] = kCrocusTriangleSentinel;
			}
			break;
		}
		case kCrocusTriangleFenceBo:
			break;
		default:
			return false;
	}
	if (source != NULL)
		memcpy(address, source, sourceBytes);
	return true;
}

inline bool
PatchCrocusTriangleRelocations(void* const* addresses,
	const uint32* renderAddresses, const size_t* byteCounts, size_t count)
{
	if (addresses == NULL || renderAddresses == NULL || byteCounts == NULL
		|| count != kCrocusTriangleBoCount) {
		return false;
	}
	for (size_t index = 0;
			index < sizeof(kCrocusTriangleRelocations)
				/ sizeof(kCrocusTriangleRelocations[0]); index++) {
		const CrocusTriangleRelocation& relocation
			= kCrocusTriangleRelocations[index];
		if (addresses[relocation.sourceBo] == NULL
			|| relocation.byteOffset > byteCounts[relocation.sourceBo]
			|| sizeof(uint32)
				> byteCounts[relocation.sourceBo] - relocation.byteOffset
			|| renderAddresses[relocation.targetBo]
				> UINT32_MAX - relocation.delta) {
			return false;
		}
		uint32* value = reinterpret_cast<uint32*>(
			static_cast<uint8*>(addresses[relocation.sourceBo])
				+ relocation.byteOffset);
		*value = renderAddresses[relocation.targetBo] + relocation.delta;
	}
	return true;
}

struct CrocusTriangleAnalysis {
	uint32	visibleSentinelPixels;
	uint32	paddingMismatchDwords;
	uint32	opaquePixels;
	uint32	clearPixels;
	uint32	trianglePixels;
	uint32	redPixels;
	uint32	greenPixels;
	uint32	bluePixels;
	uint32	coverageRowsMatched;
	uint32	interpolationSamplesMatched;
	uint64	checksum;
};


inline bool
IsCrocusTriangleClearPixel(uint32 value)
{
	const uint32 blue = value & 0xff;
	const uint32 green = (value >> 8) & 0xff;
	const uint32 red = (value >> 16) & 0xff;
	const uint32 alpha = value >> 24;
	return alpha == 0xff && red == blue && red >= 75 && red <= 78
		&& green >= 24 && green <= 27;
}


inline bool
IsCrocusTriangleDominant(uint32 value, uint32 channel)
{
	const uint32 colors[] = {
		(value >> 16) & 0xff, (value >> 8) & 0xff, value & 0xff
	};
	return channel < 3 && colors[channel] > 192
		&& colors[(channel + 1) % 3] < 96
		&& colors[(channel + 2) % 3] < 96;
}


inline bool
AnalyzeCrocusTriangle(const uint32* words, size_t byteCount,
	CrocusTriangleAnalysis& analysis)
{
	analysis = {};
	if (words == NULL || byteCount < kCrocusTriangleTargetBytes)
		return false;
	analysis.checksum = 1469598103934665603ull;
	const uint32 visiblePixels = kCrocusTriangleWidth * kCrocusTriangleHeight;
	for (uint32 pixel = 0; pixel < visiblePixels; pixel++) {
		const uint32 value = words[pixel];
		analysis.checksum ^= value;
		analysis.checksum *= 1099511628211ull;
		if (value == kCrocusTriangleSentinel)
			analysis.visibleSentinelPixels++;
		const uint32 blue = value & 0xff;
		const uint32 green = (value >> 8) & 0xff;
		const uint32 red = (value >> 16) & 0xff;
		const uint32 alpha = value >> 24;
		if (alpha == 0xff)
			analysis.opaquePixels++;
		if (IsCrocusTriangleClearPixel(value))
			analysis.clearPixels++;
		if (red > 192 && green < 96 && blue < 96)
			analysis.redPixels++;
		if (green > 192 && red < 96 && blue < 96)
			analysis.greenPixels++;
		if (blue > 192 && red < 96 && green < 96)
			analysis.bluePixels++;
	}
	analysis.trianglePixels = visiblePixels - analysis.clearPixels;
	for (size_t word = visiblePixels;
			word < kCrocusTriangleTargetBytes / sizeof(uint32); word++) {
		if (words[word] != kCrocusTriangleSentinel)
			analysis.paddingMismatchDwords++;
	}

	const uint32 sampleRows[] = {10, 30, 75, 150, 225, 280};
	for (size_t sample = 0;
			sample < sizeof(sampleRows) / sizeof(sampleRows[0]); sample++) {
		const uint32 y = sampleRows[sample];
		uint32 first = kCrocusTriangleWidth;
		uint32 last = 0;
		uint32 count = 0;
		for (uint32 x = 0; x < kCrocusTriangleWidth; x++) {
			if (!IsCrocusTriangleClearPixel(
					words[y * kCrocusTriangleWidth + x])) {
				if (count == 0)
					first = x;
				last = x;
				count++;
			}
		}
		if (y < 15) {
			if (count == 0)
				analysis.coverageRowsMatched++;
			continue;
		}
		const uint32 expectedCount = y - 15;
		const uint32 expectedFirst
			= 150 - expectedCount / 2;
		const uint32 expectedLast
			= 150 + expectedCount / 2;
		const bool countMatches = count + 4 >= expectedCount
			&& count <= expectedCount + 4;
		const bool firstMatches = first + 3 >= expectedFirst
			&& first <= expectedFirst + 3;
		const bool lastMatches = last + 3 >= expectedLast
			&& last <= expectedLast + 3;
		if (countMatches && firstMatches && lastMatches)
			analysis.coverageRowsMatched++;
	}
	if (IsCrocusTriangleDominant(
			words[30 * kCrocusTriangleWidth + 150], 0)) {
		analysis.interpolationSamplesMatched++;
	}
	if (IsCrocusTriangleDominant(
			words[270 * kCrocusTriangleWidth + 30], 1)) {
		analysis.interpolationSamplesMatched++;
	}
	if (IsCrocusTriangleDominant(
			words[270 * kCrocusTriangleWidth + 270], 2)) {
		analysis.interpolationSamplesMatched++;
	}
	return analysis.visibleSentinelPixels == 0
		&& analysis.paddingMismatchDwords == 0
		&& analysis.opaquePixels == visiblePixels
		&& analysis.clearPixels >= 50000 && analysis.clearPixels <= 60000
		&& analysis.trianglePixels >= 30000 && analysis.trianglePixels <= 40000
		&& analysis.redPixels >= 100 && analysis.greenPixels >= 100
		&& analysis.bluePixels >= 100
		&& analysis.coverageRowsMatched
			== sizeof(sampleRows) / sizeof(sampleRows[0])
		&& analysis.interpolationSamplesMatched == 3;
}


} // namespace valleyview

#endif
