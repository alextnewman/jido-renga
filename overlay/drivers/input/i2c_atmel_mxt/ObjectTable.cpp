// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with Qwen 3.6

//! Object table parsing for Atmel maXTouch controllers.


#include "Driver.h"
#include "ObjectTable.h"

#include <new>
#include <string.h>


ObjectTable::ObjectTable()
	:	fStatus(B_NO_INIT),
		fObjectCount(0)
{
	memset(fObjects, 0, sizeof(fObjects));
}


ObjectTable::~ObjectTable()
{
}


status_t
ObjectTable::Initialize(uint8* data, size_t length)
{
	if (data == NULL || length < 6) {
		ERROR("invalid object table data: data=%p, length=%zu\n", data, length);
		return B_BAD_DATA;
	}

	// Parse raw object table entries (6 bytes each)
	fStatus = _ParseEntries(data, length);
	if (fStatus != B_OK) {
		ERROR("failed to parse object table entries\n");
		return fStatus;
	}

	TRACE("Object table initialized: %zu objects\n", fObjectCount);
	return B_OK;
}


status_t
ObjectTable::_ParseEntries(uint8* data, size_t length)
{
	if (length % 6 != 0) {
		ERROR("object table length %zu is not a multiple of 6\n", length);
		return B_BAD_DATA;
	}

	fObjectCount = length / 6;
	if (fObjectCount > MXT_MAX_OBJECT_TYPES) {
		ERROR("object count %zu exceeds maximum %d\n", fObjectCount, MXT_MAX_OBJECT_TYPES);
		return B_BAD_DATA;
	}

	for (size_t i = 0; i < fObjectCount; i++) {
		uint8* entry = data + (i * 6);

		fObjects[i].type = entry[0];
		// Object table entries are little-endian per the Windows driver.
		// entry[1] is low byte, entry[2] is high byte — convert to host order.
		fObjects[i].startAddress = entry[1] | (entry[2] << 8);
		fObjects[i].size = entry[3] + 1;
		fObjects[i].instances = entry[4] + 1;
		fObjects[i].numReportIDs = entry[5];

		TRACE("Object %" B_PRIu32 ": type=%u, addr=0x%04x, size=%u, instances=%u, reportIDs=%u\n",
			(uint32)i, fObjects[i].type, fObjects[i].startAddress,
			fObjects[i].size, fObjects[i].instances, fObjects[i].numReportIDs);
	}

	return B_OK;
}


const mxt_object_entry*
ObjectTable::FindObject(uint8 type) const
{
	for (size_t i = 0; i < fObjectCount; i++) {
		if (fObjects[i].type == type)
			return &fObjects[i];
	}
	return NULL;
}


uint32
ObjectTable::GetStartAddress(uint8 type) const
{
	const mxt_object_entry* obj = FindObject(type);
	if (obj == NULL)
		return 0;
	return obj->startAddress;
}


uint8
ObjectTable::GetSize(uint8 type) const
{
	const mxt_object_entry* obj = FindObject(type);
	if (obj == NULL)
		return 0;
	return obj->size;
}


uint8
ObjectTable::GetInstances(uint8 type) const
{
	const mxt_object_entry* obj = FindObject(type);
	if (obj == NULL)
		return 0;
	return obj->instances;
}


uint8
ObjectTable::GetNumReportIDs(uint8 type) const
{
	const mxt_object_entry* obj = FindObject(type);
	if (obj == NULL)
		return 0;
	return obj->numReportIDs;
}


bool
ObjectTable::HasObject(uint8 type) const
{
	return FindObject(type) != NULL;
}


const mxt_object_entry*
ObjectTable::ObjectAt(size_t index) const
{
	if (index >= fObjectCount)
		return NULL;
	return &fObjects[index];
}


uint32
ObjectTable::CalculateMemorySize() const
{
	uint32 maxEnd = 0;

	for (size_t i = 0; i < fObjectCount; i++) {
		uint32 endAddress = fObjects[i].startAddress +
			(fObjects[i].size * fObjects[i].instances) - 1;
		if (endAddress > maxEnd)
			maxEnd = endAddress;
	}

	return maxEnd + 1;
}


status_t
ObjectTable::VerifyCRC(uint8* checksum, size_t length) const
{
	if (checksum == NULL || length < 3) {
		ERROR("invalid checksum data\n");
		return B_BAD_DATA;
	}

	uint32 readCRC = checksum[0] | (checksum[1] << 8) | (checksum[2] << 16);

	// Compute CRC over the object table entries
	uint32 computedCRC = mxt_crc24_compute((uint8*)fObjects, fObjectCount * sizeof(mxt_object_entry));

	TRACE("CRC check: read=0x%06x\n", readCRC);
	TRACE("CRC check: computed=0x%06x\n", computedCRC);

	if (readCRC != computedCRC) {
		ERROR("CRC mismatch!\n");
		return B_ERROR;
	}

	TRACE("CRC verified OK\n");
	return B_OK;
}


static uint32
_crc24_bytes(uint8* data, size_t length)
{
	// CRC-24 per spec Section 10.3: pair-based algorithm.
	// Polynomial: 0x001B0001 (reflected form of Atmel's 0x80001B).
	// Bytes are processed in pairs as big-endian words.
	uint32 crc = 0;

	for (size_t i = 0; i < length; i += 2) {
		uint16 word = data[i];
		if (i + 1 < length)
			word |= data[i + 1] << 8;

		crc = ((crc << 1) ^ word);
		if (crc & 0x1000000)
			crc ^= 0x001B0001;
		crc &= 0xFFFFFF;
	}

	return crc;
}


uint32
ObjectTable::_CRC24(uint8* data, size_t length) const
{
	return _crc24_bytes(data, length);
}


// CRC-24 utility function implementations
status_t
mxt_crc24_init(mxt_crc24_context* context)
{
	if (context == NULL)
		return B_BAD_DATA;

	context->crc = 0;
	context->bytesProcessed = 0;
	return B_OK;
}


status_t
mxt_crc24_update(mxt_crc24_context* context, uint8 byte)
{
	if (context == NULL)
		return B_BAD_DATA;

	context->bytesProcessed++;

	// Process bytes in pairs per spec Section 10.3.
	// First byte of pair: accumulate into crc high byte.
	// Second byte of pair: complete word and process.
	if (context->bytesProcessed & 1) {
		// Odd position — first byte of pair, store in high byte.
		context->crc ^= (uint32)byte << 16;
	} else {
		// Even position — second byte, complete word and process.
		uint16 word = ((context->crc >> 16) & 0xff) | (byte << 8);
		context->crc = ((context->crc << 1) ^ word);
		if (context->crc & 0x1000000)
			context->crc ^= 0x001B0001;
		context->crc &= 0xFFFFFF;
	}

	return B_OK;
}


uint32
mxt_crc24_finish(mxt_crc24_context* context)
{
	if (context == NULL)
		return 0;

	// If odd byte count, the pending first byte was zero-padded (spec 10.3).
	if (context->bytesProcessed & 1) {
		uint16 word = (context->crc >> 16) & 0xff;
		context->crc = ((context->crc << 1) ^ word);
		if (context->crc & 0x1000000)
			context->crc ^= 0x001B0001;
		context->crc &= 0xFFFFFF;
	}

	return context->crc;
}


uint32
mxt_crc24_compute(uint8* data, size_t length)
{
	return _crc24_bytes(data, length);
}
