// Minimal unit test helpers
#ifndef TEST_H
#define TEST_H

#include <stdio.h>
#include <string.h>

static int test_failures = 0;
static int test_checks = 0;

#define CHECK(cond) do { test_checks++; if (!(cond)) { test_failures++; \
    printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

#define CHECK_EQ(a, b) do { long long va_ = (long long)(a), vb_ = (long long)(b); test_checks++; if (va_ != vb_) { \
    test_failures++; printf("  FAIL %s:%d: %s == %lld, expected %lld\n", __FILE__, __LINE__, #a, va_, vb_); } } while (0)

#define CHECK_STR(a, b) do { test_checks++; if (strcmp((a), (b)) != 0) { test_failures++; \
    printf("  FAIL %s:%d: \"%s\", expected \"%s\"\n", __FILE__, __LINE__, (a), (b)); } } while (0)

#define TEST_REPORT(name) (printf("%-16s %d checks, %d failed\n", name, test_checks, test_failures), test_failures != 0)

#endif
