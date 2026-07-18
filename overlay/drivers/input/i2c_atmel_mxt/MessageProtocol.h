// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot
#ifndef _I2C_ATMEL_MXT_MESSAGE_PROTOCOL_H
#define _I2C_ATMEL_MXT_MESSAGE_PROTOCOL_H


#include <SupportDefs.h>


struct mxt_message_read_step {
	uint16	address;
	size_t	length;
	bool	includesCount;
};


// T44 and the first T5 message are contiguous. Every later message is a new
// read from the T5 FIFO address; it is not ordinary memory after that message.
static inline mxt_message_read_step
MxtMessageReadStep(uint8 messageIndex, uint16 t44Address, uint16 t5Address,
	uint8 messageSize)
{
	mxt_message_read_step step;
	step.address = messageIndex == 0 ? t44Address : t5Address;
	step.length = messageSize + (messageIndex == 0 ? 1 : 0);
	step.includesCount = messageIndex == 0;
	return step;
}


#endif	// _I2C_ATMEL_MXT_MESSAGE_PROTOCOL_H
