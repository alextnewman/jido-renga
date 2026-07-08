// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot
#pragma once

#include <cstdio>
#include <cstddef>
#include <atomic>
#include <mutex>
#include <vector>

// jr_test: a tiny, dependency-free, self-registering unit-test framework for
// jidō-renga's host-buildable "pure core" code. Header-only; each test source
// just includes this and uses JR_TEST(). Designed to be retrofit across
// drivers -- the pure logic of any overlay driver can be proven off-target.

namespace jr::test {


using TestFn = void (*)();

struct TestCase {
	const char*	suite;
	const char*	name;
	TestFn		fn;
};


inline std::vector<TestCase>&
Registry()
{
	static std::vector<TestCase> registry;
	return registry;
}


// Global counters are atomic so JR_CHECK may be called from worker threads in
// concurrency tests; curSuite/curName are only mutated between tests on the
// main thread (a test's threads are always joined before the next test runs).
struct Stats {
	std::atomic<int>	checks{0};
	std::atomic<int>	failures{0};
	std::mutex			printMutex;
	const char*			curSuite = "";
	const char*			curName = "";
};


inline Stats&
S()
{
	static Stats stats;
	return stats;
}


struct Registrar {
	Registrar(const char* suite, const char* name, TestFn fn)
	{
		Registry().push_back(TestCase{suite, name, fn});
	}
};


inline void
ReportFail(const char* file, int line, const char* expr)
{
	S().failures++;
	std::lock_guard<std::mutex> lock(S().printMutex);
	std::printf("  \033[31mFAIL\033[0m [%s.%s] %s:%d\n        %s\n",
		S().curSuite, S().curName, file, line, expr);
}


inline int
RunAll()
{
	int failedTests = 0;
	std::printf("jr_test: running %zu tests\n\n", Registry().size());
	for (const TestCase& t : Registry()) {
		S().curSuite = t.suite;
		S().curName = t.name;
		const int before = S().failures.load();
		t.fn();
		if (S().failures.load() > before) {
			failedTests++;
		} else {
			std::printf("  \033[32mok\033[0m   %s.%s\n", t.suite, t.name);
		}
	}
	std::printf("\n%zu tests, %d checks, %d failures\n",
		Registry().size(), S().checks.load(), S().failures.load());
	if (failedTests == 0)
		std::printf("\033[32mALL PASS\033[0m\n");
	else
		std::printf("\033[31m%d TEST(S) FAILED\033[0m\n", failedTests);
	return failedTests == 0 ? 0 : 1;
}


} // namespace jr::test


#define JR_CHECK(cond) \
	do { \
		::jr::test::S().checks++; \
		if (!(cond)) \
			::jr::test::ReportFail(__FILE__, __LINE__, #cond); \
	} while (0)

#define JR_CHECK_EQ(a, b) \
	do { \
		::jr::test::S().checks++; \
		if (!((a) == (b))) \
			::jr::test::ReportFail(__FILE__, __LINE__, #a " == " #b); \
	} while (0)

#define JR_CHECK_NE(a, b) \
	do { \
		::jr::test::S().checks++; \
		if (!((a) != (b))) \
			::jr::test::ReportFail(__FILE__, __LINE__, #a " != " #b); \
	} while (0)

#define JR_TEST(suite, name) \
	static void jr_test_##suite##_##name(); \
	static ::jr::test::Registrar jr_reg_##suite##_##name( \
		#suite, #name, &jr_test_##suite##_##name); \
	static void jr_test_##suite##_##name()
