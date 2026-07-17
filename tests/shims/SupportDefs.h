// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot
#ifndef _JR_SHIM_SUPPORT_DEFS_H
#define _JR_SHIM_SUPPORT_DEFS_H

// Minimal host-side shim of Haiku's <SupportDefs.h>. It provides ONLY the
// fixed-width types, status_t, and the handful of B_* codes referenced by the
// pure-core input-driver logic under test. It is NOT a Haiku emulation — it
// exists so a driver translation unit such as ObjectTable.cpp can be compiled
// and exercised on the build host (see tests/Makefile, tests/README).

#include <stddef.h>	// size_t (global namespace)

typedef unsigned char		uint8;
typedef unsigned short		uint16;
typedef unsigned int		uint32;
typedef unsigned long long	uint64;
typedef signed char		int8;
typedef short			int16;
typedef int			int32;
typedef long long		int64;

typedef int			status_t;
typedef int64			bigtime_t;

#ifndef NULL
#define NULL 0
#endif

// Values mirror Haiku's <Errors.h> so any code that hard-codes them behaves
// identically; only relative distinctness matters for the current tests.
enum {
	B_OK		= 0,
	B_ERROR		= -1,
	B_NO_INIT	= (int)0x80000003,
	B_BAD_VALUE	= (int)0x80000005,
	B_BAD_DATA	= (int)0x8000000a,
	B_BUFFER_OVERFLOW = (int)0x8000000b,
	B_DEV_NO_MEDIA = (int)0x8000a003,
	B_DEV_MEDIA_CHANGED = (int)0x8000a00d,
};

static inline void
atomic_set(int32* value, int32 newValue)
{
	__atomic_store_n(value, newValue, __ATOMIC_RELEASE);
}

static inline int32
atomic_get(int32* value)
{
	return __atomic_load_n(value, __ATOMIC_ACQUIRE);
}

static inline int32
atomic_get_and_set(int32* value, int32 newValue)
{
	return __atomic_exchange_n(value, newValue, __ATOMIC_SEQ_CST);
}

static inline int32
atomic_test_and_set(int32* value, int32 newValue, int32 testAgainst)
{
	__atomic_compare_exchange_n(value, &testAgainst, newValue, false,
		__ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
	return testAgainst;
}

// printf length modifiers (Haiku spells these via <inttypes.h>-style macros).
#define B_PRId32 "d"
#define B_PRIu32 "u"
#define B_PRIx32 "x"
#define B_PRId64 "lld"
#define B_PRIu64 "llu"

#endif	// _JR_SHIM_SUPPORT_DEFS_H
