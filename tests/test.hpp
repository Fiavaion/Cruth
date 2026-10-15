// Minimal dependency-free test harness for the Cruth core.
// SPDX-License-Identifier: MIT
#pragma once

#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace cruth_test {

struct Case {
    const char* name;
    std::function<void()> fn;
};

inline std::vector<Case>& registry() {
    static std::vector<Case> cases;
    return cases;
}

inline int& failures() {
    static int n = 0;
    return n;
}

struct Register {
    Register(const char* name, std::function<void()> fn) { registry().push_back({name, std::move(fn)}); }
};

inline void report(const char* file, int line, const std::string& what) {
    std::fprintf(stderr, "  %s:%d: %s\n", file, line, what.c_str());
    ++failures();
}

}  // namespace cruth_test

#define CRUTH_CONCAT2(a, b) a##b
#define CRUTH_CONCAT(a, b) CRUTH_CONCAT2(a, b)

#define TEST(name)                                                                              \
    static void CRUTH_CONCAT(test_, name)();                                                    \
    static cruth_test::Register CRUTH_CONCAT(reg_, name)(#name, &CRUTH_CONCAT(test_, name));     \
    static void CRUTH_CONCAT(test_, name)()

#define CHECK(cond)                                                                   \
    do {                                                                              \
        if (!(cond)) cruth_test::report(__FILE__, __LINE__, "CHECK(" #cond ") failed"); \
    } while (0)

#define CHECK_EQ(a, b)                                                                                     \
    do {                                                                                                   \
        auto va_ = (a);                                                                                    \
        auto vb_ = (b);                                                                                    \
        if (!(va_ == vb_))                                                                                 \
            cruth_test::report(__FILE__, __LINE__,                                                         \
                               "CHECK_EQ(" #a ", " #b ") failed: " + std::to_string(va_) + " vs " +        \
                                   std::to_string(vb_));                                                   \
    } while (0)

#define CHECK_NEAR(a, b, eps)                                                                              \
    do {                                                                                                   \
        double va_ = double(a), vb_ = double(b);                                                           \
        if (!(std::fabs(va_ - vb_) <= double(eps)))                                                        \
            cruth_test::report(__FILE__, __LINE__,                                                         \
                               "CHECK_NEAR(" #a ", " #b ") failed: " + std::to_string(va_) + " vs " +      \
                                   std::to_string(vb_));                                                   \
    } while (0)

// Stops the current test; use when later checks would crash.
#define REQUIRE(cond)                                                                    \
    do {                                                                                 \
        if (!(cond)) {                                                                   \
            cruth_test::report(__FILE__, __LINE__, "REQUIRE(" #cond ") failed");         \
            return;                                                                      \
        }                                                                                \
    } while (0)
