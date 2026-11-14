#pragma once
#include <stdio.h>
#include <stdlib.h>
static int g_checks = 0, g_fail = 0;
#define CHECK(c) do { g_checks++; if (!(c)) { g_fail++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)
#define CHECK_EQ(a, b) do { g_checks++; long long _a = (long long)(a), _b = (long long)(b); if (_a != _b) { g_fail++; printf("FAIL %s:%d: %s == %s (%lld vs %lld)\n", __FILE__, __LINE__, #a, #b, _a, _b); } } while (0)
#define TEST(name) static void name(); struct name##_r { name##_r() { tests().push_back({#name, name}); } } name##_i; static void name()
#include <vector>
struct T { const char *n; void (*f)(); };
static std::vector<T> &tests() { static std::vector<T> t; return t; }
static int run_all(const char *suite) {
    for (auto &t : tests()) { int before = g_fail; t.f(); printf("%s %s.%s\n", g_fail == before ? "ok  " : "FAIL", suite, t.n); }
    printf("%s: %d tests, %d checks, %d failed\n", suite, (int) tests().size(), g_checks, g_fail);
    return g_fail ? 1 : 0;
}
