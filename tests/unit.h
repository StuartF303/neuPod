// SPDX-License-Identifier: MPL-2.0
// Minimal unit-test harness: TEST(name) { ... }, ASSERT_* macros, RUN(name), RESULT().
#ifndef NEUPOD_TESTS_UNIT_H
#define NEUPOD_TESTS_UNIT_H

#include <stdio.h>

static int unit_checks, unit_failures, unit_tests;
static int unit_test_failed; // failures in the current test

#define TEST(name) static void name(void)

#define ASSERT_TRUE(cond)                                                                          \
    do {                                                                                           \
        unit_checks++;                                                                             \
        if (!(cond)) {                                                                             \
            unit_failures++;                                                                       \
            unit_test_failed++;                                                                    \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                               \
        }                                                                                          \
    } while (0)

#define ASSERT_FALSE(cond) ASSERT_TRUE(!(cond))

#define ASSERT_EQ(expected, actual)                                                                \
    do {                                                                                           \
        long long e_ = (long long)(expected), a_ = (long long)(actual);                            \
        unit_checks++;                                                                             \
        if (e_ != a_) {                                                                            \
            unit_failures++;                                                                       \
            unit_test_failed++;                                                                    \
            printf("  FAIL %s:%d: %s == %s (expected 0x%llx, got 0x%llx)\n", __FILE__, __LINE__,   \
                   #expected, #actual, (unsigned long long)e_, (unsigned long long)a_);            \
        }                                                                                          \
    } while (0)

#define RUN(name)                                                                                  \
    do {                                                                                           \
        unit_tests++;                                                                              \
        unit_test_failed = 0;                                                                      \
        name();                                                                                    \
        printf("%s %s\n", unit_test_failed ? "[FAIL]" : "[ ok ]", #name);                          \
    } while (0)

/** Print a summary and evaluate to the process exit code (0 = all passed). */
#define RESULT()                                                                                   \
    (printf("%d tests, %d checks, %d failures\n", unit_tests, unit_checks, unit_failures),         \
     unit_failures ? 1 : 0)

#endif // NEUPOD_TESTS_UNIT_H
