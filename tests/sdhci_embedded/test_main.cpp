// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot

#include "framework/jr_test.h"

// Entry point for the host-buildable pure-core test suite. Tests self-register
// via JR_TEST; this just runs them all.
int
main()
{
	return jr::test::RunAll();
}
