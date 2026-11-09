// Game entry point: parses the command line, then either writes the icon (--export-icon) or runs
// the App (window, menus, and the game).
#include "cli.hpp"
#include "render.hpp"
#include "screens.hpp"
#include "storage.hpp"  // write_file_atomic

#include "raylib.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <optional>
#include <span>
#include <system_error>
#include <vector>

namespace {

// generate_icon(size) as a PNG at file (parent folders created); no window is opened. The bytes
// go through std::filesystem, so non-ASCII paths work on Windows too.
int export_icon(const std::filesystem::path& file, int size) {
    Image icon = generate_icon(size);
    int bytes = 0;
    unsigned char* png = ExportImageToMemory(icon, ".png", &bytes);
    UnloadImage(icon);
    if (png == nullptr || bytes <= 0) {
        std::fputs("nimecraft: could not encode the icon\n", stderr);
        return 1;
    }
    const std::vector<uint8_t> data(png, png + bytes);
    MemFree(png);
    std::error_code ec;
    if (file.has_parent_path()) std::filesystem::create_directories(file.parent_path(), ec);
    if (ec || !write_file_atomic(file, data)) {
        std::fputs("nimecraft: could not write the icon file\n", stderr);
        return 1;
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    const std::size_t count = argc > 1 ? static_cast<std::size_t>(argc - 1) : 0;
    const std::optional<CliOptions> opts = parse_cli(std::span<const char* const>(argv + 1, count));
    if (!opts) {
        std::fputs(cli_usage(), stderr);
        return 2;
    }
    try {
        if (opts->export_icon) return export_icon(*opts->export_icon, opts->icon_size);
        App app(*opts);
        return app.run();
    } catch (const std::exception& e) {
        std::fprintf(stderr, "nimecraft: %s\n", e.what());
        return 1;
    }
}
