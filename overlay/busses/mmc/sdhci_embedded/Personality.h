// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot
#pragma once

#include <stdint.h>

#include "Command.h"
#include "Matcher.h"
#include "Types.h"

// Host-quirk personalities (axis 1). A personality abstracts the lifecycle and
// per-command deviations of a controller family from the SDHCI standard. The
// quirk *bitmask* drives command constraints (Command.h); the personality
// *object* drives lifecycle hooks, reply overrides, OCR sanity, and timeout
// configuration. Generic controllers pay zero cost.
//
// The only register interaction a personality needs (PostResetInit) is routed
// through a minimal abstract target, so the whole personality is host-testable
// without an MMIO map.

namespace jr::sdhci {


// Minimal register operations a personality may request after a controller
// reset. The engine implements this against real hardware; tests fake it.
class IHostQuirkTarget {
public:
	virtual ~IHostQuirkTarget() = default;

	// Disable the (broken on Bay Trail) preset-value mode in HOST_CONTROL_2.
	virtual void DisablePresetValueMode() = 0;
};


class HostPersonality {
public:
	virtual const char* Name() const noexcept = 0;

	// Reject transient garbage OCR values from uninitialized registers. Return
	// true if the OCR looks sane.
	virtual bool ValidateOcr(uint32_t ocr, CardDialect dialect) const noexcept = 0;

	// Some controllers need a different reply type than the spec suggests
	// (e.g. Bay Trail CMD12 as R1b). Return true and set `out` to override.
	virtual bool
	OverrideReplyType(Cmd command, CardDialect dialect, ReplyType& out)
		const noexcept
	{
		(void)command; (void)dialect; (void)out;
		return false;
	}

	// Apply controller-specific register patches after software reset.
	virtual void PostResetInit(IHostQuirkTarget& target) const { (void)target; }

	// Timeout clock in kHz (0 = auto-detect from the Capabilities register).
	virtual uint32_t TimeoutClockKHz() const noexcept { return 0; }

	// True if the timeout counter is driven by the SD clock domain (so the
	// divider must be recomputed on every clock change).
	virtual bool TimeoutClockUsesSdClock() const noexcept { return false; }

protected:
	// Personalities are stateless, statically-stored singletons that are never
	// deleted through this base, so the destructor is intentionally protected
	// and NON-virtual. That keeps the whole hierarchy trivially destructible:
	// the kernel runtime provides neither atexit/__cxa_atexit (destructor
	// registration for static-storage objects) nor __cxa_guard_* (function-local
	// static guards), so a public virtual destructor here would fail to link.
	~HostPersonality() = default;
};


// Standard SDHCI: no quirks, all defaults.
class GenericPersonality final : public HostPersonality {
public:
	const char* Name() const noexcept override { return "generic"; }

	bool
	ValidateOcr(uint32_t ocr, CardDialect) const noexcept override
	{
		return ocr != 0;
	}
};


// Intel Bay Trail SCC controllers (eMMC / SD). Implements the errata set.
class BayTrailPersonality final : public HostPersonality {
public:
	const char* Name() const noexcept override { return "baytrail"; }

	bool
	ValidateOcr(uint32_t ocr, CardDialect) const noexcept override
	{
		// Reject obviously uninitialized / floating patterns.
		switch (ocr) {
			case 0x00000000u:
			case 0xffffffffu:
			case 0xccccccccu:
			case 0x55555555u:
				return false;
			default:
				return true;
		}
	}

	bool
	OverrideReplyType(Cmd command, CardDialect, ReplyType& out)
		const noexcept override
	{
		// CMD12 must carry a busy response on Bay Trail even though the SD
		// spec doesn't specify one.
		if (command == Cmd::StopTransmission) {
			out = ReplyType::R1b;
			return true;
		}
		return false;
	}

	void
	PostResetInit(IHostQuirkTarget& target) const override
	{
		target.DisablePresetValueMode();
	}

	uint32_t TimeoutClockKHz() const noexcept override { return 1000; }
	bool TimeoutClockUsesSdClock() const noexcept override { return true; }
};


// Stateless singletons -- personalities carry no per-controller state.
const HostPersonality& GetPersonality(PersonalityKind kind) noexcept;


} // namespace jr::sdhci
