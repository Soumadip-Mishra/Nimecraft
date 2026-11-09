// Game entry point: parses the command line and runs the frame loop of one Game.
#include "cli.hpp"
#include "game.hpp"

#include <cstddef>
#include <cstdio>
#include <exception>
#include <optional>
#include <span>

int main(int argc, char** argv) {
    const std::size_t count = argc > 1 ? static_cast<std::size_t>(argc - 1) : 0;
    const std::optional<CliOptions> opts = parse_cli(std::span<const char* const>(argv + 1, count));
    if (!opts) {
        std::fputs(cli_usage(), stderr);
        return 2;
    }
    try {
        Game game(*opts);
        while (game.frame()) {
        }
        return game.finish();
    } catch (const std::exception& e) {
        std::fprintf(stderr, "nimecraft: %s\n", e.what());
        return 1;
    }
}
