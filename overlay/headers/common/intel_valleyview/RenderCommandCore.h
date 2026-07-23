// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot
#ifndef INTEL_VALLEYVIEW_RENDER_COMMAND_CORE_H
#define INTEL_VALLEYVIEW_RENDER_COMMAND_CORE_H

#include <common/intel_valleyview/PpgttCore.h>

#include <stddef.h>
#include <stdint.h>


namespace valleyview {

constexpr size_t kRenderCommandMaxBatchBytes = 64 * 1024;
constexpr uint32 kRenderCommandInvalidOffset = UINT32_MAX;
constexpr uint32 kRenderCommandInvalidDword = UINT32_MAX;

constexpr uint32 kRenderMiNoopOpcode = 0x00;
constexpr uint32 kRenderMiBatchBufferEndOpcode = 0x0a;
constexpr uint32 kRenderMiLoadRegisterImmOpcode = 0x22;
constexpr uint32 kRenderMiBatchBufferStartOpcode = 0x31;
constexpr uint32 kRenderMiOpcodeMask = 0x1f800000;
constexpr uint32 kRenderMiLriHeaderMask = kRenderMiOpcodeMask | 0xff;
constexpr uint32 kRenderMiBatchBufferEnd = 0x05000000;

constexpr uint32 kRenderPipeControlOpcode = 0x7a000000;
constexpr uint32 kRenderPipelineSelectOpcode = 0x69040000;
constexpr uint32 kRenderStateBaseAddressOpcode = 0x61010000;
constexpr uint32 kRenderStateSipOpcode = 0x61020000;
constexpr uint32 kRender3dPrimitiveOpcode = 0x7b000000;
constexpr uint32 kRenderVfStatisticsOpcode = 0x680b0000;

constexpr uint32 kRenderPipeControlGlobalGtt = 1u << 24;
constexpr uint32 kRenderPipeControlMmioWrite = 1u << 23;
constexpr uint32 kRenderPipeControlStoreDataIndex = 1u << 21;
constexpr uint32 kRenderPipeControlPostSyncMask = 3u << 14;
constexpr uint32 kRenderPipeControlNotify = 1u << 8;
constexpr uint32 kRenderPipeControlAddressMask = 0xfffffffc;


enum RenderCommandStatus {
	kRenderCommandStatusAccepted = 0,
	kRenderCommandStatusRejected
};


enum RenderCommandReason {
	kRenderCommandReasonNone = 0,
	kRenderCommandReasonNullBatch,
	kRenderCommandReasonEmptyBatch,
	kRenderCommandReasonOversizedBatch,
	kRenderCommandReasonUnalignedBatch,
	kRenderCommandReasonForbiddenMi,
	kRenderCommandReasonNestedBatch,
	kRenderCommandReasonBlt,
	kRenderCommandReasonUnknownType,
	kRenderCommandReasonUnknownRenderOpcode,
	kRenderCommandReasonMalformedLength,
	kRenderCommandReasonTruncatedCommand,
	kRenderCommandReasonLriRegister,
	kRenderCommandReasonLriValue,
	kRenderCommandReasonPipeControlMmioWrite,
	kRenderCommandReasonPipeControlNotify,
	kRenderCommandReasonPipeControlStoreDataIndex,
	kRenderCommandReasonPipeControlGlobalGtt,
	kRenderCommandReasonPipeControlAddressAlignment,
	kRenderCommandReasonPipeControlAddressRange,
	kRenderCommandReasonMissingEnd,
	kRenderCommandReasonTrailingData
};


struct RenderCommandResult {
	RenderCommandStatus	status;
	RenderCommandReason	reason;
	uint32				failingByteOffset;
	uint32				failingDword;
	uint32				parsedCommandCount;
	uint32				lriRegisterCount;
	uint32				pipeControlCount;
	uint32				primitiveCount;
	bool				sawEnd;
};


inline RenderCommandResult
InitializeRenderCommandResult()
{
	RenderCommandResult result = {};
	result.status = kRenderCommandStatusRejected;
	result.reason = kRenderCommandReasonNone;
	result.failingByteOffset = kRenderCommandInvalidOffset;
	result.failingDword = kRenderCommandInvalidDword;
	return result;
}


inline RenderCommandResult
RejectRenderCommand(RenderCommandResult result, RenderCommandReason reason,
	uint32 byteOffset, uint32 dword)
{
	result.status = kRenderCommandStatusRejected;
	result.reason = reason;
	result.failingByteOffset = byteOffset;
	result.failingDword = dword;
	return result;
}


inline uint32
RenderMiOpcode(uint32 command)
{
	return (command & kRenderMiOpcodeMask) >> 23;
}


inline bool
IsAllowedRenderOpcode(uint32 opcode)
{
	switch (opcode) {
		case kRenderPipeControlOpcode:
		case kRenderPipelineSelectOpcode:
		case kRenderStateBaseAddressOpcode:
		case kRenderStateSipOpcode:
		case kRender3dPrimitiveOpcode:
		case kRenderVfStatisticsOpcode:
		case 0x78080000: // 3DSTATE_VERTEX_BUFFERS
		case 0x78090000: // 3DSTATE_VERTEX_ELEMENTS
		case 0x78300000: // 3DSTATE_URB_VS
		case 0x78310000: // 3DSTATE_URB_HS
		case 0x78320000: // 3DSTATE_URB_DS
		case 0x78330000: // 3DSTATE_URB_GS
		case 0x78240000: // 3DSTATE_BLEND_STATE_POINTERS
		case 0x780e0000: // 3DSTATE_CC_STATE_POINTERS
		case 0x78250000: // 3DSTATE_DEPTH_STENCIL_STATE_POINTERS
		case 0x78150000: // 3DSTATE_CONSTANT_VS
		case 0x78190000: // 3DSTATE_CONSTANT_HS
		case 0x781a0000: // 3DSTATE_CONSTANT_DS
		case 0x78160000: // 3DSTATE_CONSTANT_GS
		case 0x78170000: // 3DSTATE_CONSTANT_PS
		case 0x790d0000: // 3DSTATE_MULTISAMPLE
		case 0x78180000: // 3DSTATE_SAMPLE_MASK
		case 0x78100000: // 3DSTATE_VS
		case 0x781b0000: // 3DSTATE_HS
		case 0x781c0000: // 3DSTATE_TE
		case 0x781d0000: // 3DSTATE_DS
		case 0x781e0000: // 3DSTATE_STREAMOUT
		case 0x78110000: // 3DSTATE_GS
		case 0x78120000: // 3DSTATE_CLIP
		case 0x78130000: // 3DSTATE_SF
		case 0x781f0000: // 3DSTATE_SBE
		case 0x78140000: // 3DSTATE_WM
		case 0x78200000: // 3DSTATE_PS
		case 0x78230000: // 3DSTATE_VIEWPORT_STATE_POINTERS_CC
		case 0x78210000: // 3DSTATE_VIEWPORT_STATE_POINTERS_SF_CLIP
		case 0x78260000: // 3DSTATE_BINDING_TABLE_POINTERS_VS
		case 0x78270000: // 3DSTATE_BINDING_TABLE_POINTERS_HS
		case 0x78280000: // 3DSTATE_BINDING_TABLE_POINTERS_DS
		case 0x78290000: // 3DSTATE_BINDING_TABLE_POINTERS_GS
		case 0x782a0000: // 3DSTATE_BINDING_TABLE_POINTERS_PS
		case 0x782b0000: // 3DSTATE_SAMPLER_STATE_POINTERS_VS
		case 0x782f0000: // 3DSTATE_SAMPLER_STATE_POINTERS_PS
		case 0x78050000: // 3DSTATE_DEPTH_BUFFER
		case 0x78060000: // 3DSTATE_STENCIL_BUFFER
		case 0x78070000: // 3DSTATE_HIER_DEPTH_BUFFER
		case 0x78040000: // 3DSTATE_CLEAR_PARAMS
		case 0x780f0000: // 3DSTATE_SCISSOR_STATE_POINTERS
		case 0x790a0000: // 3DSTATE_AA_LINE_PARAMETERS
		case 0x79060000: // 3DSTATE_POLY_STIPPLE_OFFSET
		case 0x79070000: // 3DSTATE_POLY_STIPPLE_PATTERN
		case 0x79120000: // 3DSTATE_PUSH_CONSTANT_ALLOC_VS
		case 0x79130000: // 3DSTATE_PUSH_CONSTANT_ALLOC_HS
		case 0x79140000: // 3DSTATE_PUSH_CONSTANT_ALLOC_DS
		case 0x79150000: // 3DSTATE_PUSH_CONSTANT_ALLOC_GS
		case 0x79160000: // 3DSTATE_PUSH_CONSTANT_ALLOC_PS
		case 0x79080000: // 3DSTATE_LINE_STIPPLE
		case 0x79000000: // 3DSTATE_DRAWING_RECTANGLE
			return true;
		default:
			return false;
	}
}


inline bool
IsAllowedRenderLriRegister(uint32 reg)
{
	return reg == 0x0000b010 || reg == 0x0000b020
		|| reg == 0x0000b024 || reg == 0x000020c0;
}


inline bool
IsAllowedRenderLriPair(uint32 reg, uint32 value)
{
	switch (reg) {
		case 0x0000b010:
			return value == 0x00d30000;
		case 0x0000b020:
			return value == 0x00880038 || value == 0x01040011;
		case 0x0000b024:
			return value == 0;
		case 0x000020c0:
			return value == 0x00400040;
		default:
			return false;
	}
}


inline RenderCommandResult
ParseRenderCommands(const void* batch, size_t byteCount)
{
	RenderCommandResult result = InitializeRenderCommandResult();
	if (batch == NULL) {
		return RejectRenderCommand(result, kRenderCommandReasonNullBatch,
			kRenderCommandInvalidOffset, kRenderCommandInvalidDword);
	}
	if (byteCount == 0) {
		return RejectRenderCommand(result, kRenderCommandReasonEmptyBatch, 0,
			kRenderCommandInvalidDword);
	}
	if (byteCount > kRenderCommandMaxBatchBytes) {
		return RejectRenderCommand(result, kRenderCommandReasonOversizedBatch,
			0, kRenderCommandInvalidDword);
	}
	if ((byteCount & (sizeof(uint32) - 1)) != 0
		|| (reinterpret_cast<uintptr_t>(batch) & (sizeof(uint32) - 1)) != 0) {
		return RejectRenderCommand(result, kRenderCommandReasonUnalignedBatch,
			0, kRenderCommandInvalidDword);
	}

	const uint32* commands = static_cast<const uint32*>(batch);
	const size_t dwordCount = byteCount / sizeof(uint32);
	size_t index = 0;
	while (index < dwordCount) {
		const uint32 command = commands[index];
		const uint32 byteOffset
			= static_cast<uint32>(index * sizeof(uint32));
		const uint32 type = command >> 29;

		if (type == 0) {
			const uint32 opcode = RenderMiOpcode(command);
			if (opcode == kRenderMiNoopOpcode) {
				result.parsedCommandCount++;
				index++;
				continue;
			}
			if (opcode == kRenderMiBatchBufferEndOpcode
				&& command == kRenderMiBatchBufferEnd) {
				result.parsedCommandCount++;
				result.sawEnd = true;
				for (index++; index < dwordCount; index++) {
					if (commands[index] != 0) {
						return RejectRenderCommand(result,
							kRenderCommandReasonTrailingData,
							static_cast<uint32>(index * sizeof(uint32)),
							commands[index]);
					}
				}
				result.status = kRenderCommandStatusAccepted;
				result.reason = kRenderCommandReasonNone;
				return result;
			}
			if (opcode == kRenderMiBatchBufferStartOpcode) {
				return RejectRenderCommand(result,
					kRenderCommandReasonNestedBatch, byteOffset, command);
			}
			if (opcode != kRenderMiLoadRegisterImmOpcode) {
				return RejectRenderCommand(result,
					kRenderCommandReasonForbiddenMi, byteOffset, command);
			}
			if ((command & ~kRenderMiLriHeaderMask) != 0) {
				return RejectRenderCommand(result,
					kRenderCommandReasonForbiddenMi, byteOffset, command);
			}

			const uint32 commandDwords = (command & 0xff) + 2;
			if (commandDwords < 3 || (commandDwords & 1) == 0) {
				return RejectRenderCommand(result,
					kRenderCommandReasonMalformedLength, byteOffset, command);
			}
			if (commandDwords > dwordCount - index) {
				return RejectRenderCommand(result,
					kRenderCommandReasonTruncatedCommand, byteOffset, command);
			}
			for (uint32 pair = 1; pair < commandDwords; pair += 2) {
				const uint32 reg = commands[index + pair];
				const uint32 value = commands[index + pair + 1];
				if (!IsAllowedRenderLriRegister(reg)) {
					return RejectRenderCommand(result,
						kRenderCommandReasonLriRegister,
						static_cast<uint32>((index + pair) * sizeof(uint32)),
						reg);
				}
				if (!IsAllowedRenderLriPair(reg, value)) {
					return RejectRenderCommand(result,
						kRenderCommandReasonLriValue,
						static_cast<uint32>((index + pair + 1)
							* sizeof(uint32)),
						value);
				}
				result.lriRegisterCount++;
			}
			result.parsedCommandCount++;
			index += commandDwords;
			continue;
		}

		if (type == 2) {
			return RejectRenderCommand(result, kRenderCommandReasonBlt,
				byteOffset, command);
		}
		if (type != 3) {
			return RejectRenderCommand(result, kRenderCommandReasonUnknownType,
				byteOffset, command);
		}

		const uint32 opcode = command & 0xffff0000;
		if (!IsAllowedRenderOpcode(opcode)) {
			return RejectRenderCommand(result,
				kRenderCommandReasonUnknownRenderOpcode, byteOffset, command);
		}

		uint32 commandDwords = 1;
		if (opcode != kRenderPipelineSelectOpcode
			&& opcode != kRenderVfStatisticsOpcode) {
			commandDwords = (command & 0xff) + 2;
			if ((opcode == kRenderPipeControlOpcode && commandDwords != 5)
				|| (opcode == kRender3dPrimitiveOpcode
					&& commandDwords != 7)) {
				return RejectRenderCommand(result,
					kRenderCommandReasonMalformedLength, byteOffset, command);
			}
			if (commandDwords > dwordCount - index) {
				return RejectRenderCommand(result,
					kRenderCommandReasonTruncatedCommand, byteOffset, command);
			}
		}

		if (opcode == kRenderPipeControlOpcode) {
			const uint32 flags = commands[index + 1];
			if ((flags & kRenderPipeControlMmioWrite) != 0) {
				return RejectRenderCommand(result,
					kRenderCommandReasonPipeControlMmioWrite,
					byteOffset + sizeof(uint32), flags);
			}
			if ((flags & kRenderPipeControlNotify) != 0) {
				return RejectRenderCommand(result,
					kRenderCommandReasonPipeControlNotify,
					byteOffset + sizeof(uint32), flags);
			}
			if ((flags & kRenderPipeControlStoreDataIndex) != 0) {
				return RejectRenderCommand(result,
					kRenderCommandReasonPipeControlStoreDataIndex,
					byteOffset + sizeof(uint32), flags);
			}
			if ((flags & kRenderPipeControlPostSyncMask) != 0) {
				if ((flags & kRenderPipeControlGlobalGtt) != 0) {
					return RejectRenderCommand(result,
						kRenderCommandReasonPipeControlGlobalGtt,
						byteOffset + sizeof(uint32), flags);
				}
				const uint32 addressWord = commands[index + 2];
				if ((addressWord & ~kRenderPipeControlAddressMask) != 0) {
					return RejectRenderCommand(result,
						kRenderCommandReasonPipeControlAddressAlignment,
						byteOffset + 2 * sizeof(uint32), addressWord);
				}
				const uint32 address
					= addressWord & kRenderPipeControlAddressMask;
				if (address < kPpgttPageBytes
					|| address >= kPpgttVirtualAddressBytes
					|| 8 > kPpgttVirtualAddressBytes - address) {
					return RejectRenderCommand(result,
						kRenderCommandReasonPipeControlAddressRange,
						byteOffset + 2 * sizeof(uint32), addressWord);
				}
			}
			result.pipeControlCount++;
		} else if (opcode == kRender3dPrimitiveOpcode)
			result.primitiveCount++;

		result.parsedCommandCount++;
		index += commandDwords;
	}

	return RejectRenderCommand(result, kRenderCommandReasonMissingEnd,
		static_cast<uint32>(byteCount), kRenderCommandInvalidDword);
}

} // namespace valleyview

#endif
