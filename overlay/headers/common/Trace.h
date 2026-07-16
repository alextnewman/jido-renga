// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot
#pragma once

#include <KernelExport.h>


// Shared kernel-diagnostic presentation. Components retain their own compile
// gates and labels; this header owns only the bracketed format and severity
// palette.
#define JR_DIAG_ANSI_RESET		"\33[0m"
#define JR_DIAG_ANSI_CYAN		"\33[36m"
#define JR_DIAG_ANSI_YELLOW		"\33[33m"
#define JR_DIAG_ANSI_RED		"\33[31m"
#define JR_DIAG_ANSI_MAGENTA	"\33[35m"

#define JR_DIAG_PRINT(color, label, fmt, ...) \
	do { \
		dprintf(color "[%s]" JR_DIAG_ANSI_RESET " " fmt, label, \
			##__VA_ARGS__); \
	} while (0)

#define JR_DIAG_TRACE(label, fmt, ...) \
	JR_DIAG_PRINT(JR_DIAG_ANSI_CYAN, label, fmt, ##__VA_ARGS__)

#define JR_DIAG_INFO(label, fmt, ...) \
	JR_DIAG_PRINT(JR_DIAG_ANSI_CYAN, label, fmt, ##__VA_ARGS__)

#define JR_DIAG_WARN(label, fmt, ...) \
	JR_DIAG_PRINT(JR_DIAG_ANSI_YELLOW, label, fmt, ##__VA_ARGS__)

#define JR_DIAG_ERROR(label, fmt, ...) \
	JR_DIAG_PRINT(JR_DIAG_ANSI_RED, label, fmt, ##__VA_ARGS__)

#define JR_DIAG_EVENT(label, fmt, ...) \
	JR_DIAG_PRINT(JR_DIAG_ANSI_MAGENTA, label, fmt, ##__VA_ARGS__)

#define JR_DIAG_DISABLED(...) do {} while (0)
