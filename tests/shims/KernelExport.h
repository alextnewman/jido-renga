// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot
#ifndef _JR_SHIM_KERNEL_EXPORT_H
#define _JR_SHIM_KERNEL_EXPORT_H

// Host shim of <KernelExport.h>. The tested logic only reaches the kernel via
// dprintf (through the driver's ERROR/TRACE macros); on the host we route it to
// a no-op so error paths can run without a kernel. Add more stubs here only if
// a newly tested translation unit needs them.

#include <SupportDefs.h>

static inline void
dprintf(const char*, ...)
{
}

#endif	// _JR_SHIM_KERNEL_EXPORT_H
