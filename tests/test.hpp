// Minimal test harness: self-registering TEST, CHECK, CHECK_NEAR, and a TempDir helper.
// `nimecraft_tests [substring]` runs every test whose name contains the substring and prints
// "N tests, M checks, F failures"; the exit code is 1 when F > 0.
#pragma once

#include <filesystem>

namespace test {

using TestFn = void (*)();

// Adds a test to the registry; created by the TEST macro at static-initialization time.
struct Registration {
    Registration(const char* name, TestFn fn);
};

void check(bool ok, const char* expr, const char* file, int line);
void check_near(double a, double b, double eps, const char* a_expr, const char* b_expr,
                const char* file, int line);

// A fresh, empty directory under the system temp folder, removed with its contents on destruction.
class TempDir {
public:
    TempDir();
    ~TempDir();
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

}  // namespace test

#define TEST(name)                                                              \
    static void name();                                                         \
    static const ::test::Registration name##_registration(#name, &name);        \
    static void name()

#define CHECK(cond) ::test::check(static_cast<bool>(cond), #cond, __FILE__, __LINE__)

#define CHECK_NEAR(a, b, eps) \
    ::test::check_near((a), (b), (eps), #a, #b, __FILE__, __LINE__)
