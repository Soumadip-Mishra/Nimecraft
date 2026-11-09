#include "cli.hpp"

#include <charconv>
#include <cstddef>
#include <string>
#include <string_view>
#include <system_error>

namespace {

template <class T>
std::optional<T> parse_number(std::string_view s) {
    T v{};
    const auto res = std::from_chars(s.data(), s.data() + s.size(), v);
    if (s.empty() || res.ec != std::errc{} || res.ptr != s.data() + s.size()) return std::nullopt;
    return v;
}

char upper(char c) {
    return c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c;
}

// CON, PRN, AUX, NUL, COM1-9, LPT1-9, alone or before an extension ("aux.txt"), any case.
bool windows_device_name(std::string_view name) {
    const std::string_view stem = name.substr(0, name.find('.'));
    std::string up;
    for (char c : stem) up.push_back(upper(c));
    if (up == "CON" || up == "PRN" || up == "AUX" || up == "NUL") return true;
    return up.size() == 4 && (up.starts_with("COM") || up.starts_with("LPT")) && up[3] >= '1' &&
           up[3] <= '9';
}

// One folder name that is valid on Linux and Windows and cannot step outside saves/.
bool valid_world_name(std::string_view name) {
    if (name.empty() || name == "." || name == "..") return false;
    if (name.back() == '.' || name.back() == ' ') return false;  // Windows drops them silently
    for (char c : name)
        if (static_cast<unsigned char>(c) < 0x20 ||
            std::string_view("<>:\"/\\|?*").find(c) != std::string_view::npos)
            return false;
    return !windows_device_name(name);
}

}  // namespace

std::optional<CliOptions> parse_cli(std::span<const char* const> args) {
    CliOptions o;
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string_view flag = args[i];
        if (flag == "--smoke" || flag == "--creative") {
            (flag == "--smoke" ? o.smoke : o.creative) = true;
            continue;
        }
        if (i + 1 >= args.size()) return std::nullopt;  // every other flag takes a value
        const std::string_view value = args[++i];
        if (flag == "--world") {
            if (!valid_world_name(value)) return std::nullopt;
            o.world = std::string(value);
        } else if (flag == "--seed") {
            const auto seed = parse_number<uint32_t>(value);
            if (!seed) return std::nullopt;
            o.seed = *seed;
        } else if (flag == "--rd") {
            const auto rd = parse_number<int>(value);
            if (!rd || *rd < 4 || *rd > 16) return std::nullopt;
            o.rd = *rd;
        } else {
            return std::nullopt;
        }
    }
    return o;
}

const char* cli_usage() {
    return "usage: nimecraft [--world NAME] [--seed N] [--rd 4..16] [--creative] [--smoke]\n"
           "  --world NAME  world folder under saves/ (default: default)\n"
           "  --seed N      seed for a new world, 0..4294967295 (default: from the clock)\n"
           "  --rd N        render distance in columns, 4..16 (default: 8)\n"
           "  --creative    start a new world in Creative mode (fly, unlimited blocks)\n"
           "  --smoke       walk a temporary world for 600 frames, print timing, exit\n";
}
