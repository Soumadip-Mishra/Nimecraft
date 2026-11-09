// Tests for src/settings.* (M8 Task 3): settings.ini load, save, clamping, and tolerance.
#include "settings.hpp"
#include "test.hpp"

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>

namespace {

void write_text(const std::filesystem::path& f, const std::string& s) {
    std::ofstream out(f, std::ios::binary);
    out << s;
}

std::string read_text(const std::filesystem::path& f) {
    std::ifstream in(f, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

bool is_default(const Settings& s) {
    return s.render_distance == 8 && s.fov == 70 && s.sensitivity == 1.f && s.master == 0.8f && s.sfx == 1.f &&
           s.music == 0.5f && s.vsync && s.gui_scale == 2;
}

bool in_ranges(const Settings& s) {
    return s.render_distance >= 4 && s.render_distance <= 16 && s.fov >= 60 && s.fov <= 100 &&
           s.sensitivity >= 0.1f && s.sensitivity <= 3.f && s.master >= 0.f && s.master <= 1.f && s.sfx >= 0.f &&
           s.sfx <= 1.f && s.music >= 0.f && s.music <= 1.f && s.gui_scale >= 1 && s.gui_scale <= 4 &&
           std::isfinite(s.sensitivity) && std::isfinite(s.master) && std::isfinite(s.sfx) && std::isfinite(s.music);
}

Settings load_text(const test::TempDir& tmp, const std::string& text) {
    const std::filesystem::path f = tmp.path() / "settings.ini";
    write_text(f, text);
    return load_settings(f);
}

}  // namespace

TEST(test_settings_defaults) {
    CHECK(is_default(Settings{}));
}

TEST(test_settings_roundtrip) {
    test::TempDir tmp;
    const std::filesystem::path f = tmp.path() / "settings.ini";
    Settings s;
    s.render_distance = 12;
    s.fov = 85;
    s.sensitivity = 1.7f;
    s.master = 0.33f;
    s.sfx = 0.1f;
    s.music = 0.f;
    s.vsync = false;
    s.gui_scale = 3;
    CHECK(save_settings(f, s));
    const Settings l = load_settings(f);
    CHECK(l.render_distance == 12);
    CHECK(l.fov == 85);
    CHECK(l.sensitivity == 1.7f);
    CHECK(l.master == 0.33f);
    CHECK(l.sfx == 0.1f);
    CHECK(l.music == 0.f);
    CHECK(!l.vsync);
    CHECK(l.gui_scale == 3);
    // The extremes of every range round-trip too, and saving again gives the same bytes.
    Settings lo{4, 60, 0.1f, 0.f, 0.f, 0.f, true, 1}, hi{16, 100, 3.f, 1.f, 1.f, 1.f, false, 4};
    for (const Settings& e : {lo, hi}) {
        CHECK(save_settings(f, e));
        const std::string first = read_text(f);
        const Settings r = load_settings(f);
        CHECK(r.render_distance == e.render_distance && r.fov == e.fov && r.sensitivity == e.sensitivity &&
              r.master == e.master && r.sfx == e.sfx && r.music == e.music && r.vsync == e.vsync &&
              r.gui_scale == e.gui_scale);
        CHECK(save_settings(f, r));
        CHECK(read_text(f) == first);
    }
    CHECK(!std::filesystem::exists(tmp.path() / "settings.ini.tmp"));
}

TEST(test_settings_file_format) {
    test::TempDir tmp;
    const std::filesystem::path f = tmp.path() / "settings.ini";
    CHECK(save_settings(f, Settings{}));
    CHECK(read_text(f) ==
          "render_distance=8\nfov=70\nsensitivity=1\nmaster=0.8\nsfx=1\nmusic=0.5\nvsync=true\ngui_scale=2\n");
    // Saving creates a missing parent folder (non-ASCII name included).
    const std::filesystem::path nested = tmp.path() / std::filesystem::path(u8"données") / "x" / "settings.ini";
    CHECK(save_settings(nested, Settings{}));
    CHECK(is_default(load_settings(nested)));
}

TEST(test_settings_clamp) {
    test::TempDir tmp;
    Settings a = load_text(tmp, "render_distance=3\nfov=59\nmaster=-0.1\nsfx=1.1\nmusic=2\nsensitivity=0\ngui_scale=0\n");
    CHECK(a.render_distance == 4);
    CHECK(a.fov == 60);
    CHECK(a.master == 0.f);
    CHECK(a.sfx == 1.f);
    CHECK(a.music == 1.f);
    CHECK(a.sensitivity == 0.1f);
    CHECK(a.gui_scale == 1);
    Settings b = load_text(tmp, "render_distance=17\nfov=101\nmaster=1.1\nsfx=-0.1\nmusic=-5\nsensitivity=5\ngui_scale=5\n");
    CHECK(b.render_distance == 16);
    CHECK(b.fov == 100);
    CHECK(b.master == 1.f);
    CHECK(b.sfx == 0.f);
    CHECK(b.music == 0.f);
    CHECK(b.sensitivity == 3.f);
    CHECK(b.gui_scale == 4);
    // Exactly at each limit stays.
    Settings c = load_text(tmp, "render_distance=4\nfov=100\nsensitivity=0.1\ngui_scale=4\nmaster=0\nmusic=1\n");
    CHECK(c.render_distance == 4 && c.fov == 100 && c.sensitivity == 0.1f && c.gui_scale == 4 && c.master == 0.f &&
          c.music == 1.f);
    // Huge numbers clamp instead of overflowing.
    Settings d = load_text(tmp, "render_distance=99999999999999999999\nfov=-99999999999999999999\nsensitivity=1e30\n");
    CHECK(d.render_distance == 8);  // does not fit an int: unparsable, default
    CHECK(d.fov == 70);
    CHECK(d.sensitivity == 3.f);
    // clamp_settings on its own, including non-finite floats.
    const float nan = std::numeric_limits<float>::quiet_NaN();
    Settings e = clamp_settings(Settings{-5, 500, nan, nan, std::numeric_limits<float>::infinity(), -nan, false, 9});
    CHECK(e.render_distance == 4 && e.fov == 100 && e.gui_scale == 4);
    CHECK(e.sensitivity == 1.f && e.master == 0.8f && e.sfx == 1.f && e.music == 0.5f);
    CHECK(!e.vsync);
    // Saving clamps too.
    const std::filesystem::path f = tmp.path() / "clamped.ini";
    CHECK(save_settings(f, Settings{30, 10, 9.f, 2.f, -1.f, 0.5f, true, 0}));
    const Settings g = load_settings(f);
    CHECK(g.render_distance == 16 && g.fov == 60 && g.sensitivity == 3.f && g.master == 1.f && g.sfx == 0.f &&
          g.gui_scale == 1);
}

TEST(test_settings_missing_file_defaults) {
    test::TempDir tmp;
    CHECK(is_default(load_settings(tmp.path() / "nope.ini")));
    CHECK(is_default(load_settings(tmp.path())));  // a folder, not a file
    CHECK(is_default(load_text(tmp, "")));         // empty file
}

TEST(test_settings_invalid_values) {
    test::TempDir tmp;
    const Settings s = load_text(tmp, "render_distance=abc\nvsync=maybe\nfov=\nsfx=0.25\ngui_scale=3\nmusic=0.2\n");
    CHECK(s.render_distance == 8);
    CHECK(s.vsync);
    CHECK(s.fov == 70);
    CHECK(s.sfx == 0.25f);
    CHECK(s.gui_scale == 3);
    CHECK(s.music == 0.2f);
    // Trailing junk, fractions in int keys, non-finite floats, and lines without '=' are ignored.
    const Settings t = load_text(tmp, "render_distance=12x\nfov=80.5\nmaster=nan\nsfx=inf\nsensitivity=-inf\n"
                                      "gui_scale\n=3\nmusic=0.3.4\nvsync=TRUE\n");
    CHECK(is_default(t));
    // The other spellings of vsync.
    CHECK(!load_text(tmp, "vsync=false\n").vsync);
    CHECK(!load_text(tmp, "vsync=0\n").vsync);
    CHECK(load_text(tmp, "vsync=1\n").vsync);
    CHECK(load_text(tmp, "vsync=false\nvsync=true\n").vsync);  // a repeated key takes its last value
    // A leading '+' is not a number from_chars accepts: default.
    CHECK(load_text(tmp, "fov=+90\n").fov == 70);
}

TEST(test_settings_tolerant_text) {
    test::TempDir tmp;
    const Settings s = load_text(tmp, "\xEF\xBB\xBF# comment line\r\n\r\nunknown_key=5\r\n  fov = 90  \r\n"
                                      "\r\nmaster=0.4\r\nrender_distance=10\r\nfuture.thing=yes\r\nvsync=false");
    CHECK(s.fov == 90);
    CHECK(s.master == 0.4f);
    CHECK(s.render_distance == 10);
    CHECK(!s.vsync);  // last line without a newline
    CHECK(s.sfx == 1.f);
    // Keys are case-sensitive: FOV is unknown.
    CHECK(load_text(tmp, "FOV=90\n").fov == 70);
    // A NUL byte in the middle of the file does not stop the rest from loading.
    const Settings n = load_text(tmp, std::string("fov=61\n\0\nrender_distance=5\n", 27));
    CHECK(n.fov == 61);
    CHECK(n.render_distance == 5);
}

TEST(test_settings_fuzz_no_crash) {
    test::TempDir tmp;
    const std::string base =
        "render_distance=12\nfov=85\nsensitivity=1.7\nmaster=0.33\nsfx=0.1\nmusic=0\nvsync=false\ngui_scale=3\n";
    const char alphabet[] = "=\n\r .-+e0123456789abcdefnitruvsx\xEF\xBB\xBF";
    uint32_t rng = 0xC0FFEEu;
    const auto next = [&rng] {
        rng ^= rng << 13;
        rng ^= rng >> 17;
        rng ^= rng << 5;
        return rng;
    };
    int changed = 0;
    for (int i = 0; i < 2000; ++i) {
        std::string text = base;
        const int edits = 1 + static_cast<int>(next() % 8);
        for (int e = 0; e < edits; ++e) {
            const std::size_t at = text.empty() ? 0 : next() % text.size();
            const char c = alphabet[next() % (sizeof alphabet - 1)];
            switch (next() % 3) {
            case 0: if (!text.empty()) text[at] = c; break;
            case 1: text.insert(text.begin() + static_cast<std::ptrdiff_t>(at), c); break;
            default: if (!text.empty()) text.erase(at, 1); break;
            }
        }
        const Settings s = load_text(tmp, text);
        CHECK(in_ranges(s));
        changed += s.render_distance != 12 || s.fov != 85 || s.gui_scale != 3 ? 1 : 0;
    }
    CHECK(changed > 100);  // the mutations really reached the values
}

TEST(test_settings_save_fails_cleanly) {
    test::TempDir tmp;
    write_text(tmp.path() / "blocker", "x");
    bool ok = true;
    bool threw = false;
    try {
        ok = save_settings(tmp.path() / "blocker" / "settings.ini", Settings{});
    } catch (...) {
        threw = true;
    }
    CHECK(!threw);
    CHECK(!ok);
    // The target is a folder: the rename fails, no tmp file is left behind.
    std::filesystem::create_directories(tmp.path() / "dir.ini" / "inner");
    CHECK(!save_settings(tmp.path() / "dir.ini", Settings{}));
    CHECK(!std::filesystem::exists(tmp.path() / "dir.ini.tmp"));
}
