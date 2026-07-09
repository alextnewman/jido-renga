// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot
#ifndef _JR_SHIM_OS_H
#define _JR_SHIM_OS_H

// Host shim of <OS.h>. The real header is enormous; the pure-core logic under
// test only needs the base types and dprintf, so we simply re-export the two
// shims that provide them.

#include <SupportDefs.h>
#include <KernelExport.h>

#endif	// _JR_SHIM_OS_H
