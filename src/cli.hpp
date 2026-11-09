// Command-line parsing for ./nimecraft [--world NAME] [--seed N] [--rd 4..16] [--creative] [--smoke]
// [--export-icon FILE [SIZE]].
#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>

struct CliOptions {
    std::string world = "default";
    std::optional<uint32_t> seed;
    int rd = 8;
    bool smoke = false;
    bool creative = false;  // a new world starts in Creative mode; a saved world keeps its mode
    // Any flag starts a game directly (M1 behavior); no arguments open the title screen (M8).
    bool skip_title = false;
    // --export-icon FILE [SIZE] (M8 Task 7): write generate_icon(SIZE) as a PNG and exit without a
    // window. SIZE is 16..1024 (default 256); an argument after FILE that starts with "--" is the
    // next flag, not a size.
    std::optional<std::filesystem::path> export_icon;
    int icon_size = 256;
};

// args are the arguments after the program name. nullopt on any invalid argument: an unknown
// flag, a missing value, a seed outside 0..4294967295, rd outside 4..16, or a world name that is
// not one folder name valid on both Linux and Windows: empty, ".", "..", a control character or
// one of < > : " / \ | ? *, a trailing dot or space, or a device name such as CON or COM1.
// A repeated flag takes its last value.
std::optional<CliOptions> parse_cli(std::span<const char* const> args);

// The usage text main prints before exiting with code 2.
const char* cli_usage();
