// The world list: world folders under <user data>/saves, their names, and deleting them. Logic only.
#pragma once

#include "player.hpp"  // GameMode

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

// One saved world. `name` is what the list shows; it is the folder name (world.meta has no name
// key). last_played is world.meta's modification time.
struct WorldInfo {
    std::string name, folder;
    uint32_t seed;
    GameMode mode;
    std::filesystem::file_time_type last_played;
};

// Every folder in saves_dir holding a world.meta that load_meta accepts, newest first (ties by
// folder name); folders without a valid world.meta and plain files are skipped; a missing or
// unreadable saves_dir gives an empty list. Never throws.
std::vector<WorldInfo> list_worlds(const std::filesystem::path& saves_dir);

// A folder name that is safe on Linux and Windows: only [A-Za-z0-9 _-] kept (every other byte,
// including each byte of a non-ASCII character, dropped), spaces trimmed from both ends, cut to 32
// characters (and trimmed again), "World" if nothing is left, and a Windows device name (CON, PRN,
// AUX, NUL, COM1-9, LPT1-9, any case) gets "_" appended.
std::string sanitize_world_name(std::string_view name);

// sanitize_world_name(name), or the first of "<it> (2)", "<it> (3)", ... that no entry of saves_dir
// already uses, compared without ASCII case (so the folder also works on Windows). A missing
// saves_dir has no entries.
std::string unique_folder(const std::filesystem::path& saves_dir, std::string_view name);

// Removes saves_dir/folder and everything in it. False, and nothing deleted, unless `folder` is one
// plain path part (not empty, ".", or "..", no '/', '\\', or ':', not absolute) naming an existing
// real folder (not a symlink) directly inside saves_dir; false too if removal fails. Never throws.
bool delete_world(const std::filesystem::path& saves_dir, std::string_view folder);

// The seed typed on the Create World screen: a decimal 0..4294967295 is used as is (surrounding
// spaces ignored); any other non-empty text is hashed (FNV-1a over its bytes); empty or only spaces
// gives `fallback` (the caller passes a clock-based seed).
uint32_t seed_from_text(std::string_view text, uint32_t fallback);
