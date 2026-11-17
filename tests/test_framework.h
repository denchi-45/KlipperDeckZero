/*
 * SPDX-License-Identifier: MIT
 *
 * Minimal self-registering test framework (no external dependency).
 */

#pragma once

#include <cmath>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace kdz_test {

struct TestCase {
    const char* name;
    std::function<void()> body;
};

std::vector<TestCase>& registry();
void report_failure(const char* file, int line, const std::string& message);

struct Registrar {
    Registrar(const char* name, std::function<void()> body) {
        registry().push_back({name, std::move(body)});
    }
};

template <typename A, typename B>
std::string describe(const A& actual, const B& expected) {
    std::ostringstream out;
    out << "actual: " << actual << ", expected: " << expected;
    return out.str();
}

} // namespace kdz_test

#define KDZ_CONCAT_INNER(a, b) a##b
#define KDZ_CONCAT(a, b) KDZ_CONCAT_INNER(a, b)

#define TEST_CASE(name)                                                              \
    static void KDZ_CONCAT(kdz_test_fn_, __LINE__)();                                \
    static ::kdz_test::Registrar KDZ_CONCAT(kdz_test_reg_, __LINE__)(                \
        name, KDZ_CONCAT(kdz_test_fn_, __LINE__));                                   \
    static void KDZ_CONCAT(kdz_test_fn_, __LINE__)()

#define CHECK(expr)                                                                  \
    do {                                                                             \
        if (!(expr)) {                                                               \
            ::kdz_test::report_failure(__FILE__, __LINE__, "CHECK(" #expr ")");      \
        }                                                                            \
    } while (0)

#define CHECK_EQ(actual, expected)                                                   \
    do {                                                                             \
        const auto kdz_a = (actual);                                                 \
        const auto kdz_e = (expected);                                               \
        if (!(kdz_a == kdz_e)) {                                                     \
            ::kdz_test::report_failure(__FILE__, __LINE__,                           \
                #actual " == " #expected " (" + ::kdz_test::describe(kdz_a, kdz_e) + ")"); \
        }                                                                            \
    } while (0)

#define CHECK_NEAR(actual, expected, tolerance)                                      \
    do {                                                                             \
        const double kdz_a = (actual);                                               \
        const double kdz_e = (expected);                                             \
        if (std::fabs(kdz_a - kdz_e) > (tolerance)) {                                \
            ::kdz_test::report_failure(__FILE__, __LINE__,                           \
                #actual " ~= " #expected " (" + ::kdz_test::describe(kdz_a, kdz_e) + ")"); \
        }                                                                            \
    } while (0)
