// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot

#include "Card.h"

#include "Command.h"
#include "SdhciEngine.h"
#include "Trace.h"

namespace jr::sdhci {


namespace {

constexpr uint16_t kEmmcRca = 1;
constexpr uint8_t kExtCsdFlushCache = 32;
constexpr uint8_t kExtCsdCacheControl = 33;
constexpr uint8_t kExtCsdBusWidth = 183;
constexpr uint8_t kExtCsdHsTiming = 185;
constexpr uint8_t kExtCsdPowerClass = 187;

constexpr uint8_t kBusWidth1 = 0;
constexpr uint8_t kBusWidth8 = 2;
constexpr uint8_t kBusWidth8Ddr = 6;
constexpr uint8_t kTimingLegacy = 0;
constexpr uint8_t kTimingHighSpeed = 1;
constexpr uint8_t kTimingHs200 = 2;

constexpr uint32_t kR1ErrorMask = 0xfff9a080u;
constexpr uint32_t kR1ReadyForData = 1u << 8;
constexpr uint32_t kR1StateMask = 0xfu << 9;
constexpr uint32_t kR1StateTransfer = 4u << 9;


status_t
WaitReady(SdhciEngine& engine, uint16_t rca)
{
	CommandOutcome outcome;
	for (int i = 0; i < 100; i++) {
		status_t status = engine.Execute(Cmd::SendStatus,
			static_cast<uint32_t>(rca) << 16, ReplyType::R1, outcome);
		if (status != B_OK)
			return status;
		if ((outcome.response[0] & kR1ErrorMask) != 0)
			return B_IO_ERROR;
		if ((outcome.response[0] & (kR1ReadyForData | kR1StateMask))
			== (kR1ReadyForData | kR1StateTransfer)) {
			return B_OK;
		}
		snooze(10000);
	}
	return B_TIMED_OUT;
}


status_t
SwitchByte(SdhciEngine& engine, uint16_t rca, uint8_t index, uint8_t value,
	uint16_t timeoutMs)
{
	CommandConstraints constraints = GetCommandConstraints(Cmd::MmcSwitch,
		Quirk::None);
	if (timeoutMs > constraints.timeoutMs)
		constraints.timeoutMs = timeoutMs;

	CommandOutcome outcome;
	status_t status = engine.Execute(Cmd::MmcSwitch,
		BuildMmcSwitchArgument(index, value), ReplyType::R1b, constraints,
		outcome);
	if (status != B_OK)
		return status;
	return WaitReady(engine, rca);
}


status_t
SelectPowerClass(SdhciEngine& engine, uint16_t rca,
	const EmmcExtCsd& extCsd, EmmcMode mode, uint16_t timeoutMs)
{
	const uint8_t powerClass = PowerClass8Bit(extCsd, mode);
	return powerClass == 0 ? B_OK
		: SwitchByte(engine, rca, kExtCsdPowerClass, powerClass, timeoutMs);
}


BusMode
ModeAtClock(EmmcMode mode, uint32_t clockKHz)
{
	BusMode bus = Describe(mode);
	bus.clockKHz = clockKHz;
	return bus;
}


} // namespace


status_t
MmcCard::Identify(SdhciEngine& engine)
{
	const HostCapabilities host = engine.Capabilities();
	const uint32_t hostOcr = MmcHostOcrMask(host);
	if (hostOcr == 0) {
		JR_WARN(engine.Label(), "eMMC identify: host exposes no usable OCR window\n");
		return B_NOT_SUPPORTED;
	}

	CommandOutcome outcome;
	CommandConstraints opCond = GetCommandConstraints(Cmd::MmcSendOpCond,
		Quirk::None);

	// Probe with argument zero before selecting a voltage window. The host never
	// writes the response-only ready bit back to the card.
	status_t status = engine.Execute(Cmd::MmcSendOpCond, 0, ReplyType::R3,
		opCond, outcome);
	if (status != B_OK) {
		JR_WARN(engine.Label(), "eMMC identify: CMD1 OCR probe failed: %s\n",
			strerror(status));
		return status;
	}
	const uint32_t selectedOcr = SelectOcrWindow(outcome.response[0], hostOcr);
	if (selectedOcr == 0) {
		JR_WARN(engine.Label(), "eMMC identify: no OCR overlap, card %#08"
			B_PRIx32 " host %#08" B_PRIx32 "\n", outcome.response[0], hostOcr);
		return B_NOT_SUPPORTED;
	}

	const uint32_t ocrArgument = BuildMmcOcrArgument(selectedOcr);
	for (int i = 0; i < 100; i++) {
		status = engine.Execute(Cmd::MmcSendOpCond, ocrArgument, ReplyType::R3,
			opCond, outcome);
		if (status == B_OK && (outcome.response[0] & (1u << 31)) != 0)
			break;
		snooze(10000);
		status = B_TIMED_OUT;
	}
	if (status != B_OK) {
		JR_WARN(engine.Label(), "eMMC identify: CMD1 never became ready; last OCR "
			"%#08" B_PRIx32 ", request %#08" B_PRIx32 "\n",
			outcome.response[0], ocrArgument);
		return status;
	}
	fSectorAddressing = (outcome.response[0] & (1u << 30)) != 0;

	status = engine.Execute(Cmd::AllSendCid, 0, ReplyType::R2, outcome);
	if (status != B_OK) {
		JR_WARN(engine.Label(), "eMMC identify: CMD2 CID failed: %s\n",
			strerror(status));
		return B_IO_ERROR;
	}
	for (int i = 0; i < 4; i++)
		fCid[i] = outcome.response[i];

	fRca = kEmmcRca;
	status = engine.Execute(Cmd::SendRelativeAddr,
		static_cast<uint32_t>(fRca) << 16, ReplyType::R1, outcome);
	if (status != B_OK) {
		JR_WARN(engine.Label(), "eMMC identify: CMD3 RCA failed: %s\n",
			strerror(status));
		return B_IO_ERROR;
	}
	if (R1HasError(outcome.response[0])) {
		JR_WARN(engine.Label(), "eMMC identify: CMD3 RCA returned R1 %#08"
			B_PRIx32 "\n", outcome.response[0]);
		return B_IO_ERROR;
	}
	status = engine.Execute(Cmd::SendCsd, static_cast<uint32_t>(fRca) << 16,
		ReplyType::R2, outcome);
	if (status != B_OK) {
		JR_WARN(engine.Label(), "eMMC identify: CMD9 CSD failed: %s\n",
			strerror(status));
		return B_IO_ERROR;
	}
	const uint32_t csdResponse0 = outcome.response[0];
	JR_TRACE_ALWAYS(engine.Label(), "eMMC identify: CMD9 CSD %#08" B_PRIx32
		"/%#08" B_PRIx32 "/%#08" B_PRIx32 "/%#08" B_PRIx32 "\n",
		outcome.response[0], outcome.response[1], outcome.response[2],
		outcome.response[3]);
	status = engine.Execute(Cmd::SelectDeselectCard,
		static_cast<uint32_t>(fRca) << 16, ReplyType::R1, outcome);
	if (status != B_OK) {
		JR_WARN(engine.Label(), "eMMC identify: CMD7 select failed: %s\n",
			strerror(status));
		return B_IO_ERROR;
	}
	if (R1HasError(outcome.response[0])) {
		JR_WARN(engine.Label(), "eMMC identify: CMD7 select returned R1 %#08"
			B_PRIx32 "%s\n", outcome.response[0],
			outcome.response[0] == csdResponse0
				? " (matches stale CMD9 RESP0)" : "");
		return B_IO_ERROR;
	}

	uint8_t rawExtCsd[512] = {};
	status = engine.ReadDataBlock(Cmd::SendExtCsd, 0, ReplyType::R1,
		rawExtCsd, sizeof(rawExtCsd));
	if (status != B_OK) {
		JR_WARN(engine.Label(), "eMMC identify: CMD8 EXT_CSD failed: %s\n",
			strerror(status));
		return status;
	}
	fExtCsd = DecodeExtCsd(rawExtCsd);
	if (fExtCsd.sectorCount == 0)
		return B_BAD_DATA;
	fSectorCount = fExtCsd.sectorCount;
	fSectorSize = 512;

	const uint16_t switchTimeout = fExtCsd.genericCmd6Time10Ms != 0
		? static_cast<uint16_t>(fExtCsd.genericCmd6Time10Ms * 10u) : 1000;

	// Fixed Winky eMMC is a 1.8-V device. Switch the host signaling before any
	// high-speed card mode and establish 8-bit SDR at the legacy clock.
	if (host.voltage1v8) {
		status = engine.SwitchSignalVoltage(true, false);
		if (status != B_OK)
			return status;
	}
	status = SwitchByte(engine, fRca, kExtCsdBusWidth, kBusWidth8,
		switchTimeout);
	if (status == B_OK) {
		BusMode legacy8 = {BusTiming::Legacy, 400, 8, true, false};
		status = engine.ConfigureBus(legacy8);
	}
	if (status != B_OK)
		return status;

	bool haveHighSpeedBaseline = false;
	EmmcMode baseline = EmmcMode::Legacy;
	if (Supports(fExtCsd, host, EmmcMode::Hs52)
		|| Supports(fExtCsd, host, EmmcMode::Hs26)) {
		baseline = Supports(fExtCsd, host, EmmcMode::Hs52)
			? EmmcMode::Hs52 : EmmcMode::Hs26;
		status = SelectPowerClass(engine, fRca, fExtCsd, baseline,
			switchTimeout);
		if (status == B_OK)
			status = SwitchByte(engine, fRca, kExtCsdHsTiming,
				kTimingHighSpeed, switchTimeout);
		if (status == B_OK)
			status = engine.ConfigureBus(Describe(baseline));
		if (status == B_OK) {
			haveHighSpeedBaseline = true;
			fMode = Describe(baseline);
		}
	}

	if (Supports(fExtCsd, host, EmmcMode::Hs200)) {
		status = SelectPowerClass(engine, fRca, fExtCsd, EmmcMode::Hs200,
			switchTimeout);
		if (status == B_OK)
			status = SwitchByte(engine, fRca, kExtCsdHsTiming,
				kTimingHs200, switchTimeout);
		if (status == B_OK)
			status = engine.ConfigureBus(ModeAtClock(EmmcMode::Hs200, 52000));
		if (status == B_OK)
			status = WaitReady(engine, fRca);
		if (status == B_OK)
			status = engine.ConfigureBus(Describe(EmmcMode::Hs200));
		if (status == B_OK)
			status = engine.ExecuteTuning(Cmd::MmcSendTuningBlock, 128);
		if (status == B_OK) {
			fMode = Describe(EmmcMode::Hs200);
			JR_TRACE_ALWAYS(engine.Label(), "negotiated eMMC HS200 8-bit at %"
				B_PRIu32 " kHz\n", fMode.clockKHz);
			goto cache;
		}

		// Tuning or the final speed step failed. Slow down while retaining HS200
		// signaling, switch the card back first, then restore the host baseline.
		engine.ConfigureBus(ModeAtClock(EmmcMode::Hs200, 26000));
		status = SwitchByte(engine, fRca, kExtCsdHsTiming,
			haveHighSpeedBaseline ? kTimingHighSpeed : kTimingLegacy,
			switchTimeout);
		if (status != B_OK)
			return B_DEV_NOT_READY;
		if (haveHighSpeedBaseline)
			engine.ConfigureBus(Describe(baseline));
		else
			engine.ConfigureBus({BusTiming::Legacy, 20000, 8, true, false});
	}

	if (Supports(fExtCsd, host, EmmcMode::Ddr52)
		&& haveHighSpeedBaseline) {
		status = SelectPowerClass(engine, fRca, fExtCsd, EmmcMode::Ddr52,
			switchTimeout);
		if (status == B_OK)
			status = SwitchByte(engine, fRca, kExtCsdBusWidth,
				kBusWidth8Ddr, switchTimeout);
		if (status == B_OK)
			status = engine.ConfigureBus(Describe(EmmcMode::Ddr52));
		if (status == B_OK) {
			fMode = Describe(EmmcMode::Ddr52);
			JR_TRACE_ALWAYS(engine.Label(), "negotiated eMMC DDR52 8-bit\n");
			goto cache;
		}

		engine.ConfigureBus(ModeAtClock(EmmcMode::Ddr52, 26000));
		status = SwitchByte(engine, fRca, kExtCsdBusWidth, kBusWidth8,
			switchTimeout);
		if (status != B_OK)
			return B_DEV_NOT_READY;
		engine.ConfigureBus(Describe(baseline));
	}

	if (!haveHighSpeedBaseline) {
		fMode = {BusTiming::Legacy, 20000, 8, true, false};
		status = engine.ConfigureBus(fMode);
		if (status != B_OK)
			return status;
	} else {
		JR_TRACE_ALWAYS(engine.Label(), "negotiated eMMC %s 8-bit\n",
			baseline == EmmcMode::Hs52 ? "HS52" : "HS26");
	}

cache:
	if (fExtCsd.cacheSizeKiB != 0
		&& SwitchByte(engine, fRca, kExtCsdCacheControl, 1,
			switchTimeout) == B_OK) {
		fCacheEnabled = true;
		JR_TRACE_ALWAYS(engine.Label(), "enabled eMMC cache (%" B_PRIu32
			" KiB)\n", fExtCsd.cacheSizeKiB);
	}
	return B_OK;
}


status_t
MmcCard::Flush(SdhciEngine& engine)
{
	if (!fCacheEnabled)
		return B_OK;
	const uint16_t timeout = fExtCsd.genericCmd6Time10Ms != 0
		? static_cast<uint16_t>(fExtCsd.genericCmd6Time10Ms * 10u) : 1000;
	return SwitchByte(engine, fRca, kExtCsdFlushCache, 1, timeout);
}


status_t
MmcCard::Reidentify(SdhciEngine& engine)
{
	engine.RecoverBus();
	engine.EmmcHardwareReset();

	CommandOutcome outcome;
	status_t status = engine.Execute(Cmd::GoIdleState, 0, ReplyType::None,
		outcome);
	if (status != B_OK)
		return status;
	snooze(30000);

	fSectorAddressing = true;
	fSectorCount = 0;
	fRca = 0;
	fCacheEnabled = false;
	fExtCsd = {};
	fMode = {};
	return Identify(engine);
}


} // namespace jr::sdhci
