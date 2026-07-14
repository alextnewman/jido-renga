// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot

//! Host-side tests for the Atmel maXTouch object-table parser.
//
// ObjectTable is the boot-critical discovery mechanism: it turns the raw object
// table read over I2C into the register map the rest of the driver relies on
// (T5 message processor, T7 power, T100 touch, T19 GPIO, ...). If parsing is
// wrong, every subsequent register access is wrong, so it is worth pinning.
//
// The CRC-24 routines are characterization only. Runtime does not use them, and
// the batch and streaming implementations disagree. These tests lock current
// behavior without claiming that either value is hardware-correct.

#include "framework/jr_test.h"

#include "ObjectTable.h"


namespace {

// Drive the streaming CRC API over a complete buffer.
uint32
StreamingCrc(uint8* data, size_t length)
{
	mxt_crc24_context ctx;
	mxt_crc24_init(&ctx);
	for (size_t i = 0; i < length; i++)
		mxt_crc24_update(&ctx, data[i]);
	return mxt_crc24_finish(&ctx);
}

}	// namespace


JR_TEST(object_table, parses_entries)
{
	// Three synthetic 6-byte entries: { type, addrLo, addrHi, size-1, inst-1,
	// numReportIDs }. Start address is little-endian; size/instances are stored
	// biased by one on the wire.
	uint8 table[] = {
		7,   0xAB, 0x00, 3, 0, 0,	// T7   @0x00AB size 4  inst 1  rid 0
		100, 0x23, 0x01, 9, 1, 8,	// T100 @0x0123 size 10 inst 2  rid 8
		5,   0x10, 0x00, 4, 0, 1,	// T5   @0x0010 size 5  inst 1  rid 1
	};

	ObjectTable ot;
	JR_CHECK_EQ(ot.Initialize(table, sizeof(table)), B_OK);
	JR_CHECK_EQ(ot.InitCheck(), B_OK);
	JR_CHECK_EQ(ot.CountObjects(), (size_t)3);

	JR_CHECK(ot.HasObject(100));
	JR_CHECK(!ot.HasObject(200));

	// Little-endian decode: 0x23 | (0x01 << 8) == 0x0123.
	JR_CHECK_EQ(ot.GetStartAddress(100), (uint32)0x0123);
	JR_CHECK_EQ(ot.GetSize(100), (uint8)10);		// stored 9, +1
	JR_CHECK_EQ(ot.GetInstances(100), (uint8)2);		// stored 1, +1
	JR_CHECK_EQ(ot.GetNumReportIDs(100), (uint8)8);

	JR_CHECK_EQ(ot.GetStartAddress(7), (uint32)0x00AB);
	JR_CHECK_EQ(ot.GetSize(7), (uint8)4);
	JR_CHECK_EQ(ot.GetInstances(7), (uint8)1);

	// Ordering is preserved and ObjectAt is bounds-checked.
	const mxt_object_entry* first = ot.ObjectAt(0);
	JR_CHECK(first != NULL);
	JR_CHECK_EQ(first->type, (uint8)7);
	JR_CHECK(ot.ObjectAt(3) == NULL);

	// Absent object -> null find and zero-valued accessors.
	JR_CHECK(ot.FindObject(200) == NULL);
	JR_CHECK_EQ(ot.GetStartAddress(200), (uint32)0);
	JR_CHECK_EQ(ot.GetSize(200), (uint8)0);
	JR_CHECK_EQ(ot.GetInstances(200), (uint8)0);
	JR_CHECK_EQ(ot.GetNumReportIDs(200), (uint8)0);
}


JR_TEST(object_table, memory_size_spans_highest_object)
{
	uint8 table[] = {
		7,   0xAB, 0x00, 3, 0, 0,	// end = 0x00AB + 4*1 - 1 = 0x00AE
		100, 0x23, 0x01, 9, 1, 8,	// end = 0x0123 + 10*2 - 1 = 0x0136
		5,   0x10, 0x00, 4, 0, 1,	// end = 0x0010 + 5*1 - 1 = 0x0014
	};

	ObjectTable ot;
	JR_CHECK_EQ(ot.Initialize(table, sizeof(table)), B_OK);
	// Highest end address is 0x0136; memory size is that + 1.
	JR_CHECK_EQ(ot.CalculateMemorySize(), (uint32)0x0137);
}


JR_TEST(object_table, rejects_malformed_tables)
{
	uint8 buf[12] = { 0 };

	ObjectTable a;
	JR_CHECK_EQ(a.Initialize(NULL, 6), B_BAD_DATA);		// null data

	ObjectTable b;
	JR_CHECK_EQ(b.Initialize(buf, 5), B_BAD_DATA);		// shorter than one entry

	ObjectTable c;
	JR_CHECK_EQ(c.Initialize(buf, 7), B_BAD_DATA);		// not a multiple of 6

	// A single well-formed entry is the minimum valid table.
	uint8 one[6] = { 6, 0x00, 0x00, 0, 0, 0 };
	ObjectTable d;
	JR_CHECK_EQ(d.Initialize(one, sizeof(one)), B_OK);
	JR_CHECK_EQ(d.CountObjects(), (size_t)1);
	JR_CHECK(d.HasObject(6));
}


JR_TEST(object_table, crc24_batch_is_stable)
{
	// Lock the batch routine's current output without treating it as a protocol
	// reference value.
	uint8 data[] = { 0x01, 0x02, 0x03, 0x04 };

	uint32 even = mxt_crc24_compute(data, sizeof(data));
	JR_CHECK_EQ(even, (uint32)0x000001);
	JR_CHECK_EQ(mxt_crc24_compute(data, sizeof(data)), even);	// deterministic
	JR_CHECK(even <= 0xFFFFFF);					// stays 24-bit

	// Odd length exercises the zero-pad branch and yields a distinct result.
	uint32 odd = mxt_crc24_compute(data, 3);
	JR_CHECK_EQ(odd, (uint32)0x000401);
	JR_CHECK(odd <= 0xFFFFFF);
	JR_CHECK_NE(odd, even);
}


JR_TEST(object_table, crc24_streaming_diverges_from_batch)
{
	// The unused streaming path does not reproduce the batch result. Keep the
	// discrepancy visible until both implementations can be checked against
	// controller data.
	uint8 data[] = { 0x01, 0x02, 0x03, 0x04 };

	uint32 batch = mxt_crc24_compute(data, sizeof(data));
	uint32 streamed = StreamingCrc(data, sizeof(data));

	JR_CHECK_EQ(batch, (uint32)0x000001);
	JR_CHECK_EQ(streamed, (uint32)0x020003);
	JR_CHECK_NE(batch, streamed);
}
