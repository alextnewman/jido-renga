// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot
#pragma once

#include <KernelExport.h>
#include <device_manager.h>

#include "Command.h"
#include "Types.h"

namespace jr::sdhci {

class SdhciEngine;


// Card -- the dialect axis.
//
// A Card owns card identity (RCA, CID, CSD-derived geometry) and speaks the
// right command dialect (SD vs MMC): which opcode initializes it, whether it
// uses byte or sector addressing, how it reports capacity. The Controller
// probes once at init and instantiates exactly one concrete Card.
//
// Polymorphism here is the *card* varying, not cardinality -- there is one card
// per controller (container-of-1), so a Card is a member, not a node.
class Card {
public:
	virtual ~Card() = default;

	// Run the dialect-specific identify/init sequence against the engine, up to
	// the point the card is in transfer state and geometry is known.
	virtual status_t Identify(SdhciEngine& engine) = 0;

	virtual CardDialect Dialect() const = 0;
	virtual bool UsesSectorAddressing() const = 0;

	// Human-facing name for the device tree, e.g. "eMMC" / "SD Card".
	virtual const char* PrettyName() const = 0;

	uint64_t SectorCount() const { return fSectorCount; }
	uint32_t SectorSize() const { return fSectorSize; }
	uint16_t Rca() const { return fRca; }

	// Factory: probe the powered bus and build the concrete Card. Returns
	// nullptr if no card responds.
	static Card* Probe(SdhciEngine& engine);

protected:
	uint64_t	fSectorCount = 0;
	uint32_t	fSectorSize = 512;
	uint16_t	fRca = 0;
	uint32_t	fCid[4] = {0, 0, 0, 0};
};


class SdCard final : public Card {
public:
	status_t Identify(SdhciEngine& engine) override;
	CardDialect Dialect() const override { return CardDialect::Sd; }
	bool UsesSectorAddressing() const override { return fHighCapacity; }
	const char* PrettyName() const override { return "SD Card"; }

private:
	bool fHighCapacity = false;
};


class MmcCard final : public Card {
public:
	status_t Identify(SdhciEngine& engine) override;
	CardDialect Dialect() const override { return CardDialect::Mmc; }
	bool UsesSectorAddressing() const override { return fSectorAddressing; }
	const char* PrettyName() const override { return "eMMC"; }

private:
	// Set from the CMD1 OCR access-mode echo (bit 30). Extended-capacity eMMC
	// (> 2 GiB, the Bay Trail target) is sector-addressed; smaller parts are
	// byte-addressed. Default true for the expected soldered target.
	bool fSectorAddressing = true;
};


} // namespace jr::sdhci
