// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with Qwen 3.6
#ifndef _I2C_ATMEL_MXT_OBJECT_TABLE_H
#define _I2C_ATMEL_MXT_OBJECT_TABLE_H


#include <SupportDefs.h>
#include <KernelExport.h>


// Maximum number of object types in the table
#define MXT_MAX_OBJECT_TYPES	64

// Object entry structure (6 bytes each)
typedef struct {
	uint8	type;			// Object type (1-255)
	uint16	startAddress;	// Start address (host order; little-endian on the wire)
	uint8	size;			// Size in bytes (stored as size_minus_one)
	uint8	instances;		// Instance count (stored as instances_minus_one)
	uint8	numReportIDs;	// Number of report IDs
} mxt_object_entry;


// Information block (7 bytes from address 0x0000)
typedef struct {
	uint8	familyID;		// Family ID
	uint8	variantID;		// Variant ID
	uint8	version;		// Version (major nibble high, minor nibble low)
	uint8	build;			// Build number
	uint8	xSize;			// Matrix X size
	uint8	ySize;			// Matrix Y size
	uint8	numObjects;		// Number of objects in object table
} mxt_info_block;


// CRC-24 context
typedef struct {
	uint32	crc;
	uint32	bytesProcessed;	// tracks pair alignment for spec Section 10.3
} mxt_crc24_context;


class ObjectTable {
public:
								ObjectTable();
								~ObjectTable();

			status_t			Initialize(uint8* data, size_t length);
			status_t			InitCheck() const { return fStatus; }

			const mxt_object_entry*	FindObject(uint8 type) const;
			uint32				GetStartAddress(uint8 type) const;
			uint8				GetSize(uint8 type) const;
			uint8				GetInstances(uint8 type) const;
			uint8				GetNumReportIDs(uint8 type) const;
			bool				HasObject(uint8 type) const;

			size_t				CountObjects() const { return fObjectCount; }
			const mxt_object_entry*	ObjectAt(size_t index) const;

			uint32				CalculateMemorySize() const;
			status_t			VerifyCRC(uint8* checksum, size_t length) const;

private:
	status_t				_ParseEntries(uint8* data, size_t length);
	uint32				_CRC24(uint8* data, size_t length) const;

private:
			status_t			fStatus;
			mxt_object_entry	fObjects[MXT_MAX_OBJECT_TYPES];
			size_t				fObjectCount;
};


// CRC-24 utility functions
status_t					mxt_crc24_init(mxt_crc24_context* context);
status_t					mxt_crc24_update(mxt_crc24_context* context, uint8 byte);
uint32					mxt_crc24_finish(mxt_crc24_context* context);
uint32					mxt_crc24_compute(uint8* data, size_t length);


#endif	// _I2C_ATMEL_MXT_OBJECT_TABLE_H
