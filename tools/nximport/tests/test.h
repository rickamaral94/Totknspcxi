// A minimal test runner: TEST(name) { ... } registers a case; CHECK fails it and goes on; REQUIRE stops it.
#pragma once
#include "error.h"

#include <cstdio>
#include <functional>
#include <string>
#include <vector>

struct TestCase {
    const char *name;
    std::function<void()> fn;
};
std::vector<TestCase> &test_registry();
extern int g_failures;

struct TestRegistrar {
    TestRegistrar(const char *n, std::function<void()> f) { test_registry().push_back({n, std::move(f)}); }
};

struct RequireFailed {};

#define TEST(name)                                                    \
    static void test_##name();                                        \
    static TestRegistrar reg_##name(#name, test_##name);              \
    static void test_##name()

#define CHECK(cond)                                                                       \
    do {                                                                                  \
        if (!(cond)) {                                                                    \
            fprintf(stderr, "  CHECK failed %s:%d: %s\n", __FILE__, __LINE__, #cond);     \
            g_failures++;                                                                 \
        }                                                                                 \
    } while (0)

#define REQUIRE(cond)                                                                     \
    do {                                                                                  \
        if (!(cond)) {                                                                    \
            fprintf(stderr, "  REQUIRE failed %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
            g_failures++;                                                                 \
            throw RequireFailed();                                                        \
        }                                                                                 \
    } while (0)

// The nx::Error a statement throws ("" when it throws none)
#define ERROR_OF(stmt)                         \
    ([&]() -> std::string {                    \
        try {                                  \
            stmt;                              \
        } catch (const nx::Error &e_) {        \
            return e_.en() + " | " + e_.pt(); \
        }                                      \
        return "";                             \
    }())

inline bool contains(const std::string &s, const std::string &sub) { return s.find(sub) != std::string::npos; }
