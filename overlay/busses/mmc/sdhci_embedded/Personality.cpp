// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot

#include "Personality.h"

namespace jr::sdhci {


const HostPersonality&
GetPersonality(PersonalityKind kind) noexcept
{
	static const GenericPersonality kGeneric;
	static const BayTrailPersonality kBayTrail;

	switch (kind) {
		case PersonalityKind::BayTrail:
			return kBayTrail;
		case PersonalityKind::Generic:
		default:
			return kGeneric;
	}
}


} // namespace jr::sdhci
