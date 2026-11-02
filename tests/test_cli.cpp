// Tests for src/cli.*: command-line parsing and validation.
#include "cli.hpp"
#include "test.hpp"

#include <initializer_list>
#include <optional>
#include <span>
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
