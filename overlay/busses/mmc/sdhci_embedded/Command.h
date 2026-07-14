// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot
#pragma once

#include <stdint.h>

#include "Types.h"

// SD/eMMC command vocabulary, controller quirks, and the pure policy that maps
// a command + quirk set to its retry/timeout constraints. No kernel deps.

namespace jr::sdhci {


// Command indices as they appear on the wire (SD Physical Layer 8.00 / JEDEC
// 84-B51). These are hardware-defined; the enum is just a readable spelling.
enum class Cmd : uint8_t {
	GoIdleState			= 0,
	MmcSendOpCond		= 1,	// eMMC CMD1
	AllSendCid			= 2,
	SendRelativeAddr	= 3,
	MmcSwitch			= 6,	// eMMC CMD6 (shares index 6 with ACMD6 SET_BUS_WIDTH)
	SelectDeselectCard	= 7,
	SendIfCond			= 8,	// SD CMD8 (shares index 8 with eMMC SEND_EXT_CSD)
	SendExtCsd			= 8,
	SendCsd				= 9,
	VoltageSwitch		= 11,	// SD CMD11
	StopTransmission	= 12,
	SendStatus			= 13,
	SetBlockLength		= 16,
	ReadSingleBlock		= 17,
	ReadMultipleBlocks	= 18,
	SdSendTuningBlock	= 19,
	MmcSendTuningBlock	= 21,
	WriteSingleBlock	= 24,
	WriteMultipleBlocks	= 25,
	EraseStart			= 32,
	EraseEnd			= 33,
	Erase				= 38,
	SdSendOpCond		= 41,	// ACMD41 (issued after a CMD55 prefix; no CMD41
								// conflict, so it lives here to reach the register)
	AppCmd				= 55,	// CMD55 prefix for application commands
};


// Application commands whose opcode collides with a normal command of the same
// index (so they cannot live in Cmd). Issued after a CMD55 prefix. ACMD41 has
// no such collision and lives in Cmd above.
enum class AppCmd : uint8_t {
	SetBusWidth			= 6,	// ACMD6 (collides with CMD6 MmcSwitch)
	SendScr				= 51,	// ACMD51
};


// The response shape a command expects. Named after the SD spec response types.
enum class ReplyType : uint8_t {
	None,
	R1,		// normal
	R1b,	// normal with busy
	R2,		// 136-bit (CID/CSD)
	R3,		// OCR
	R6,		// published RCA
	R7,		// card interface condition
};


// What the controller IS -- errata flags set by the platform layer from the
// MatchProfile. These describe the silicon, not what to do with any one
// command; the command policy below derives behavior from them.
enum class Quirk : uint32_t {
	None					= 0,
	BrokenPresetValues		= 1u << 0,	// preset registers non-functional
	PowerOnDelay			= 1u << 1,	// ~10ms settle after bus power on
	StopTransmissionBusy	= 1u << 2,	// CMD12 needs a busy response
	TimeoutClockFromSdClock	= 1u << 3,	// timeout counter rides the SD clock
	CardOnNeedsBusOn		= 1u << 4,	// full reset drops bus power
	EmmcHardwareReset		= 1u << 5,	// dedicated eMMC reset line
	NeedsIosfOcpFixup		= 1u << 6,	// IOSF-MBI OCP-timeout clear at probe
	ClockPllSequence		= 1u << 7,	// enable PLL between internal clock and SDCLK
	Fixed1MHzTimeoutClock	= 1u << 8,	// capability timeout field is unusable
};


constexpr Quirk
operator|(Quirk a, Quirk b) noexcept
{
	return static_cast<Quirk>(
		static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}


constexpr Quirk
operator&(Quirk a, Quirk b) noexcept
{
	return static_cast<Quirk>(
		static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}


// Test whether a quirk is present in a set.
constexpr bool
Has(Quirk set, Quirk flag) noexcept
{
	return (set & flag) != Quirk::None;
}


// Per-command execution policy, derived from the quirk set. Convergence (the
// worker-local retry loop) consumes this. It is intentionally a computed
// value, not a table: two controllers may want different policy for the same
// command index.
struct CommandConstraints {
	uint8_t		maxRetries = 0;			// extra attempts beyond the first
	uint16_t	timeoutMs = 0;			// per-attempt budget (0 = engine default)
	bool		validateOcr = false;	// filter spurious OCR responses
	bool		needsBusResetOnError = false;
	bool		replaySafe = true;		// false for writes with uncertain completion

	// Total attempts including the first.
	constexpr uint32_t Attempts() const noexcept { return maxRetries + 1u; }
};


// Derive the constraints for a command under a given quirk set. Pure function:
// exhaustively testable, and the single source of truth for retry behavior.
constexpr CommandConstraints
GetCommandConstraints(Cmd command, Quirk quirks) noexcept
{
	CommandConstraints c;

	switch (command) {
		case Cmd::MmcSendOpCond:
		case Cmd::SdSendOpCond:
			c.maxRetries = 3;
			c.validateOcr = true;
			break;

		case Cmd::AppCmd:
			// ACMD55 frequently times out on Bay Trail; allow a few retries.
			c.maxRetries = 3;
			c.timeoutMs = 200;
			break;

		case Cmd::StopTransmission:
			if (Has(quirks, Quirk::StopTransmissionBusy))
				c.needsBusResetOnError = true;
			break;

		case Cmd::SelectDeselectCard:
		case Cmd::Erase:
			if (Has(quirks, Quirk::CardOnNeedsBusOn))
				c.needsBusResetOnError = true;
			c.maxRetries = 1;
			c.timeoutMs = 2000;
			break;

		case Cmd::MmcSwitch:
			// eMMC CMD6 (R1b) shares the unreliable-busy problem. Without
			// retries a single spurious timeout leaves the card at 400kHz/1-bit.
			c.maxRetries = 1;
			c.timeoutMs = 2000;
			break;

		case Cmd::ReadSingleBlock:
		case Cmd::ReadMultipleBlocks:
			c.maxRetries = 2;
			c.timeoutMs = 5000;
			break;

		case Cmd::WriteSingleBlock:
		case Cmd::WriteMultipleBlocks:
			c.timeoutMs = 5000;
			c.replaySafe = false;
			break;

		default:
			break;
	}

	return c;
}


// SDHCI command-register response flags. The R3 OCR response carries neither
// CRC nor command-index fields, so enabling those checks makes valid CMD1/
// ACMD41 responses fail in hardware.
namespace command_flag {
	constexpr uint8_t kResp136		= 1u;
	constexpr uint8_t kResp48		= 2u;
	constexpr uint8_t kResp48Busy	= 3u;
	constexpr uint8_t kCheckCrc		= 1u << 3;
	constexpr uint8_t kCheckIndex	= 1u << 4;
}


constexpr uint8_t
ResponseFlags(ReplyType reply) noexcept
{
	using namespace command_flag;
	switch (reply) {
		case ReplyType::None:
			return 0;
		case ReplyType::R2:
			return kResp136 | kCheckCrc;
		case ReplyType::R1b:
			return kResp48Busy | kCheckCrc | kCheckIndex;
		case ReplyType::R3:
			return kResp48;
		case ReplyType::R1:
		case ReplyType::R6:
		case ReplyType::R7:
		default:
			return kResp48 | kCheckCrc | kCheckIndex;
	}
}


// SDHCI routes both data transfers and short-busy responses through the data
// line, so both command and data inhibit must clear before issue.
constexpr bool
RequiresDataLineIdle(bool dataPresent, ReplyType reply) noexcept
{
	return dataPresent || reply == ReplyType::R1b;
}


constexpr bool
R1HasError(uint32_t response) noexcept
{
	return (response & 0xfff9a080u) != 0;
}


// The SD OP_COND command (ACMD41) reuses the eMMC OP_COND retry policy. This
// helper lets the SD dialect ask for it without a second switch arm.
constexpr CommandConstraints
GetSdOpCondConstraints(Quirk quirks) noexcept
{
	return GetCommandConstraints(Cmd::SdSendOpCond, quirks);
}


// ---------------------------------------------------------------------------
// Transfer mode word computation (SDHCI Transfer Mode register, offset 0x0C).
// Bit layout is spec-defined; we mirror it as named constants and compute the
// word from a command. Pure and testable.
// ---------------------------------------------------------------------------

namespace transfer_mode {
	constexpr uint16_t kDmaEnable		= 1u << 0;
	constexpr uint16_t kBlockCountEnable= 1u << 1;
	constexpr uint16_t kAutoCmd12		= 1u << 2;
	constexpr uint16_t kAutoCmd23		= 2u << 2;
	constexpr uint16_t kRead			= 1u << 4;	// 1 = card->host
	constexpr uint16_t kMulti			= 1u << 5;	// multi-block
} // namespace transfer_mode


// Returns the Transfer Mode register value for a data command, or 0 for
// commands with no data phase.
constexpr uint16_t
ComputeTransferMode(Cmd command) noexcept
{
	using namespace transfer_mode;
	switch (command) {
		case Cmd::ReadSingleBlock:
			return kRead | kDmaEnable;
		case Cmd::ReadMultipleBlocks:
			return kRead | kMulti | kAutoCmd12 | kBlockCountEnable | kDmaEnable;
		case Cmd::WriteSingleBlock:
			return kDmaEnable;
		case Cmd::WriteMultipleBlocks:
			return kMulti | kAutoCmd12 | kBlockCountEnable | kDmaEnable;
		default:
			return 0;
	}
}


} // namespace jr::sdhci
