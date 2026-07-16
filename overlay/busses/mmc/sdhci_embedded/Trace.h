// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot
#pragma once

#include <common/Trace.h>

// Per-component labeled tracing. Every log line is decorated with a short label
// (e.g. "sdhci_emb:eMMC0") so it is always obvious which device or discovery
// stage -- and that it is definitively the embedded driver, not upstream sdhci
// -- a line is about.
//
// Long-lived objects own their labels; module glue can use a static label.
// Data-path TRACE is compile-gated to keep the syslog quiet during transfers;
// TRACE_ALWAYS and ERROR are always on.

namespace jr::sdhci {

// A compact label buffer carried by traceable objects.
struct TraceLabel {
	char text[24] = "sdhci_emb";
};

} // namespace jr::sdhci


// Toggle verbose data-path tracing during bring-up.
#define JR_SDHCI_TRACE
//#define JR_SDHCI_TRACE_MEOW

#ifdef JR_SDHCI_TRACE
#	define JR_TRACE(label, fmt, ...) \
		JR_DIAG_TRACE((label).text, fmt, ##__VA_ARGS__)
#else
#	define JR_TRACE(label, fmt, ...) JR_DIAG_DISABLED()
#endif

#define JR_TRACE_ALWAYS(label, fmt, ...) \
	JR_DIAG_INFO((label).text, fmt, ##__VA_ARGS__)

#define JR_WARN(label, fmt, ...) \
	JR_DIAG_WARN((label).text, fmt, ##__VA_ARGS__)

#define JR_ERROR(label, fmt, ...) \
	JR_DIAG_ERROR((label).text, fmt, ##__VA_ARGS__)

// The "meow bus": a spurious/late interrupt is just the device meowing. The
// worker (the owner) will get up and check. Logged only when verbose, because
// on Bay Trail the cat is *very* chatty.
#ifdef JR_SDHCI_TRACE_MEOW
#	define JR_MEOW(label, status) \
		JR_DIAG_EVENT((label).text, "meow (irq 0x%08" B_PRIx32 ")\n", \
			(uint32)(status))
#else
#	define JR_MEOW(label, status) JR_DIAG_DISABLED()
#endif
