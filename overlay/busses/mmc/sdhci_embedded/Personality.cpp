// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot

#include "Personality.h"

namespace jr::sdhci {


// File-scope singletons. At namespace scope the compiler does not emit the
// __cxa_guard_acquire/release calls a function-local static would need (those
// guard symbols are absent from the kernel runtime). The vtable pointer is
// still set by dynamic init in the add-on's .init_array, which the kernel
// module loader runs; no atexit/__cxa_atexit is emitted because the types are
// trivially destructible (protected non-virtual dtor -- see Personality.h).
namespace {
const GenericPersonality kGeneric;
const BayTrailPersonality kBayTrail;
} // namespace


const HostPersonality&
GetPersonality(PersonalityKind kind) noexcept
{
	switch (kind) {
		case PersonalityKind::BayTrail:
			return kBayTrail;
		case PersonalityKind::Generic:
		default:
			return kGeneric;
	}
}


} // namespace jr::sdhci
