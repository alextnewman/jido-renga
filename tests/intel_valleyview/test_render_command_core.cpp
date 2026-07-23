// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot

#include "framework/jr_test.h"

#include <common/intel_valleyview/RenderCommandCore.h>


using namespace valleyview;


static void
AppendPacket(uint32* commands, size_t& count, uint32 header,
	uint32 commandDwords)
{
	commands[count++] = header;
	for (uint32 index = 1; index < commandDwords; index++)
		commands[count++] = 0;
}


static void
AppendLri(uint32* commands, size_t& count, uint32 reg, uint32 value)
{
	commands[count++] = 0x11000001;
	commands[count++] = reg;
	commands[count++] = value;
}


static RenderCommandResult
ParsePipeControl(uint32 flags, uint32 address)
{
	const uint32 commands[] = {
		0x7a000003, flags, address, 0, 0, kRenderMiBatchBufferEnd
	};
	return ParseRenderCommands(commands, sizeof(commands));
}


JR_TEST(intel_valleyview_render_command, accepts_representative_crocus_batch)
{
	uint32 commands[128] = {};
	size_t count = 0;
	commands[count++] = 0;
	AppendLri(commands, count, 0x0000b010, 0x00d30000);
	AppendLri(commands, count, 0x0000b020, 0x00880038);
	AppendLri(commands, count, 0x0000b024, 0);
	AppendLri(commands, count, 0x000020c0, 0x00400040);

	AppendPacket(commands, count, 0x7a000003, 5);
	commands[count - 4] = 1u << 14;
	commands[count - 3] = 0x00001000;
	AppendPacket(commands, count, 0x69040000, 1);
	AppendPacket(commands, count, 0x61010008, 10);
	AppendPacket(commands, count, 0x78080003, 5);
	AppendPacket(commands, count, 0x78090003, 5);
	AppendPacket(commands, count, 0x680b0001, 1);
	AppendPacket(commands, count, 0x78100004, 6);
	AppendPacket(commands, count, 0x78200006, 8);
	AppendPacket(commands, count, 0x78130005, 7);
	AppendPacket(commands, count, 0x78140001, 3);
	AppendPacket(commands, count, 0x781f000c, 14);
	AppendPacket(commands, count, 0x7b000005, 7);
	commands[count++] = kRenderMiBatchBufferEnd;
	commands[count++] = 0;
	commands[count++] = 0;

	const RenderCommandResult result
		= ParseRenderCommands(commands, count * sizeof(uint32));
	JR_CHECK_EQ(result.status, kRenderCommandStatusAccepted);
	JR_CHECK_EQ(result.reason, kRenderCommandReasonNone);
	JR_CHECK_EQ(result.failingByteOffset, kRenderCommandInvalidOffset);
	JR_CHECK_EQ(result.failingDword, kRenderCommandInvalidDword);
	JR_CHECK_EQ(result.parsedCommandCount, 18u);
	JR_CHECK_EQ(result.lriRegisterCount, 4u);
	JR_CHECK_EQ(result.pipeControlCount, 1u);
	JR_CHECK_EQ(result.primitiveCount, 1u);
	JR_CHECK(result.sawEnd);
}


JR_TEST(intel_valleyview_render_command, accepts_compute_l3_configuration)
{
	const uint32 commands[] = {
		0x11000001, 0x0000b020, 0x01040011,
		kRenderMiBatchBufferEnd
	};
	const RenderCommandResult result
		= ParseRenderCommands(commands, sizeof(commands));
	JR_CHECK_EQ(result.status, kRenderCommandStatusAccepted);
	JR_CHECK_EQ(result.lriRegisterCount, 1u);
	JR_CHECK_EQ(result.parsedCommandCount, 2u);
	JR_CHECK(result.sawEnd);
}


JR_TEST(intel_valleyview_render_command, accepts_exact_corpus_render_opcodes)
{
	const uint32 corpusHeaders[] = {
		0x7a000003, 0x69040002, 0x61010008, 0x61020000, 0x7b000005,
		0x680b0001, 0x78080003, 0x78090003,
		0x78300000, 0x78310000, 0x78320000, 0x78330000,
		0x78240000, 0x780e0000, 0x78250000,
		0x78150005, 0x78190005, 0x781a0005, 0x78160005, 0x78170005,
		0x790d0002, 0x78180000,
		0x78100004, 0x781b0005, 0x781c0002, 0x781d0004,
		0x781e0001, 0x78110005, 0x78120002, 0x78130005,
		0x781f000c, 0x78140001, 0x78200006,
		0x78230000, 0x78210000,
		0x78260000, 0x78270000, 0x78280000, 0x78290000, 0x782a0000,
		0x782b0000, 0x782f0000,
		0x78050005, 0x78060001, 0x78070001, 0x78040001, 0x780f0000,
		0x790a0001, 0x79060000, 0x7907001f,
		0x79120000, 0x79130000, 0x79140000, 0x79150000, 0x79160000,
		0x79080001, 0x79000002
	};

	for (size_t index = 0;
		index < sizeof(corpusHeaders) / sizeof(corpusHeaders[0]); index++) {
		uint32 commands[40] = {};
		size_t count = 0;
		const uint32 header = corpusHeaders[index];
		const uint32 opcode = header & 0xffff0000;
		const uint32 commandDwords
			= opcode == kRenderPipelineSelectOpcode
				|| opcode == kRenderVfStatisticsOpcode
				? 1 : (header & 0xff) + 2;
		AppendPacket(commands, count, header, commandDwords);
		commands[count++] = kRenderMiBatchBufferEnd;

		const RenderCommandResult result
			= ParseRenderCommands(commands, count * sizeof(uint32));
		JR_CHECK_EQ(result.status, kRenderCommandStatusAccepted);
		JR_CHECK_EQ(result.parsedCommandCount, 2u);
		JR_CHECK(result.sawEnd);
	}
}


JR_TEST(intel_valleyview_render_command, rejects_forbidden_mi_commands)
{
	const uint32 forbidden[] = {
		0x01000000, 0x10000002, 0x12000001, 0x13000001
	};
	for (size_t index = 0;
		index < sizeof(forbidden) / sizeof(forbidden[0]); index++) {
		const uint32 commands[] = {forbidden[index], kRenderMiBatchBufferEnd};
		const RenderCommandResult result
			= ParseRenderCommands(commands, sizeof(commands));
		JR_CHECK_EQ(result.status, kRenderCommandStatusRejected);
		JR_CHECK_EQ(result.reason, kRenderCommandReasonForbiddenMi);
		JR_CHECK_EQ(result.failingByteOffset, 0u);
		JR_CHECK_EQ(result.failingDword, forbidden[index]);
		JR_CHECK_EQ(result.parsedCommandCount, 0u);
	}

	const uint32 partialLri[] = {
		0x11000101, 0x0000b010, 0x00d30000, kRenderMiBatchBufferEnd
	};
	const RenderCommandResult result
		= ParseRenderCommands(partialLri, sizeof(partialLri));
	JR_CHECK_EQ(result.reason, kRenderCommandReasonForbiddenMi);
	JR_CHECK_EQ(result.failingDword, 0x11000101u);
}


JR_TEST(intel_valleyview_render_command, rejects_nested_batches)
{
	const uint32 commands[] = {
		0, 0x18800000, 0x00001000, kRenderMiBatchBufferEnd
	};
	const RenderCommandResult result
		= ParseRenderCommands(commands, sizeof(commands));
	JR_CHECK_EQ(result.reason, kRenderCommandReasonNestedBatch);
	JR_CHECK_EQ(result.failingByteOffset, 4u);
	JR_CHECK_EQ(result.failingDword, 0x18800000u);
	JR_CHECK_EQ(result.parsedCommandCount, 1u);
	JR_CHECK(!result.sawEnd);
}


JR_TEST(intel_valleyview_render_command, rejects_blt_and_unknown_types)
{
	const uint32 blt[] = {0x54300004, kRenderMiBatchBufferEnd};
	RenderCommandResult result = ParseRenderCommands(blt, sizeof(blt));
	JR_CHECK_EQ(result.reason, kRenderCommandReasonBlt);
	JR_CHECK_EQ(result.failingDword, 0x54300004u);

	const uint32 unknownType[] = {0x20000000, kRenderMiBatchBufferEnd};
	result = ParseRenderCommands(unknownType, sizeof(unknownType));
	JR_CHECK_EQ(result.reason, kRenderCommandReasonUnknownType);
	JR_CHECK_EQ(result.failingByteOffset, 0u);
	JR_CHECK_EQ(result.failingDword, 0x20000000u);
}


JR_TEST(intel_valleyview_render_command, rejects_unknown_render_opcodes)
{
	const uint32 commands[] = {0x7f7f0000, kRenderMiBatchBufferEnd};
	const RenderCommandResult result
		= ParseRenderCommands(commands, sizeof(commands));
	JR_CHECK_EQ(result.reason, kRenderCommandReasonUnknownRenderOpcode);
	JR_CHECK_EQ(result.failingByteOffset, 0u);
	JR_CHECK_EQ(result.failingDword, 0x7f7f0000u);
}


JR_TEST(intel_valleyview_render_command, rejects_malformed_command_lengths)
{
	const uint32 malformed[] = {
		0x11000000, 0x7a000002, 0x7b000004
	};
	for (size_t index = 0;
		index < sizeof(malformed) / sizeof(malformed[0]); index++) {
		const uint32 commands[] = {malformed[index], kRenderMiBatchBufferEnd};
		const RenderCommandResult result
			= ParseRenderCommands(commands, sizeof(commands));
		JR_CHECK_EQ(result.reason, kRenderCommandReasonMalformedLength);
		JR_CHECK_EQ(result.failingByteOffset, 0u);
		JR_CHECK_EQ(result.failingDword, malformed[index]);
	}
}


JR_TEST(intel_valleyview_render_command, rejects_truncated_commands)
{
	const uint32 commands[] = {0x61010008, 0};
	const RenderCommandResult result
		= ParseRenderCommands(commands, sizeof(commands));
	JR_CHECK_EQ(result.reason, kRenderCommandReasonTruncatedCommand);
	JR_CHECK_EQ(result.failingByteOffset, 0u);
	JR_CHECK_EQ(result.failingDword, 0x61010008u);
	JR_CHECK_EQ(result.parsedCommandCount, 0u);
}


JR_TEST(intel_valleyview_render_command, rejects_unapproved_lri_registers)
{
	const uint32 commands[] = {
		0x11000001, 0x00007000, 0, kRenderMiBatchBufferEnd
	};
	const RenderCommandResult result
		= ParseRenderCommands(commands, sizeof(commands));
	JR_CHECK_EQ(result.reason, kRenderCommandReasonLriRegister);
	JR_CHECK_EQ(result.failingByteOffset, 4u);
	JR_CHECK_EQ(result.failingDword, 0x00007000u);
	JR_CHECK_EQ(result.lriRegisterCount, 0u);
}


JR_TEST(intel_valleyview_render_command, rejects_unapproved_lri_values)
{
	const uint32 commands[] = {
		0x11000001, 0x0000b020, 0x00880039, kRenderMiBatchBufferEnd
	};
	const RenderCommandResult result
		= ParseRenderCommands(commands, sizeof(commands));
	JR_CHECK_EQ(result.reason, kRenderCommandReasonLriValue);
	JR_CHECK_EQ(result.failingByteOffset, 8u);
	JR_CHECK_EQ(result.failingDword, 0x00880039u);
	JR_CHECK_EQ(result.lriRegisterCount, 0u);
}


JR_TEST(intel_valleyview_render_command, rejects_pipe_control_side_channels)
{
	struct Case {
		uint32				flags;
		RenderCommandReason	reason;
	};
	const Case cases[] = {
		{1u << 23, kRenderCommandReasonPipeControlMmioWrite},
		{1u << 8, kRenderCommandReasonPipeControlNotify},
		{1u << 21, kRenderCommandReasonPipeControlStoreDataIndex}
	};
	for (size_t index = 0; index < sizeof(cases) / sizeof(cases[0]); index++) {
		const RenderCommandResult result
			= ParsePipeControl(cases[index].flags, 0);
		JR_CHECK_EQ(result.reason, cases[index].reason);
		JR_CHECK_EQ(result.failingByteOffset, 4u);
		JR_CHECK_EQ(result.failingDword, cases[index].flags);
		JR_CHECK_EQ(result.pipeControlCount, 0u);
	}
}


JR_TEST(intel_valleyview_render_command, rejects_pipe_control_ggtt_writes)
{
	const uint32 flags = (1u << 14) | (1u << 24);
	const RenderCommandResult result = ParsePipeControl(flags, 0x1000);
	JR_CHECK_EQ(result.reason, kRenderCommandReasonPipeControlGlobalGtt);
	JR_CHECK_EQ(result.failingByteOffset, 4u);
	JR_CHECK_EQ(result.failingDword, flags);
	JR_CHECK_EQ(result.pipeControlCount, 0u);
}


JR_TEST(intel_valleyview_render_command, rejects_pipe_control_bad_addresses)
{
	struct Case {
		uint32				address;
		RenderCommandReason	reason;
	};
	const Case cases[] = {
		{0, kRenderCommandReasonPipeControlAddressRange},
		{4, kRenderCommandReasonPipeControlAddressRange},
		{0x0ffc, kRenderCommandReasonPipeControlAddressRange},
		{0x1001, kRenderCommandReasonPipeControlAddressAlignment},
		{0x7ffffffc, kRenderCommandReasonPipeControlAddressRange},
		{0x7fffffff, kRenderCommandReasonPipeControlAddressAlignment},
		{0x80000000, kRenderCommandReasonPipeControlAddressRange}
	};
	for (size_t index = 0; index < sizeof(cases) / sizeof(cases[0]); index++) {
		const RenderCommandResult result
			= ParsePipeControl(1u << 14, cases[index].address);
		JR_CHECK_EQ(result.reason, cases[index].reason);
		JR_CHECK_EQ(result.failingByteOffset, 8u);
		JR_CHECK_EQ(result.failingDword, cases[index].address);
	}
}


JR_TEST(intel_valleyview_render_command, accepts_pipe_control_ppgtt_write_range)
{
	const uint32 addresses[] = {0x1000, 0x7ffffff8};
	for (size_t index = 0;
		index < sizeof(addresses) / sizeof(addresses[0]); index++) {
		const RenderCommandResult result
			= ParsePipeControl(1u << 14, addresses[index]);
		JR_CHECK_EQ(result.status, kRenderCommandStatusAccepted);
		JR_CHECK_EQ(result.reason, kRenderCommandReasonNone);
		JR_CHECK_EQ(result.pipeControlCount, 1u);
	}
}


JR_TEST(intel_valleyview_render_command, requires_end_and_zero_padding)
{
	const uint32 missingEnd[] = {0};
	RenderCommandResult result
		= ParseRenderCommands(missingEnd, sizeof(missingEnd));
	JR_CHECK_EQ(result.reason, kRenderCommandReasonMissingEnd);
	JR_CHECK_EQ(result.failingByteOffset, 4u);
	JR_CHECK_EQ(result.failingDword, kRenderCommandInvalidDword);
	JR_CHECK_EQ(result.parsedCommandCount, 1u);
	JR_CHECK(!result.sawEnd);

	const uint32 trailing[] = {kRenderMiBatchBufferEnd, 0, 0xdeadbeef};
	result = ParseRenderCommands(trailing, sizeof(trailing));
	JR_CHECK_EQ(result.reason, kRenderCommandReasonTrailingData);
	JR_CHECK_EQ(result.failingByteOffset, 8u);
	JR_CHECK_EQ(result.failingDword, 0xdeadbeefu);
	JR_CHECK_EQ(result.parsedCommandCount, 1u);
	JR_CHECK(result.sawEnd);
}


JR_TEST(intel_valleyview_render_command, rejects_invalid_batch_inputs)
{
	uint32 command = kRenderMiBatchBufferEnd;
	RenderCommandResult result = ParseRenderCommands(NULL, sizeof(command));
	JR_CHECK_EQ(result.reason, kRenderCommandReasonNullBatch);
	JR_CHECK_EQ(result.failingByteOffset, kRenderCommandInvalidOffset);

	result = ParseRenderCommands(&command, 0);
	JR_CHECK_EQ(result.reason, kRenderCommandReasonEmptyBatch);
	JR_CHECK_EQ(result.failingByteOffset, 0u);

	result = ParseRenderCommands(&command, kRenderCommandMaxBatchBytes + 4);
	JR_CHECK_EQ(result.reason, kRenderCommandReasonOversizedBatch);
	JR_CHECK_EQ(result.failingByteOffset, 0u);

	result = ParseRenderCommands(&command, sizeof(command) - 1);
	JR_CHECK_EQ(result.reason, kRenderCommandReasonUnalignedBatch);
	JR_CHECK_EQ(result.failingByteOffset, 0u);

	alignas(uint32) uint8 bytes[2 * sizeof(uint32)] = {};
	result = ParseRenderCommands(bytes + 1, sizeof(uint32));
	JR_CHECK_EQ(result.reason, kRenderCommandReasonUnalignedBatch);
	JR_CHECK_EQ(result.failingByteOffset, 0u);

	uint32 maxBatch[kRenderCommandMaxBatchBytes / sizeof(uint32)] = {};
	maxBatch[0] = kRenderMiBatchBufferEnd;
	result = ParseRenderCommands(maxBatch, sizeof(maxBatch));
	JR_CHECK_EQ(result.status, kRenderCommandStatusAccepted);
	JR_CHECK(result.sawEnd);
}
