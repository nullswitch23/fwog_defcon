#ifndef TEST_UTIL_H
#define TEST_UTIL_H
#include <stdio.h>

/* Failure counter, shared by every translation unit in a test binary.
 * Declared extern here and defined once in test_util.c: a plain `static`
 * in a header gives each including TU its own private copy, so failures
 * recorded from any TU other than the one holding main() would silently
 * never reach TEST_RETURN(). No test currently spans multiple TUs with
 * assertions outside main(), but this is the only verification this branch
 * has, so it must stay correct if one ever does. */
extern int g_failures;

#define ASSERT_EQ(actual, expected) do { \
    unsigned long long _a = (unsigned long long)(actual); \
    unsigned long long _e = (unsigned long long)(expected); \
    if (_a != _e) { g_failures++; \
        printf("FAIL %s:%d: %s == 0x%llX, expected 0x%llX\n", \
               __FILE__, __LINE__, #actual, _a, _e); } \
} while (0)
#define ASSERT_TRUE(expr) do { \
    if (!(expr)) { g_failures++; \
        printf("FAIL %s:%d: %s is false\n", __FILE__, __LINE__, #expr); } \
} while (0)
#define TEST_RETURN() return g_failures ? 1 : 0
#endif
