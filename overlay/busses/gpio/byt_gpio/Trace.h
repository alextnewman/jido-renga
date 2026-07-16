// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot
#pragma once

#include <common/Trace.h>


#define TRACE_BYT_GPIO
#define BYT_GPIO_TRACE_LABEL "byt_gpio"

#ifdef TRACE_BYT_GPIO
#	define TRACE(x...) JR_DIAG_TRACE(BYT_GPIO_TRACE_LABEL, x)
#else
#	define TRACE(x...) JR_DIAG_DISABLED()
#endif

#define ERROR(x...) JR_DIAG_ERROR(BYT_GPIO_TRACE_LABEL, x)
