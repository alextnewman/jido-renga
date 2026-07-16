// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot
#pragma once

#include <stdint.h>


namespace jr::byt_audio {

constexpr uint64_t kMrfldCsrLpeReset = uint64_t{1} << 0;
constexpr uint64_t kMrfldCsrResetVector = uint64_t{1} << 1;
constexpr uint64_t kMrfldCsrRunstall = uint64_t{1} << 2;
constexpr uint64_t kMrfldCsrXtSnoop = uint64_t{1} << 11;
constexpr uint64_t kMrfldCsrResetMask = kMrfldCsrLpeReset
	| kMrfldCsrResetVector | kMrfldCsrRunstall;


constexpr uint64_t
MrfldAssertReset(uint64_t csr)
{
	return csr | kMrfldCsrResetMask;
}


constexpr uint64_t
MrfldReleaseLpeReset(uint64_t csr)
{
	return csr & ~kMrfldCsrLpeReset;
}


constexpr uint64_t
MrfldReleaseForExecution(uint64_t csr)
{
	return (csr | kMrfldCsrXtSnoop)
		& ~(kMrfldCsrLpeReset | kMrfldCsrRunstall);
}

} // namespace jr::byt_audio
