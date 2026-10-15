/*
 * SPDX-License-Identifier: MIT
 */

#include "test_framework.h"

namespace kdz_test {
namespace {
int failures = 0;
} // namespace

std::vector<TestCase>& registry() {
    static std::vector<TestCase> tests;
    return tests;
}

void report_failure(const char* file, int line, const std::string& message) {
    ++failures;
    std::cerr << "  FAILED " << file << ":" << line << ": " << message << "\n";
}

} // namespace kdz_test

int main() {
    int failed_tests = 0;
    for (const auto& test : kdz_test::registry()) {
        const int before = kdz_test::failures;
        test.body();
        const bool ok = kdz_test::failures == before;
        failed_tests += ok ? 0 : 1;
        std::cout << (ok ? "[ OK ] " : "[FAIL] ") << test.name << "\n";
    }
    std::cout << kdz_test::registry().size() - failed_tests << "/" << kdz_test::registry().size()
              << " tests passed\n";
    return failed_tests == 0 ? 0 : 1;
}
