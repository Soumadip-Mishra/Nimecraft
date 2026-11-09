#include "settings.hpp"

#include "storage.hpp"  // read_file, write_file_atomic

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace {

std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.remove_suffix(1);
    return s;
}

template <class T>
std::optional<T> parse(std::string_view s) {
    T v{};
    const auto res = std::from_chars(s.data(), s.data() + s.size(), v);
    if (s.empty() || res.ec != std::errc{} || res.ptr != s.data() + s.size()) return std::nullopt;
    if constexpr (std::is_floating_point_v<T>) {
        if (!std::isfinite(v)) return std::nullopt;
    }
    return v;
}

std::optional<bool> parse_bool(std::string_view s) {
    if (s == "true" || s == "1") return true;
    if (s == "false" || s == "0") return false;
    return std::nullopt;
}

template <class T>
std::string number_text(T v) {
    std::array<char, 64> buf{};
    const auto res = std::to_chars(buf.data(), buf.data() + buf.size(), v);
    return std::string(buf.data(), res.ptr);
}

float unit_or(float v, float fallback) {
    return std::isfinite(v) ? std::clamp(v, 0.f, 1.f) : fallback;
}

}  // namespace

Settings clamp_settings(Settings s) {
    const Settings d;
    s.render_distance = std::clamp(s.render_distance, RD_MIN, RD_MAX);
    s.fov = std::clamp(s.fov, FOV_MIN, FOV_MAX);
    s.gui_scale = std::clamp(s.gui_scale, GUI_MIN, GUI_MAX);
    s.sensitivity = std::isfinite(s.sensitivity) ? std::clamp(s.sensitivity, SENS_MIN, SENS_MAX) : d.sensitivity;
    s.master = unit_or(s.master, d.master);
    s.sfx = unit_or(s.sfx, d.sfx);
    s.music = unit_or(s.music, d.music);
    return s;
}

Settings load_settings(const std::filesystem::path& file) {
    Settings s;
    const std::optional<std::vector<uint8_t>> bytes = read_file(file);
    if (!bytes) return s;
    const std::string text(bytes->begin(), bytes->end());
    std::string_view rest(text);
    if (rest.starts_with("\xEF\xBB\xBF")) rest.remove_prefix(3);  // UTF-8 byte order mark
    while (!rest.empty()) {
        const std::size_t nl = rest.find('\n');
        const std::string_view line = rest.substr(0, nl);
        rest = nl == std::string_view::npos ? std::string_view{} : rest.substr(nl + 1);
        const std::size_t eq = line.find('=');
        if (eq == std::string_view::npos) continue;
        const std::string_view key = trim(line.substr(0, eq)), value = trim(line.substr(eq + 1));
        if (key == "render_distance") {
            if (auto v = parse<int>(value)) s.render_distance = *v;
        } else if (key == "fov") {
            if (auto v = parse<int>(value)) s.fov = *v;
        } else if (key == "gui_scale") {
            if (auto v = parse<int>(value)) s.gui_scale = *v;
        } else if (key == "sensitivity") {
            if (auto v = parse<float>(value)) s.sensitivity = *v;
        } else if (key == "master") {
            if (auto v = parse<float>(value)) s.master = *v;
        } else if (key == "sfx") {
            if (auto v = parse<float>(value)) s.sfx = *v;
        } else if (key == "music") {
            if (auto v = parse<float>(value)) s.music = *v;
        } else if (key == "vsync") {
            if (auto v = parse_bool(value)) s.vsync = *v;
        }
    }
    return clamp_settings(s);
}

bool save_settings(const std::filesystem::path& file, const Settings& in) {
    const Settings s = clamp_settings(in);
    std::error_code ec;
    if (file.has_parent_path()) {
        std::filesystem::create_directories(file.parent_path(), ec);
        if (ec) return false;
    }
    const std::string text = "render_distance=" + number_text(s.render_distance) + "\nfov=" + number_text(s.fov) +
                             "\nsensitivity=" + number_text(s.sensitivity) + "\nmaster=" + number_text(s.master) +
                             "\nsfx=" + number_text(s.sfx) + "\nmusic=" + number_text(s.music) +
                             "\nvsync=" + (s.vsync ? "true" : "false") + "\ngui_scale=" + number_text(s.gui_scale) +
                             "\n";
    return write_file_atomic(file, std::vector<uint8_t>(text.begin(), text.end()));
}
