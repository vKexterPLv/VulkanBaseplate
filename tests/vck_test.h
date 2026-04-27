// =============================================================================
//  VCK R14 unit-test harness  (header-only assertion micro-framework)
// =============================================================================
//  Why not GoogleTest:  VCK ships zero third-party test deps (rule 16).  The
//  harness is intentionally tiny - register cases by static-initialiser, run
//  them all from main(), print pass/fail with the same ANSI scheme VCKLog
//  uses, exit non-zero if anything failed.
//
//  Usage:
//
//      #include "vck_test.h"
//
//      TEST(MyGroup, my_case_name) {
//          ASSERT_TRUE(condition);
//          ASSERT_EQ(actual, expected);
//      }
//
//  main() lives in test_main.cpp - it just calls VCK::Test::RunAll(argc,argv).
//
//  The harness deliberately does not catch exceptions VCK code might throw -
//  R14 says fail loud, and an uncaught throw IS the loudest failure mode.
// =============================================================================

#pragma once

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sstream>
#include <vector>
#include <functional>

namespace VCK { namespace Test {

// ANSI colour shorthands (mirror VCKLog so test output blends in).
constexpr const char* kReset  = "\x1b[0m";
constexpr const char* kBold   = "\x1b[1m";
constexpr const char* kDim    = "\x1b[2m";
constexpr const char* kCyan   = "\x1b[96m";
constexpr const char* kYellow = "\x1b[93m";
constexpr const char* kRed    = "\x1b[91m";
constexpr const char* kGreen  = "\x1b[92m";
constexpr const char* kWhite  = "\x1b[97m";

struct Case {
    const char*           group;
    const char*           name;
    std::function<void()> fn;
};

// Lazy singleton vector so per-TU TestRegistrar instances enqueue cleanly
// regardless of static-init order.
inline std::vector<Case>& Registry() {
    static std::vector<Case> v;
    return v;
}

// Per-test failure log.  Set on ASSERT_* failure; ASSERT_* uses 'return' to
// abort the case immediately so subsequent assertions in the same case
// don't fire on already-broken state.
inline std::vector<std::string>& Failures() {
    static std::vector<std::string> v;
    return v;
}

inline void RecordFailure(const std::string& where, const std::string& body)
{
    std::string line;
    line.reserve(where.size() + body.size() + 4);
    line += where;
    line += "  ";
    line += body;
    Failures().push_back(std::move(line));
}

struct Registrar {
    Registrar(const char* group, const char* name, std::function<void()> fn) {
        Registry().push_back({group, name, std::move(fn)});
    }
};

inline int RunAll(int /*argc*/, char** /*argv*/)
{
    auto& reg = Registry();
    std::printf("%s[VCK tests]%s  %zu case(s) registered\n",
                kCyan, kReset, reg.size());

    int passed = 0;
    int failed = 0;

    for (const auto& c : reg) {
        Failures().clear();
        std::printf("  %s%s.%s%s ... ",
                    kBold, c.group, c.name, kReset);
        std::fflush(stdout);

        c.fn();

        if (Failures().empty()) {
            std::printf("%sok%s\n", kGreen, kReset);
            ++passed;
        } else {
            std::printf("%sFAIL%s\n", kRed, kReset);
            for (const auto& f : Failures()) {
                std::printf("      %s%s%s\n", kRed, f.c_str(), kReset);
            }
            ++failed;
        }
    }

    std::printf("\n%s[VCK tests]%s  passed %s%d%s   failed %s%d%s\n",
                kCyan, kReset,
                kGreen, passed, kReset,
                (failed ? kRed : kGreen), failed, kReset);

    return failed == 0 ? 0 : 1;
}

}} // namespace VCK::Test


// -----------------------------------------------------------------------------
//  Macros
// -----------------------------------------------------------------------------
//  TEST(group, name)         declares + registers a test case
//  ASSERT_TRUE(expr)         records failure + 'return's from the case
//  ASSERT_FALSE(expr)        same, inverted
//  ASSERT_EQ(a, b)           records expected/actual, returns
//  ASSERT_NE(a, b)           same, inverted
//  ASSERT_GE(a, b)           greater-or-equal
//  ASSERT_CONTAINS(haystack, needle)
//                            std::string::find(needle) != npos, records both
//                            on failure
//
//  Failures DO NOT throw - we want test bodies to be plain functions.
//  'return' aborts the case body cleanly.
// -----------------------------------------------------------------------------
#define VCK_TEST_STRINGIFY_(x) #x
#define VCK_TEST_STRINGIFY(x)  VCK_TEST_STRINGIFY_(x)
#define VCK_TEST_AT_           __FILE__ ":" VCK_TEST_STRINGIFY(__LINE__)

#define TEST(group, name)                                                      \
    static void vck_test_##group##_##name();                                   \
    static ::VCK::Test::Registrar                                              \
        vck_test_reg_##group##_##name(#group, #name,                           \
                                      vck_test_##group##_##name);              \
    static void vck_test_##group##_##name()

#define ASSERT_TRUE(expr)                                                      \
    do {                                                                        \
        if (!(expr)) {                                                          \
            ::VCK::Test::RecordFailure(VCK_TEST_AT_,                            \
                std::string("ASSERT_TRUE(" #expr ") was false"));               \
            return;                                                             \
        }                                                                       \
    } while (0)

#define ASSERT_FALSE(expr)                                                     \
    do {                                                                        \
        if ((expr)) {                                                           \
            ::VCK::Test::RecordFailure(VCK_TEST_AT_,                            \
                std::string("ASSERT_FALSE(" #expr ") was true"));               \
            return;                                                             \
        }                                                                       \
    } while (0)

#define ASSERT_EQ(actual, expected)                                            \
    do {                                                                        \
        const auto _v_a = (actual);                                             \
        const auto _v_e = (expected);                                           \
        if (!(_v_a == _v_e)) {                                                  \
            std::ostringstream _vck_oss;                                        \
            _vck_oss << "ASSERT_EQ(" #actual ", " #expected ")  "               \
                     << "actual=" << _v_a << " expected=" << _v_e;              \
            ::VCK::Test::RecordFailure(VCK_TEST_AT_, _vck_oss.str());           \
            return;                                                             \
        }                                                                       \
    } while (0)

#define ASSERT_NE(actual, expected)                                            \
    do {                                                                        \
        const auto _v_a = (actual);                                             \
        const auto _v_e = (expected);                                           \
        if ((_v_a == _v_e)) {                                                   \
            std::ostringstream _vck_oss;                                        \
            _vck_oss << "ASSERT_NE(" #actual ", " #expected ")  "               \
                     << "both=" << _v_a;                                        \
            ::VCK::Test::RecordFailure(VCK_TEST_AT_, _vck_oss.str());           \
            return;                                                             \
        }                                                                       \
    } while (0)

#define ASSERT_GE(actual, expected)                                            \
    do {                                                                        \
        const auto _v_a = (actual);                                             \
        const auto _v_e = (expected);                                           \
        if (!(_v_a >= _v_e)) {                                                  \
            std::ostringstream _vck_oss;                                        \
            _vck_oss << "ASSERT_GE(" #actual ", " #expected ")  "               \
                     << "actual=" << _v_a << " expected>=" << _v_e;             \
            ::VCK::Test::RecordFailure(VCK_TEST_AT_, _vck_oss.str());           \
            return;                                                             \
        }                                                                       \
    } while (0)

#define ASSERT_CONTAINS(haystack, needle)                                      \
    do {                                                                        \
        const std::string _vck_h = (haystack);                                  \
        const std::string _vck_n = (needle);                                    \
        if (_vck_h.find(_vck_n) == std::string::npos) {                         \
            ::VCK::Test::RecordFailure(VCK_TEST_AT_,                            \
                std::string("ASSERT_CONTAINS  needle='") + _vck_n               \
                + "'  haystack='" + _vck_h + "'");                              \
            return;                                                             \
        }                                                                       \
    } while (0)
