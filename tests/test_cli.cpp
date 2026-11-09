// Tests for src/cli.*: command-line parsing and validation.
#include "cli.hpp"
#include "test.hpp"

#include <filesystem>
#include <initializer_list>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

// Arguments after the program name, as main passes them.
std::optional<CliOptions> parse(std::initializer_list<const char*> args) {
    const std::vector<const char*> v(args);
    return parse_cli(std::span<const char* const>(v.data(), v.size()));
}

}  // namespace

TEST(test_cli_valid) {
    auto o = parse({"--world", "My World", "--seed", "42", "--rd", "12", "--smoke"});
    CHECK(o.has_value());
    if (o) {
        CHECK(o->world == "My World");
        CHECK(o->seed == 42u);
        CHECK(o->rd == 12);
        CHECK(o->smoke);
    }
    auto d = parse({});
    CHECK(d.has_value());
    if (d) {
        CHECK(d->world == "default");
        CHECK(!d->seed.has_value());
        CHECK(d->rd == 8);
        CHECK(!d->smoke);
    }
}

TEST(test_cli_order_and_repeats) {
    auto o = parse({"--smoke", "--rd", "4", "--world", "a", "--world", "b"});
    CHECK(o.has_value());
    if (o) {
        CHECK(o->world == "b");  // the last one wins
        CHECK(o->rd == 4);
        CHECK(o->smoke);
    }
}

TEST(test_cli_seed_bounds) {
    auto zero = parse({"--seed", "0"});
    CHECK(zero.has_value() && zero->seed == 0u);
    auto max = parse({"--seed", "4294967295"});
    CHECK(max.has_value() && max->seed == 4294967295u);
}

TEST(test_cli_rd_bounds) {
    auto lo = parse({"--rd", "4"});
    CHECK(lo.has_value() && lo->rd == 4);
    auto hi = parse({"--rd", "16"});
    CHECK(hi.has_value() && hi->rd == 16);
    CHECK(!parse({"--rd", "3"}).has_value());
    CHECK(!parse({"--rd", "17"}).has_value());
    CHECK(!parse({"--rd", "x"}).has_value());
    CHECK(!parse({"--rd", "8x"}).has_value());
    CHECK(!parse({"--rd", ""}).has_value());
    CHECK(!parse({"--rd"}).has_value());
}

TEST(test_cli_bad_input) {
    CHECK(!parse({"--bogus"}).has_value());
    CHECK(!parse({"world"}).has_value());
    CHECK(!parse({"--seed"}).has_value());
    CHECK(!parse({"--seed", "-1"}).has_value());
    CHECK(!parse({"--seed", "4294967296"}).has_value());
    CHECK(!parse({"--seed", "+5"}).has_value());
    CHECK(!parse({"--seed", "12 "}).has_value());
    CHECK(!parse({"--world", ""}).has_value());
    CHECK(!parse({"--world"}).has_value());
    CHECK(!parse({"--smoke", "1"}).has_value());
}

TEST(test_cli_usage_lists_flags) {
    const std::string_view usage = cli_usage();
    CHECK(usage.starts_with("usage: nimecraft"));
    for (std::string_view flag : {"--world", "--seed", "--rd 4..16", "--smoke"})
        CHECK(usage.find(flag) != std::string_view::npos);
}

TEST(test_cli_world_name_is_single_path_part) {
    // The name becomes one folder under saves/: separators or dot names would escape it.
    CHECK(parse({"--world", "Mondé 世界"}).has_value());
    CHECK(!parse({"--world", "../evil"}).has_value());
    CHECK(!parse({"--world", "a/b"}).has_value());
    CHECK(!parse({"--world", "a\\b"}).has_value());
    CHECK(!parse({"--world", "."}).has_value());
    CHECK(!parse({"--world", ".."}).has_value());
    // Names Windows cannot hold as one folder: "D:x" would even replace the whole saves path.
    for (const char* bad : {"D:x", "C:", "a<b", "a>b", "a\"b", "a|b", "a?b", "a*b", "tab\there",
                            "trailing.", "trailing ", "CON", "nul", "Com1", "lpt9", "aux.txt"})
        CHECK(!parse({"--world", bad}).has_value());
    for (const char* good : {"My World 2", "con2", "console", "a.b", "-dash", "COM0"})
        CHECK(parse({"--world", good}).has_value());
}

// M4 Task 6 (user choice, 2026-11-08): --creative starts a new world in Creative mode.
TEST(test_cli_creative_flag) {
    auto o = parse({"--creative"});
    CHECK(o.has_value() && o->creative);
    auto d = parse({"--world", "a"});
    CHECK(d.has_value() && !d->creative);
    auto both = parse({"--creative", "--seed", "7", "--creative"});
    CHECK(both.has_value() && both->creative && both->seed == 7u);
    CHECK(!parse({"--creative", "yes"}).has_value());  // takes no value: "yes" is an unknown flag
    CHECK(std::string_view(cli_usage()).find("--creative") != std::string_view::npos);
}

// M8 Task 6: the version comes from project(Nimecraft VERSION x.y.z) in CMakeLists.txt and is shown
// on the title screen and in the F3 panel.
TEST(test_version_from_cmake) {
    const std::string v = NIMECRAFT_VERSION;
    int dots = 0;
    bool digits_only = !v.empty() && v.front() != '.' && v.back() != '.';
    for (char c : v) {
        if (c == '.') ++dots;
        else if (c < '0' || c > '9') digits_only = false;
    }
    CHECK(digits_only);
    CHECK(dots == 2);
    CHECK(v == "1.0.0");
}

// M8 Task 7: --export-icon FILE [SIZE] writes the generated icon and exits without a window.
TEST(test_cli_export_icon) {
    const auto parse = [](std::vector<const char*> a) {
        return parse_cli(std::span<const char* const>(a.data(), a.size()));
    };
    const std::optional<CliOptions> plain = parse({"--export-icon", "icon.png"});
    CHECK(plain.has_value());
    if (plain) {
        CHECK(plain->export_icon.has_value());
        CHECK(plain->export_icon && *plain->export_icon == std::filesystem::path("icon.png"));
        CHECK(plain->icon_size == 256);  // default size
    }
    const std::optional<CliOptions> sized = parse({"--export-icon", "out/big icon.png", "512"});
    CHECK(sized && sized->icon_size == 512 && sized->export_icon &&
          *sized->export_icon == std::filesystem::path("out/big icon.png"));
    // A flag right after the file is a flag, not a size.
    const std::optional<CliOptions> then_flag = parse({"--export-icon", "i.png", "--rd", "5"});
    CHECK(then_flag && then_flag->icon_size == 256 && then_flag->rd == 5);
    // A UTF-8 file name stays the same path.
    const std::optional<CliOptions> utf8 = parse({"--export-icon", "ic\xC3\xB4ne.png"});
    CHECK(utf8 && utf8->export_icon && *utf8->export_icon == std::filesystem::path(u8"icône.png"));
    // Without the flag there is no icon to export.
    const std::optional<CliOptions> none = parse({"--world", "w"});
    CHECK(none && !none->export_icon && none->icon_size == 256);
    CHECK(std::string_view(cli_usage()).find("--export-icon") != std::string_view::npos);
}
