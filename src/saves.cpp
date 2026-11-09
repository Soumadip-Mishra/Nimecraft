#include "saves.hpp"

#include "storage.hpp"  // load_meta

#include <algorithm>
#include <charconv>
#include <optional>
#include <set>
#include <system_error>

namespace fs = std::filesystem;

namespace {

char lower(char c) {
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
}

std::string lower_text(std::string_view s) {
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(), lower);
    return out;
}

std::string_view trim_spaces(std::string_view s) {
    while (!s.empty() && s.front() == ' ') s.remove_prefix(1);
    while (!s.empty() && s.back() == ' ') s.remove_suffix(1);
    return s;
}

bool device_name(std::string_view s) {
    const std::string l = lower_text(s);
    if (l == "con" || l == "prn" || l == "aux" || l == "nul") return true;
    return l.size() == 4 && (l.starts_with("com") || l.starts_with("lpt")) && l[3] >= '1' && l[3] <= '9';
}

// The UTF-8 bytes of a path part as a std::string, and back.
std::string utf8_of(const fs::path& p) {
    const std::u8string u = p.u8string();
    return std::string(u.begin(), u.end());
}

fs::path path_of(std::string_view utf8) {
    return fs::path(std::u8string(utf8.begin(), utf8.end()));
}

}  // namespace

std::vector<WorldInfo> list_worlds(const fs::path& saves_dir) {
    std::vector<WorldInfo> out;
    std::error_code ec;
    if (!fs::is_directory(saves_dir, ec)) return out;
    for (fs::directory_iterator it(saves_dir, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code dec;
        if (!it->is_directory(dec)) continue;
        const fs::path meta = it->path() / "world.meta";
        const std::optional<WorldMeta> m = load_meta(meta);
        if (!m) continue;
        std::error_code tec;
        const fs::file_time_type t = fs::last_write_time(meta, tec);
        if (tec) continue;
        const std::string folder = utf8_of(it->path().filename());
        out.push_back(WorldInfo{folder, folder, m->seed, m->mode, t});
    }
    std::sort(out.begin(), out.end(), [](const WorldInfo& a, const WorldInfo& b) {
        if (a.last_played != b.last_played) return a.last_played > b.last_played;
        return a.folder < b.folder;
    });
    return out;
}

std::string sanitize_world_name(std::string_view name) {
    std::string kept;
    for (char c : name) {
        const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == ' ' ||
                        c == '_' || c == '-';
        if (ok) kept.push_back(c);
    }
    std::string out(trim_spaces(kept));
    if (out.size() > 32) out = std::string(trim_spaces(std::string_view(out).substr(0, 32)));
    if (out.empty()) return "World";
    if (device_name(out)) out += '_';
    return out;
}

std::string unique_folder(const fs::path& saves_dir, std::string_view name) {
    const std::string base = sanitize_world_name(name);
    std::set<std::string> taken;
    std::error_code ec;
    for (fs::directory_iterator it(saves_dir, ec), end; !ec && it != end; it.increment(ec))
        taken.insert(lower_text(utf8_of(it->path().filename())));
    if (!taken.contains(lower_text(base))) return base;
    for (int n = 2;; ++n) {
        const std::string candidate = base + " (" + std::to_string(n) + ")";
        if (!taken.contains(lower_text(candidate))) return candidate;
    }
}

bool delete_world(const fs::path& saves_dir, std::string_view folder) {
    if (folder.empty() || folder == "." || folder == ".." ||
        folder.find_first_of("/\\:") != std::string_view::npos)
        return false;
    const fs::path part = path_of(folder);
    if (part.is_absolute() || part.has_root_path()) return false;
    const fs::path target = saves_dir / part;
    std::error_code ec;
    const fs::file_status st = fs::symlink_status(target, ec);
    if (ec || !fs::is_directory(st)) return false;  // missing, a file, or a symlink
    // The resolved folder must sit directly inside the resolved saves folder.
    const fs::path real_target = fs::canonical(target, ec);
    if (ec) return false;
    const fs::path real_saves = fs::canonical(saves_dir, ec);
    if (ec || real_target.parent_path() != real_saves) return false;
    fs::remove_all(target, ec);
    return !ec && !fs::exists(target, ec);
}

uint32_t seed_from_text(std::string_view text, uint32_t fallback) {
    const std::string_view t = trim_spaces(text);
    if (t.empty()) return fallback;
    uint32_t v = 0;
    const auto res = std::from_chars(t.data(), t.data() + t.size(), v);
    if (res.ec == std::errc{} && res.ptr == t.data() + t.size()) return v;
    uint32_t h = 2166136261u;  // FNV-1a
    for (char c : t) {
        h ^= static_cast<unsigned char>(c);
        h *= 16777619u;
    }
    return h;
}
