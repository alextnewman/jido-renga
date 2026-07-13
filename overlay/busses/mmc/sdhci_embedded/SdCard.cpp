// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot

#include "Card.h"

#include "Command.h"
#include "Csd.h"
#include "SdhciEngine.h"
#include "Trace.h"

namespace jr::sdhci {


namespace {


status_t
SendAppPrefix(SdhciEngine& engine, uint16_t rca)
{
	CommandOutcome outcome;
	status_t status = engine.Execute(Cmd::AppCmd,
		static_cast<uint32_t>(rca) << 16,
		ReplyType::R1, outcome);
	if (status != B_OK)
		return status;
	if (R1HasError(outcome.response[0]))
		return B_IO_ERROR;
	return (outcome.response[0] & (1u << 5)) != 0 ? B_OK : B_NOT_SUPPORTED;
}


status_t
PowerUp(SdhciEngine& engine, bool version2, bool request1v8,
	bool& highCapacity, bool& accepted1v8)
{
	const HostCapabilities host = engine.Capabilities();
	CommandOutcome outcome;
	if (SendAppPrefix(engine, 0) != B_OK)
		return B_IO_ERROR;
	status_t status = engine.Execute(Cmd::SdSendOpCond, 0, ReplyType::R3,
		GetSdOpCondConstraints(Quirk::None), outcome);
	if (status != B_OK)
		return status;

	const uint32_t selectedOcr = SelectOcrWindow(outcome.response[0],
		SdHostOcrMask(host));
	if (selectedOcr == 0)
		return B_NOT_SUPPORTED;
	const uint32_t argument = selectedOcr
		| (version2 ? (1u << 30) : 0)
		| (request1v8 ? (1u << 24) : 0);

	for (int i = 0; i < 100; i++) {
		if (SendAppPrefix(engine, 0) != B_OK) {
			snooze(10000);
			continue;
		}
		status = engine.Execute(Cmd::SdSendOpCond, argument, ReplyType::R3,
			GetSdOpCondConstraints(Quirk::None), outcome);
		if (status == B_OK && (outcome.response[0] & (1u << 31)) != 0)
			break;
		snooze(10000);
		status = B_TIMED_OUT;
	}
	if (status != B_OK)
		return status;

	highCapacity = (outcome.response[0] & (1u << 30)) != 0;
	accepted1v8 = request1v8 && (outcome.response[0] & (1u << 24)) != 0;
	return B_OK;
}


status_t
ReadSwitchStatus(SdhciEngine& engine, bool set, uint8_t function,
	uint8_t status[64])
{
	return engine.ReadDataBlock(Cmd::MmcSwitch,
		BuildSdSwitchArgument(set, 0, function), ReplyType::R1, status, 64);
}


} // namespace


status_t
SdCard::Identify(SdhciEngine& engine)
{
	const HostCapabilities host = engine.Capabilities();
	const bool hostCanUhs = host.voltage1v8 && host.platformUhsMask != 0
		&& (host.sdr104 || host.sdr50 || host.ddr50);

	bool accepted1v8 = false;
	status_t status = PowerUp(engine, fVersion2, fVersion2 && hostCanUhs,
		fHighCapacity, accepted1v8);
	if (status != B_OK)
		return status;

	if (accepted1v8) {
		CommandOutcome outcome;
		status = engine.Execute(Cmd::VoltageSwitch, 0, ReplyType::R1, outcome);
		if (status == B_OK && R1HasError(outcome.response[0]))
			status = B_IO_ERROR;
		if (status == B_OK)
			status = engine.SwitchSignalVoltage(true, true);
		if (status != B_OK) {
			// CMD11 changed card electrical state. A fallback is only safe after
			// a complete power cycle and a fresh identification sequence.
			if (engine.SwitchSignalVoltage(false, false) != B_OK)
				return status;
			engine.RecoverBus();
			CommandOutcome idle;
			if (engine.Execute(Cmd::GoIdleState, 0, ReplyType::None, idle) != B_OK)
				return status;
			snooze(30000);
			if (fVersion2)
				engine.Execute(Cmd::SendIfCond, 0x1aa, ReplyType::R7, idle);
			status = PowerUp(engine, fVersion2, false, fHighCapacity,
				accepted1v8);
			if (status != B_OK)
				return status;
		} else {
			fSignal1v8 = true;
		}
	}

	CommandOutcome outcome;
	if (engine.Execute(Cmd::AllSendCid, 0, ReplyType::R2, outcome) != B_OK)
		return B_IO_ERROR;
	for (int i = 0; i < 4; i++)
		fCid[i] = outcome.response[i];

	if (engine.Execute(Cmd::SendRelativeAddr, 0, ReplyType::R6, outcome) != B_OK)
		return B_IO_ERROR;
	if ((outcome.response[0] & 0xe000) != 0)
		return B_IO_ERROR;
	fRca = static_cast<uint16_t>(outcome.response[0] >> 16);
	if (fRca == 0)
		return B_BAD_DATA;

	if (engine.Execute(Cmd::SendCsd, static_cast<uint32_t>(fRca) << 16,
			ReplyType::R2, outcome) != B_OK) {
		return B_IO_ERROR;
	}
	const CardGeometry geometry = DecodeSdCsd(outcome.response);
	if (!geometry.valid)
		return B_BAD_DATA;
	fSectorCount = geometry.blockCount;
	fSectorSize = 512;

	if (engine.Execute(Cmd::SelectDeselectCard,
			static_cast<uint32_t>(fRca) << 16, ReplyType::R1, outcome) != B_OK) {
		return B_IO_ERROR;
	}
	if (R1HasError(outcome.response[0]))
		return B_IO_ERROR;

	uint8_t rawScr[8] = {};
	if (SendAppPrefix(engine, fRca) != B_OK
		|| engine.ReadDataBlock(static_cast<Cmd>(
			static_cast<uint8_t>(AppCmd::SendScr)), 0, ReplyType::R1,
			rawScr, sizeof(rawScr)) != B_OK) {
		return B_IO_ERROR;
	}
	const SdScr scr = DecodeScr(rawScr);
	if (!scr.valid)
		return B_BAD_DATA;

	uint8_t width = 1;
	if ((scr.busWidths & (1u << 2)) != 0) {
		if (SendAppPrefix(engine, fRca) == B_OK
			&& engine.Execute(Cmd::MmcSwitch, 2, ReplyType::R1, outcome) == B_OK
			&& !R1HasError(outcome.response[0])) {
			width = 4;
			engine.SetBusWidth(4);	// card first, host second
		}
	}

	if (!fHighCapacity) {
		if (engine.Execute(Cmd::SetBlockLength, 512, ReplyType::R1,
				outcome) != B_OK || R1HasError(outcome.response[0])) {
			return B_IO_ERROR;
		}
	}

	uint8_t switchStatus[64] = {};
	if (ReadSwitchStatus(engine, false, 0, switchStatus) != B_OK) {
		fMode = {BusTiming::Legacy, 25000, width, fSignal1v8, false};
		return engine.ConfigureBus(fMode);
	}

	if (fSignal1v8) {
		const SdMode candidates[] = {
			SdMode::Sdr104, SdMode::Ddr50, SdMode::Sdr50,
			SdMode::Sdr25, SdMode::Sdr12
		};
		SdMode current = SdMode::Sdr12;
		for (SdMode candidate : candidates) {
			if (!Supports(switchStatus, host, candidate, true))
				continue;

			BusMode slow = Describe(current);
			slow.clockKHz = 25000;
			slow.width = width;
			engine.ConfigureBus(slow);

			uint8_t selected[64] = {};
			status = ReadSwitchStatus(engine, true, SdFunction(candidate),
				selected);
			if (status != B_OK
				|| !SdSwitchSelected(selected, SdFunction(candidate))) {
				continue;
			}
			current = candidate;

			BusMode mode = Describe(candidate);
			mode.width = width;
			status = engine.ConfigureBus(mode);
			if (status == B_OK && RequiresTuning(candidate, host))
				status = engine.ExecuteTuning(Cmd::SdSendTuningBlock, 64);
			if (status == B_OK) {
				fMode = mode;
				JR_TRACE_ALWAYS(engine.Label(), "negotiated SD UHS mode %u, %u-bit"
					" at %" B_PRIu32 " kHz\n", SdFunction(candidate), width,
					mode.clockKHz);
				return B_OK;
			}
		}

		// At 1.8 V, function 0 is SDR12 and is the safe floor.
		uint8_t selected[64] = {};
		if (ReadSwitchStatus(engine, true, 0, selected) == B_OK
			&& SdSwitchSelected(selected, 0)) {
			fMode = Describe(SdMode::Sdr12);
			fMode.width = width;
			return engine.ConfigureBus(fMode);
		}
		return B_IO_ERROR;
	}

	if (Supports(switchStatus, host, SdMode::HighSpeed, false)) {
		uint8_t selected[64] = {};
		if (ReadSwitchStatus(engine, true, SdFunction(SdMode::HighSpeed),
				selected) == B_OK
			&& SdSwitchSelected(selected, SdFunction(SdMode::HighSpeed))) {
			fMode = Describe(SdMode::HighSpeed);
			fMode.width = width;
			status = engine.ConfigureBus(fMode);
			if (status == B_OK) {
				JR_TRACE_ALWAYS(engine.Label(),
					"negotiated SD high-speed, %u-bit at 50 MHz\n", width);
				return B_OK;
			}
		}
	}

	fMode = Describe(SdMode::Default);
	fMode.width = width;
	return engine.ConfigureBus(fMode);
}


status_t
SdCard::Reidentify(SdhciEngine& engine)
{
	engine.RecoverBus();

	CommandOutcome outcome;
	status_t status = engine.Execute(Cmd::GoIdleState, 0, ReplyType::None,
		outcome);
	if (status != B_OK)
		return status;
	snooze(30000);

	fVersion2 = engine.Execute(Cmd::SendIfCond, 0x1aa, ReplyType::R7,
		outcome) == B_OK && (outcome.response[0] & 0xfff) == 0x1aa;
	fHighCapacity = false;
	fSignal1v8 = false;
	fRca = 0;
	fSectorCount = 0;
	fMode = {};
	return Identify(engine);
}


} // namespace jr::sdhci
