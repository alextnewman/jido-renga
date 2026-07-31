// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot (GPT-5.6 Sol)
#pragma once

#include "../../overlay/bin/jr_uvc_probe/core/UvcCore.h"

#include <initializer_list>


namespace jr::uvc::test {

inline DescriptorRecord
Record(uint8_t interfaceNumber, uint8_t subclass,
	std::initializer_list<uint8_t> bytes)
{
	DescriptorRecord record;
	record.interfaceNumber = interfaceNumber;
	record.alternateValue = 0;
	record.interfaceClass = 0x0e;
	record.interfaceSubclass = subclass;
	record.bytes = bytes;
	return record;
}


inline DescriptorRecord
Record(uint8_t interfaceNumber, uint8_t subclass,
	std::vector<uint8_t> bytes)
{
	DescriptorRecord record;
	record.interfaceNumber = interfaceNumber;
	record.alternateValue = 0;
	record.interfaceClass = 0x0e;
	record.interfaceSubclass = subclass;
	record.bytes = std::move(bytes);
	return record;
}


inline std::vector<uint8_t>
Frame(uint8_t subtype, uint8_t index, uint16_t width, uint16_t height,
	uint32_t maxFrame, std::initializer_list<uint32_t> intervals)
{
	std::vector<uint8_t> descriptor(26 + intervals.size() * 4);
	descriptor[0] = static_cast<uint8_t>(descriptor.size());
	descriptor[1] = 0x24;
	descriptor[2] = subtype;
	descriptor[3] = index;
	WriteLE16(&descriptor[5], width);
	WriteLE16(&descriptor[7], height);
	uint32_t shortest = 0;
	uint32_t longest = 0;
	for (uint32_t interval : intervals) {
		if (shortest == 0 || interval < shortest)
			shortest = interval;
		if (interval > longest)
			longest = interval;
	}
	const uint32_t minRate = longest != 0
		? static_cast<uint32_t>(
			static_cast<uint64_t>(width) * height * 16 * 10000000 / longest)
		: 0;
	const uint32_t maxRate = shortest != 0
		? static_cast<uint32_t>(
			static_cast<uint64_t>(width) * height * 16 * 10000000 / shortest)
		: 0;
	WriteLE32(&descriptor[9], minRate);
	WriteLE32(&descriptor[13], maxRate);
	WriteLE32(&descriptor[17], maxFrame);
	WriteLE32(&descriptor[21], shortest);
	descriptor[25] = static_cast<uint8_t>(intervals.size());
	size_t offset = 26;
	for (uint32_t interval : intervals) {
		WriteLE32(&descriptor[offset], interval);
		offset += 4;
	}
	return descriptor;
}


inline std::vector<DescriptorRecord>
WinkyDescriptors()
{
	std::vector<DescriptorRecord> records;
	records.push_back(Record(0, 1,
		{0x0d, 0x24, 0x01, 0x00, 0x01, 0x4e, 0x00, 0xc0, 0xe1,
			0xe4, 0x00, 0x01, 0x01}));
	records.push_back(Record(0, 1,
		{0x0b, 0x24, 0x05, 0x02, 0x01, 0x00, 0x00, 0x02, 0xfc,
			0x6b, 0x00}));
	records.push_back(Record(1, 2,
		{0x10, 0x24, 0x01, 0x03, 0xae, 0x04, 0x81, 0x00, 0x03,
			0x02, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00}));
	records.push_back(Record(1, 2,
		{0x1b, 0x24, 0x04, 0x01, 0x0a,
			0x59, 0x55, 0x59, 0x32, 0x00, 0x00, 0x10, 0x00,
			0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71,
			0x10, 0x01, 0x00, 0x00, 0x00, 0x00}));

	const uint16_t widths[] = {640, 160, 320, 320, 352, 424, 640, 848, 960,
		1280};
	const uint16_t heights[] = {480, 120, 180, 240, 288, 240, 360, 480, 540,
		720};
	for (uint8_t index = 0; index < 10; index++) {
		if (index < 7) {
			records.push_back(Record(1, 2, Frame(0x05, index + 1,
				widths[index], heights[index], widths[index] * heights[index] * 2,
				{333333, 666666})));
		} else if (index < 9) {
			records.push_back(Record(1, 2, Frame(0x05, index + 1,
				widths[index], heights[index], widths[index] * heights[index] * 2,
				{500000, 666666, 1000000})));
		} else {
			records.push_back(Record(1, 2, Frame(0x05, index + 1,
				widths[index], heights[index], widths[index] * heights[index] * 2,
				{1000000})));
		}
	}
	records.push_back(Record(1, 2,
		{0x06, 0x24, 0x0d, 0x01, 0x01, 0x04}));
	records.push_back(Record(1, 2,
		{0x0b, 0x24, 0x06, 0x02, 0x0a, 0x00, 0x01, 0x00,
			0x00, 0x00, 0x00}));
	for (uint8_t index = 0; index < 10; index++) {
		records.push_back(Record(1, 2, Frame(0x07, index + 1,
			widths[index], heights[index], widths[index] * heights[index] * 2,
			{333333, 666666})));
	}
	records.push_back(Record(1, 2,
		{0x06, 0x24, 0x0d, 0x01, 0x01, 0x04}));

	records.push_back(Record(1, 2,
		{0x38, 0x24, 0x0e, 0x03, 0x0a,
			'M', '4', '2', '0', 0x00, 0x00, 0x10, 0x00,
			0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71,
			'2', 'V', 'U', 'Y', 0x00, 0x00, 0x10, 0x00,
			0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71,
			'3', 'V', 'U', 'Y', 0x00, 0x00, 0x10, 0x00,
			0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71,
			0x00, 0x01, 0x00}));
	for (uint8_t index = 1; index <= 10; index++) {
		std::vector<uint8_t> vendor = Frame(0x0f, index, widths[index - 1],
			heights[index - 1], widths[index - 1] * heights[index - 1] * 2,
			{333333, 666666});
		records.push_back(Record(1, 2, std::move(vendor)));
	}
	return records;
}

} // namespace jr::uvc::test
