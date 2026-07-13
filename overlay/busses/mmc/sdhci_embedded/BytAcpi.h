// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot
#pragma once

#include "Matcher.h"


struct device_node;


namespace jr::sdhci {


// Applies the same ACPI device and PNP0D40 preconditions as Haiku's generic
// SDHCI driver, then classifies only Bay Trail's known controller HIDs.
const MatchProfile* ProfileForBytAcpiNode(device_node* node) noexcept;


} // namespace jr::sdhci
