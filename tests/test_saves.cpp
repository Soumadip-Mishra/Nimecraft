// Tests for src/saves.* (M8 Task 4): world list, folder names, deleting worlds, typed seeds, and
// the M1 command line still skipping the title screen.
#include "cli.hpp"
#include "saves.hpp"
#include "storage.hpp"
#include "test.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <optional>
#include <span>
#include <string>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;

namespace {

void write_text(const fs::path& f, const std::string& s) {
    std::ofstream out(f, std::ios::binary);
    out << s;
}

// A valid world folder with the given seed and mode, its world.meta dated `age_s` seconds ago.
void make_world(const fs::path& saves, const std::string& folder, uint32_t seed, GameMode mode, int age_s) {
    const fs::path dir = saves / fs::path(std::u8string(reinterpret_cast<const char8_t*>(folder.data()), folder.size()));
    fs::create_directories(dir);
    WorldMeta m{seed, 0.5f, 70.f, 0.5f, 0.f, 0.f};
    m.mode = mode;
    CHECK(save_meta(dir / "world.meta", m));
    fs::last_write_time(dir / "world.meta", fs::file_time_type::clock::now() - std::chrono::seconds(age_s));
}

std::optional<CliOptions> cli(std::vector<const char*> args) {
    return parse_cli(std::span<const char* const>(args.data(), args.size()));
}

}  // namespace

TEST(test_sanitize_names) {
    CHECK(sanitize_world_name("  My World  ") == "My World");
    CHECK(sanitize_world_name("a/b\\c:*?") == "abc");
    CHECK(sanitize_world_name("") == "World");
    CHECK(sanitize_world_name("   ") == "World");
    CHECK(sanitize_world_name("...") == "World");
    CHECK(sanitize_world_name(std::string(40, 'x')) == std::string(32, 'x'));
    CHECK(sanitize_world_name("Mond\xC3\xA9") == "Mond");  // Mondé
    CHECK(sanitize_world_name("Under_score-dash 42") == "Under_score-dash 42");
    CHECK(sanitize_world_name("tab\there\nnew") == "tabherenew");
    CHECK(sanitize_world_name(std::string_view("nul\0byte", 8)) == "nulbyte");
    // Exactly 32 stays; 33 is cut; a cut that ends in a space is trimmed again.
    CHECK(sanitize_world_name(std::string(32, 'a')) == std::string(32, 'a'));
    CHECK(sanitize_world_name(std::string(33, 'a')) == std::string(32, 'a'));
    CHECK(sanitize_world_name(std::string(31, 'a') + "  b") == std::string(31, 'a'));
    // Dropped characters in the middle can join words; inner spaces are kept.
    CHECK(sanitize_world_name("a  b") == "a  b");
    CHECK(sanitize_world_name(" (x) ") == "x");
}

TEST(test_sanitize_windows_reserved) {
    CHECK(sanitize_world_name("CON") == "CON_");
    CHECK(sanitize_world_name("nul") == "nul_");
    CHECK(sanitize_world_name("COM1") == "COM1_");
    CHECK(sanitize_world_name("LPT9") == "LPT9_");
    CHECK(sanitize_world_name("prn") == "prn_");
    CHECK(sanitize_world_name("Aux") == "Aux_");
    CHECK(sanitize_world_name(" con ") == "con_");  // trimmed first
    CHECK(sanitize_world_name("con.txt") == "contxt");  // the dot is dropped: no longer a device
    // Near misses stay as they are.
    CHECK(sanitize_world_name("COM0") == "COM0");
    CHECK(sanitize_world_name("COM10") == "COM10");
    CHECK(sanitize_world_name("CONSOLE") == "CONSOLE");
    CHECK(sanitize_world_name("LPT") == "LPT");
}

TEST(test_unique_folder) {
    test::TempDir tmp;
    const fs::path saves = tmp.path() / "saves";
    CHECK(unique_folder(saves, "A") == "A");  // saves folder missing: no entries
    fs::create_directories(saves / "A");
    CHECK(unique_folder(saves, "A") == "A (2)");
    fs::create_directories(saves / "A (2)");
    CHECK(unique_folder(saves, "A") == "A (3)");
    CHECK(unique_folder(saves, "B") == "B");
    // Case-insensitive on every platform.
    fs::create_directories(saves / "a");
    CHECK(unique_folder(saves, "A") == "A (3)");
    const fs::path other = tmp.path() / "other";
    fs::create_directories(other / "a");
    CHECK(unique_folder(other, "A") == "A (2)");
    // A plain file takes the name too.
    write_text(other / "File", "x");
    CHECK(unique_folder(other, "file") == "file (2)");
    // The name is sanitized first.
    CHECK(unique_folder(other, "  a/ ") == "a (2)");
    CHECK(unique_folder(other, "") == "World");
    // Gaps are filled from (2) up.
    fs::create_directories(other / "Z");
    fs::create_directories(other / "Z (3)");
    CHECK(unique_folder(other, "Z") == "Z (2)");
}

TEST(test_list_worlds_skips_invalid) {
    test::TempDir tmp;
    const fs::path saves = tmp.path() / "saves";
    CHECK(list_worlds(saves).empty());  // missing saves folder
    fs::create_directories(saves / "no meta");
    fs::create_directories(saves / "corrupt");
    write_text(saves / "corrupt" / "world.meta", "this is not a world\n");
    fs::create_directories(saves / "meta is a folder" / "world.meta");
    write_text(saves / "plain file", "version=1\nseed=1\npx=0\npy=0\npz=0\nyaw=0\npitch=0\n");
    make_world(saves, "Old", 11u, GameMode::Survival, 3000);
    make_world(saves, "New", 22u, GameMode::Creative, 10);
    make_world(saves, "Middle", 33u, GameMode::Survival, 600);
    make_world(saves, "M\xC3\xBCnchen", 44u, GameMode::Survival, 20000);  // non-ASCII folder
    const std::vector<WorldInfo> w = list_worlds(saves);
    CHECK(w.size() == 4);
    if (w.size() == 4) {
        CHECK(w[0].folder == "New");
        CHECK(w[0].name == "New");
        CHECK(w[0].seed == 22u);
        CHECK(w[0].mode == GameMode::Creative);
        CHECK(w[1].folder == "Middle");
        CHECK(w[1].seed == 33u);
        CHECK(w[2].folder == "Old");
        CHECK(w[2].mode == GameMode::Survival);
        CHECK(w[3].folder == "M\xC3\xBCnchen");
        CHECK(w[3].seed == 44u);
        CHECK(w[0].last_played > w[1].last_played);
    }
    // Equal times: by folder name.
    const fs::path same = tmp.path() / "same";
    make_world(same, "b", 1u, GameMode::Survival, 100);
    make_world(same, "a", 2u, GameMode::Survival, 100);
    fs::last_write_time(same / "b" / "world.meta", fs::last_write_time(same / "a" / "world.meta"));
    const std::vector<WorldInfo> s = list_worlds(same);
    CHECK(s.size() == 2 && s[0].folder == "a" && s[1].folder == "b");
    // saves is a plain file: empty.
    write_text(tmp.path() / "file_saves", "x");
    CHECK(list_worlds(tmp.path() / "file_saves").empty());
}

TEST(test_delete_refuses_escape) {
    test::TempDir tmp;
    const fs::path saves = tmp.path() / "saves";
    fs::create_directories(saves / "a" / "b");
    fs::create_directories(tmp.path() / "x");
    write_text(tmp.path() / "x" / "keep.txt", "keep");
    make_world(saves, "World", 1u, GameMode::Survival, 0);
    CHECK(!delete_world(saves, "../x"));
    CHECK(!delete_world(saves, ".."));
    CHECK(!delete_world(saves, ""));
    CHECK(!delete_world(saves, "."));
    CHECK(!delete_world(saves, (tmp.path() / "x").string()));  // absolute
    CHECK(!delete_world(saves, "/"));
    CHECK(!delete_world(saves, "a/b"));
    CHECK(!delete_world(saves, "a\\b"));
    CHECK(!delete_world(saves, "C:x"));
    CHECK(!delete_world(saves, "World/.."));
    CHECK(fs::exists(tmp.path() / "x" / "keep.txt"));
    CHECK(fs::exists(saves / "a" / "b"));
    CHECK(fs::exists(saves / "World" / "world.meta"));
    // A symlink inside saves pointing outside is never followed. Creating one can need extra
    // rights on Windows; where it cannot be created there is nothing to check.
    std::error_code ec;
    fs::create_directory_symlink(tmp.path() / "x", saves / "link", ec);
    if (!ec) {
        CHECK(!delete_world(saves, "link"));
        CHECK(fs::exists(tmp.path() / "x" / "keep.txt"));
    }
    // saves_dir itself is never deleted.
    CHECK(fs::exists(saves));
}

TEST(test_delete_world) {
    test::TempDir tmp;
    const fs::path saves = tmp.path() / "saves";
    make_world(saves, "Doomed", 5u, GameMode::Survival, 0);
    write_text(saves / "Doomed" / "r.0.0.bin", "region");
    fs::create_directories(saves / "Doomed" / "nested" / "deeper");
    make_world(saves, "Kept", 6u, GameMode::Survival, 0);
    CHECK(delete_world(saves, "Doomed"));
    CHECK(!fs::exists(saves / "Doomed"));
    CHECK(fs::exists(saves / "Kept" / "world.meta"));
    CHECK(!delete_world(saves, "Doomed"));   // already gone
    CHECK(!delete_world(saves, "Missing"));
    CHECK(!delete_world(tmp.path() / "no_saves", "Kept"));
    // A plain file is not a world folder.
    write_text(saves / "file", "x");
    CHECK(!delete_world(saves, "file"));
    CHECK(fs::exists(saves / "file"));
    // A non-ASCII folder name.
    make_world(saves, "\xC3\xA9t\xC3\xA9", 7u, GameMode::Creative, 0);
    CHECK(delete_world(saves, "\xC3\xA9t\xC3\xA9"));
    CHECK(list_worlds(saves).size() == 1);
}

TEST(test_seed_from_text) {
    CHECK(seed_from_text("42", 7u) == 42u);
    CHECK(seed_from_text("  42  ", 7u) == 42u);
    CHECK(seed_from_text("0", 7u) == 0u);
    CHECK(seed_from_text("4294967295", 7u) == 4294967295u);
    CHECK(seed_from_text("", 7u) == 7u);
    CHECK(seed_from_text("   ", 9u) == 9u);
    // FNV-1a 32-bit test vectors.
    CHECK(seed_from_text("a", 7u) == 0xE40C292Cu);
    CHECK(seed_from_text("foobar", 7u) == 0xBF9CF968u);
    // Not a plain decimal: hashed.
    CHECK(seed_from_text("4294967296", 7u) != 7u);
    CHECK(seed_from_text("4294967296", 7u) != 0u);
    CHECK(seed_from_text("-1", 7u) != 4294967295u);
    CHECK(seed_from_text("12abc", 7u) != 12u);
    CHECK(seed_from_text("hello", 1u) == seed_from_text("hello", 2u));  // fallback unused
}

TEST(test_cli_still_skips_title) {
    const std::optional<CliOptions> w = cli({"--world", "My World"});
    CHECK(w.has_value());
    if (w) {
        CHECK(w->skip_title);
        CHECK(w->world == "My World");
    }
    const std::optional<CliOptions> none = cli({});
    CHECK(none.has_value() && !none->skip_title);  // no options: the title screen
    for (const std::vector<const char*>& args : std::vector<std::vector<const char*>>{
             {"--seed", "5"}, {"--rd", "12"}, {"--creative"}, {"--smoke"}}) {
        const std::optional<CliOptions> o = cli(args);
        CHECK(o.has_value() && o->skip_title);
    }
    CHECK(!cli({"--bogus"}).has_value());
}
