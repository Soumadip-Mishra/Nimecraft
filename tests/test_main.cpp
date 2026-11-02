#include "test.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace test {
namespace {

struct TestCase {
    const char* name;
    TestFn fn;
};

struct State {
    std::vector<TestCase> tests;
    const char* current = "";
    long checks = 0;
    long failures = 0;
};

// Function-local static: registrations from other files may run before this file's globals.
State& state() {
    static State s;
    return s;
}

void report_failure(const char* file, int line, const std::string& message) {
    ++state().failures;
    std::printf("FAIL %s\n  %s:%d: %s\n", state().current, file, line, message.c_str());
}

}  // namespace

Registration::Registration(const char* name, TestFn fn) {
    for (const TestCase& t : state().tests) {
        if (std::string_view(t.name) == name) {
            std::printf("duplicate test name: %s\n", name);
            std::exit(1);
        }
    }
    state().tests.push_back({name, fn});
}

void check(bool ok, const char* expr, const char* file, int line) {
    ++state().checks;
    if (!ok) report_failure(file, line, std::string("CHECK(") + expr + ")");
}

void check_near(double a, double b, double eps, const char* a_expr, const char* b_expr,
                const char* file, int line) {
    ++state().checks;
    // Written so that NaN on either side fails.
    if (!(std::fabs(a - b) <= eps)) {
        char values[96];
        std::snprintf(values, sizeof values, " with %.17g vs %.17g (eps %.17g)", a, b, eps);
        report_failure(file, line,
                       std::string("CHECK_NEAR(") + a_expr + ", " + b_expr + ")" + values);
    }
}

TempDir::TempDir() {
    namespace fs = std::filesystem;
    const fs::path base = fs::temp_directory_path();
    // create_directory succeeds for exactly one caller, so concurrent test runs never share a folder.
    for (unsigned n = 0;; ++n) {
        fs::path candidate = base / ("nimecraft-test-" + std::to_string(n));
        if (fs::create_directory(candidate)) {
            path_ = candidate;
            return;
        }
    }
}

TempDir::~TempDir() {
    std::error_code ec;
    std::filesystem::remove_all(path_, ec);
}

}  // namespace test

int main(int argc, char** argv) {
    const std::string_view filter = argc > 1 ? argv[1] : "";
    test::State& s = test::state();
    long ran = 0;
    for (const test::TestCase& t : s.tests) {
        if (std::string_view(t.name).find(filter) == std::string_view::npos) continue;
        ++ran;
        s.current = t.name;
        try {
            t.fn();
        } catch (const std::exception& e) {
            test::report_failure(__FILE__, __LINE__, std::string("uncaught exception: ") + e.what());
        } catch (...) {
            test::report_failure(__FILE__, __LINE__, "uncaught non-standard exception");
        }
    }
    std::printf("%ld tests, %ld checks, %ld failures\n", ran, s.checks, s.failures);
    if (ran == 0) {
        // A misspelled filter must not look like a green run.
        std::printf("no test name contains \"%.*s\"\n", static_cast<int>(filter.size()), filter.data());
        return 1;
    }
    return s.failures > 0 ? 1 : 0;
}
