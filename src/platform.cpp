#define _CRT_SECURE_NO_WARNINGS  // MSVC: _wgetenv is the documented way to read %APPDATA% here

#include "platform.hpp"

#include <cstdlib>
#include <stdexcept>
#include <system_error>

namespace fs = std::filesystem;

namespace {

#ifdef _WIN32

// Value of an environment variable; empty when unset or empty. Wide, so non-ASCII folders survive.
fs::path env_path(const wchar_t* name) {
    const wchar_t* value = _wgetenv(name);
    return value != nullptr ? fs::path(value) : fs::path();
}

fs::path override_dir() {
    return env_path(L"NIMECRAFT_HOME");
}

fs::path default_dir() {
    const fs::path appdata = env_path(L"APPDATA");
    if (!appdata.is_absolute()) {
        throw std::runtime_error("user_data_dir: APPDATA is not set to an absolute path");
    }
    return appdata / L"Nimecraft";
}

#else

// Value of an environment variable; empty when unset or empty.
fs::path env_path(const char* name) {
    const char* value = std::getenv(name);
    return value != nullptr ? fs::path(value) : fs::path();
}

fs::path override_dir() {
    return env_path("NIMECRAFT_HOME");
}

fs::path default_dir() {
    // XDG Base Directory spec: a relative or empty XDG_DATA_HOME is invalid and ignored.
    const fs::path xdg = env_path("XDG_DATA_HOME");
    if (xdg.is_absolute()) {
        return xdg / "nimecraft";
    }
    const fs::path home = env_path("HOME");
    if (!home.is_absolute()) {
        throw std::runtime_error("user_data_dir: HOME is not set to an absolute path");
    }
    return home / ".local" / "share" / "nimecraft";
}

#endif

}  // namespace

fs::path user_data_dir() {
    fs::path dir = override_dir();
    if (dir.empty()) {
        dir = default_dir();
    }
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (!fs::is_directory(dir)) {
        throw fs::filesystem_error("user_data_dir: cannot create the folder", dir, ec);
    }
    return dir;
}
