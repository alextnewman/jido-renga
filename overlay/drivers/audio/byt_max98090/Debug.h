// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot

#pragma once

#include <common/Trace.h>

// Enabled while the Winky hardware path is being brought up.
#define TRACE_BYT_MAX98090
#define BYT_MAX98090_TRACE_LABEL "byt_max98090"

#ifdef TRACE_BYT_MAX98090
#	define TRACE(x...) JR_DIAG_TRACE(BYT_MAX98090_TRACE_LABEL, x)
#else
#	define TRACE(x...) JR_DIAG_DISABLED()
#endif

#define ERROR(x...) JR_DIAG_ERROR(BYT_MAX98090_TRACE_LABEL, x)
#define EVENT(x...) JR_DIAG_EVENT(BYT_MAX98090_TRACE_LABEL, x)
