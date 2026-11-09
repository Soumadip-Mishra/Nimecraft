// settings.ini: the player's options, a `key=value` text file in user_data_dir(). Logic only.
//
// Keys: render_distance (4..16), fov (60..100), sensitivity (0.1..3), master, sfx, music (0..1),
// vsync (true|false, also 1|0), gui_scale (1..4). Numbers go through std::to_chars/from_chars.
// Loading is tolerant: unknown keys, blank lines, spaces around keys and values, `\r\n` endings, and
// a UTF-8 byte order mark are accepted; a missing key or a value that does not parse (text, an
// empty value, trailing junk, a non-finite float) keeps that field's default; a number outside its
// range is clamped into it. A missing or unreadable file gives all defaults.
#pragma once

#include <filesystem>

struct Settings {
    int render_distance = 8;
    int fov = 70;
    float sensitivity = 1.f, master = 0.8f, sfx = 1.f, music = 0.5f;
    bool vsync = true;
    int gui_scale = 2;
};

constexpr int RD_MIN = 4, RD_MAX = 16, FOV_MIN = 60, FOV_MAX = 100, GUI_MIN = 1, GUI_MAX = 4;
constexpr float SENS_MIN = 0.1f, SENS_MAX = 3.f;

// Every field clamped into its range; a non-finite float becomes that field's default.
Settings clamp_settings(Settings s);

Settings load_settings(const std::filesystem::path& file);
// Writes clamp_settings(s) atomically (tmp file + rename), creating the parent folder if needed;
// false when anything fails. Never throws.
bool save_settings(const std::filesystem::path& file, const Settings& s);
