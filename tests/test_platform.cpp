// Tests for src/platform.*. This file and src/platform.* are the only places allowed to touch
// environment variables and OS #ifs (Global Constraints).
#define _CRT_SECURE_NO_WARNINGS  // MSVC: _wgetenv is fine here; the value is copied at once

#include "platform.hpp"
#include "test.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <string>
#include <system_error>

namespace {

namespace fs = std::filesystem;
using EnvString = fs::path::string_type;  // std::wstring on Windows, std::string on Linux

EnvString env_text(const char* ascii) {
    EnvString out;
    for (const char* p = ascii; *p != '\0'; ++p) out.push_back(static_cast<EnvString::value_type>(*p));
    return out;
}

// Changes one environment variable for the length of a test and restores it afterwards.
class ScopedEnv {
public:
    explicit ScopedEnv(const char* name) : name_(env_text(name)) {
#ifdef _WIN32
        const wchar_t* value = _wgetenv(name_.c_str());
#else
        const char* value = std::getenv(name_.c_str());
#endif
        if (value != nullptr) saved_ = EnvString(value);
    }
    ~ScopedEnv() {
        if (saved_) {
            put(*saved_);
        } else {
            erase();
        }
    }
    ScopedEnv(const ScopedEnv&) = delete;
    ScopedEnv& operator=(const ScopedEnv&) = delete;

    void set(const fs::path& value) { require(put(value.native())); }
    void set_text(const char* ascii) { require(put(env_text(ascii))); }
    void unset() { require(erase()); }

private:
    bool put(const EnvString& value) {
#ifdef _WIN32
        // Windows cannot hold an empty variable: an empty value removes it.
        return _wputenv_s(name_.c_str(), value.c_str()) == 0;
#else
        return setenv(name_.c_str(), value.c_str(), 1) == 0;
#endif
    }
    bool erase() {
#ifdef _WIN32
        return _wputenv_s(name_.c_str(), L"") == 0;
#else
        return unsetenv(name_.c_str()) == 0;
#endif
    }
    static void require(bool ok) {
        if (!ok) throw std::runtime_error("cannot change an environment variable");
    }

    EnvString name_;
    std::optional<EnvString> saved_;
};

// Makes `dir` the working directory until the end of the scope, so a relative path that the code
// wrongly accepts lands inside a temp folder instead of the repository.
// Declare it after the TempDir it points into: the old directory must be back before removal.
class ScopedCurrentPath {
public:
    explicit ScopedCurrentPath(const fs::path& dir) : saved_(fs::current_path()) {
        fs::current_path(dir);
    }
    ~ScopedCurrentPath() {
        std::error_code ec;
        fs::current_path(saved_, ec);
    }
    ScopedCurrentPath(const ScopedCurrentPath&) = delete;
    ScopedCurrentPath& operator=(const ScopedCurrentPath&) = delete;

private:
    fs::path saved_;
};

// Every variable user_data_dir() reads, saved now and restored when the test ends.
struct DataDirEnv {
    ScopedEnv override_dir{"NIMECRAFT_HOME"};
#ifdef _WIN32
    ScopedEnv appdata{"APPDATA"};
#else
    ScopedEnv xdg{"XDG_DATA_HOME"};
    ScopedEnv home{"HOME"};
#endif
};

// Points the platform's default location at `base` and returns the folder the spec expects there.
fs::path use_default_location(DataDirEnv& env, const fs::path& base) {
#ifdef _WIN32
    env.appdata.set(base);
    return base / "Nimecraft";
#else
    env.xdg.unset();
    env.home.set(base);
    return base / ".local" / "share" / "nimecraft";
#endif
}

bool user_data_dir_throws() {
    try {
        (void)user_data_dir();
    } catch (const std::runtime_error&) {
        return true;
    }
    return false;
}

}  // namespace

TEST(test_harness_sanity) {
    CHECK(1 + 1 == 2);
}
TEST(test_user_data_dir_override) {
    test::TempDir tmp;
    test::TempDir other;
    DataDirEnv env;
    // The default location is valid too, so the override has to win over it, not just fill a gap.
    (void)use_default_location(env, other.path());
#ifndef _WIN32
    env.xdg.set(other.path() / "xdg");
#endif
    env.override_dir.set(tmp.path());
    CHECK(user_data_dir() == tmp.path());
    CHECK(fs::is_directory(tmp.path()));
    CHECK(fs::is_empty(other.path()));
}

TEST(test_user_data_dir_creates_missing) {
    test::TempDir tmp;
    DataDirEnv env;
    const fs::path dir = tmp.path() / "a" / "b";
    env.override_dir.set(dir);
    CHECK(user_data_dir() == dir);
    CHECK(fs::is_directory(dir));
}

TEST(test_user_data_dir_default) {
    test::TempDir tmp;
    DataDirEnv env;
    env.override_dir.unset();
    const fs::path expected = use_default_location(env, tmp.path());
    const fs::path dir = user_data_dir();
#ifdef _WIN32
    CHECK(dir.filename() == "Nimecraft");
#else
    CHECK(dir.filename() == "nimecraft");
#endif
    CHECK(dir == expected);
    CHECK(fs::is_directory(expected));
}

TEST(test_user_data_dir_empty_override) {
    test::TempDir tmp;
    DataDirEnv env;
    env.override_dir.set_text("");
    const fs::path expected = use_default_location(env, tmp.path());
    CHECK(user_data_dir() == expected);
    CHECK(fs::is_directory(expected));
}

#ifdef _WIN32
TEST(test_user_data_dir_xdg) {
    // Windows counterpart of the XDG rule: %APPDATA%\Nimecraft.
    test::TempDir tmp;
    DataDirEnv env;
    env.override_dir.unset();
    env.appdata.set(tmp.path());
    CHECK(user_data_dir() == tmp.path() / "Nimecraft");
    CHECK(fs::is_directory(tmp.path() / "Nimecraft"));
}
#else
TEST(test_user_data_dir_xdg) {
    test::TempDir tmp;
    DataDirEnv env;
    env.override_dir.unset();
    env.home.set(tmp.path() / "home");
    const fs::path home_dir = tmp.path() / "home" / ".local" / "share" / "nimecraft";

    env.xdg.set(tmp.path() / "xdg");
    CHECK(user_data_dir() == tmp.path() / "xdg" / "nimecraft");
    CHECK(fs::is_directory(tmp.path() / "xdg" / "nimecraft"));

    // Relative and empty values are ignored, per the XDG Base Directory spec.
    {
        const ScopedCurrentPath cwd(tmp.path());
        env.xdg.set_text("relative/dir");
        CHECK(user_data_dir() == home_dir);
        CHECK(!fs::exists(tmp.path() / "relative"));
    }

    env.xdg.set_text("");
    CHECK(user_data_dir() == home_dir);
    CHECK(fs::is_directory(home_dir));
}
#endif

TEST(test_user_data_dir_non_ascii) {
    test::TempDir tmp;
    DataDirEnv env;
    const fs::path dir = tmp.path() / fs::path(u8"Mondé 世界");
    env.override_dir.set(dir);
    CHECK(user_data_dir() == dir);
    CHECK(fs::is_directory(dir));
}

TEST(test_user_data_dir_missing_home) {
    // Without a usable base folder there is nowhere to save; never fall back to the working directory.
    test::TempDir tmp;
    const ScopedCurrentPath cwd(tmp.path());
    DataDirEnv env;
    env.override_dir.unset();
#ifdef _WIN32
    env.appdata.unset();
    CHECK(user_data_dir_throws());
    env.appdata.set_text("relative\\dir");
    CHECK(user_data_dir_throws());
#else
    env.xdg.unset();
    env.home.unset();
    CHECK(user_data_dir_throws());
    env.home.set_text("relative/home");
    CHECK(user_data_dir_throws());
#endif
    CHECK(fs::is_empty(tmp.path()));
}

TEST(test_user_data_dir_not_a_directory) {
    test::TempDir tmp;
    DataDirEnv env;
    const fs::path file = tmp.path() / "file";
    std::ofstream(file, std::ios::binary) << "x";
    env.override_dir.set(file);
    CHECK(user_data_dir_throws());
}
