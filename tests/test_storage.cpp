// Tests for src/storage.*: region files and world.meta.
#include "block.hpp"
#include "block_entities.hpp"
#include "chunk.hpp"
#include "coords.hpp"
#include "inventory.hpp"
#include "items.hpp"
#include "player.hpp"
#include "storage.hpp"
#include "test.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <thread>
#include <variant>
#include <vector>

namespace fs = std::filesystem;

namespace {

// A column whose blocks depend on `salt`, so different columns hold different data:
// chunk 0 is STONE, chunk 1 a stripe of DIRT, plus a few salt-dependent blocks.
Column pattern_column(int salt) {
    Column col;
    for (std::size_t i = 0; i < col.size(); ++i) col[i] = std::make_unique<Chunk>();
    col[0]->blocks.fill(blk::STONE);
    for (int i = 0; i < CHUNK * CHUNK; ++i) col[1]->blocks[static_cast<std::size_t>(i)] = blk::DIRT;
    for (int k = 0; k < 20; ++k) {
        const int y = 2 + (salt * 7 + k * 3) % 14;
        const int idx = (salt * 131 + k * 197) % 4096;
        col[static_cast<std::size_t>(y)]->blocks[static_cast<std::size_t>(idx)] =
            static_cast<BlockId>(1 + (salt + k) % (blk::COUNT - 1));
    }
    return col;
}

bool same_blocks(const Column& a, const Column& b) {
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (!a[i] || !b[i]) return false;
        if (a[i]->blocks != b[i]->blocks) return false;
    }
    return true;
}

bool all_ids_valid(const Column& c) {
    for (const auto& ch : c) {
        if (!ch) return false;
        for (BlockId id : ch->blocks)
            if (id >= blk::COUNT) return false;
    }
    return true;
}

std::vector<uint8_t> read_bytes(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

void write_bytes(const fs::path& p, const std::vector<uint8_t>& b) {
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(b.data()), static_cast<std::streamsize>(b.size()));
}

void write_text(const fs::path& p, const std::string& s) {
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    out << s;
}

void put_u16(std::vector<uint8_t>& b, std::size_t at, uint16_t v) {
    b[at] = static_cast<uint8_t>(v & 0xFF);
    b[at + 1] = static_cast<uint8_t>(v >> 8);
}

uint16_t get_u16(const std::vector<uint8_t>& b, std::size_t at) {
    return static_cast<uint16_t>(b[at] | (b[at + 1] << 8));
}

std::size_t count_files(const fs::path& dir, const std::string& ext) {
    std::size_t n = 0;
    for (const auto& e : fs::directory_iterator(dir))
        if (e.path().extension() == ext) ++n;
    return n;
}

// Deterministic 32-bit LCG for the fuzz tests.
struct Lcg {
    uint32_t s;
    uint32_t next() {
        s = s * 1664525u + 1013904223u;
        return s >> 8;
    }
};

const WorldMeta SAMPLE_META{123456789u, 1.5f, 70.25f, -1000000.5f, 0.123456789f, -1.55f};

std::string sample_meta_text() {
    return "version=1\nseed=123456789\npx=1.5\npy=70.25\npz=-1000000.5\nyaw=0.5\npitch=-0.25\n";
}

bool meta_equal(const WorldMeta& a, const WorldMeta& b) {
    return a.seed == b.seed && a.px == b.px && a.py == b.py && a.pz == b.pz && a.yaw == b.yaw &&
           a.pitch == b.pitch;
}

void put_u32(std::vector<uint8_t>& b, uint32_t v) {
    for (int s = 0; s < 32; s += 8) b.push_back(static_cast<uint8_t>((v >> s) & 0xFF));
}

uint32_t get_u32(const std::vector<uint8_t>& b, std::size_t at) {
    return static_cast<uint32_t>(b[at]) | static_cast<uint32_t>(b[at + 1]) << 8 |
           static_cast<uint32_t>(b[at + 2]) << 16 | static_cast<uint32_t>(b[at + 3]) << 24;
}

// A region file in the M1 v1 layout, written here rather than by the code under test: magic,
// version 1, count, then per column cx, cz, len, and RLE pairs, with no block entity section.
std::vector<uint8_t> write_region_v1(const std::vector<std::pair<ColumnPos, BlockId>>& columns) {
    std::vector<uint8_t> b = {'N', 'M', 'C', 'R'};
    put_u32(b, 1);
    put_u32(b, static_cast<uint32_t>(columns.size()));
    for (const auto& [c, id] : columns) {
        put_u32(b, static_cast<uint32_t>(c.x));
        put_u32(b, static_cast<uint32_t>(c.z));
        put_u32(b, 8);  // two RLE pairs: 4096 x id, then 61440 x AIR
        b.push_back(0x00), b.push_back(0x10), b.push_back(static_cast<uint8_t>(id)), b.push_back(0);
        b.push_back(0x00), b.push_back(0xF0), b.push_back(0), b.push_back(0);
    }
    return b;
}

ItemStack st(ItemId id, int count, int damage = 0) {
    return ItemStack{id, static_cast<uint8_t>(count), static_cast<uint16_t>(damage)};
}

bool same_stack(const ItemStack& a, const ItemStack& b) {
    if (a.empty() && b.empty()) return true;
    return a.id == b.id && a.count == b.count && a.damage == b.damage;
}

void put_entity(Column& col, int wx, int wy, int wz, BlockEntity be) {
    const int lx = ((wx % 16) + 16) % 16, lz = ((wz % 16) + 16) % 16;
    col[static_cast<std::size_t>(wy / 16)]->entities[static_cast<uint16_t>(Chunk::index(lx, wy % 16, lz))] =
        std::move(be);
}

const BlockEntity* get_entity(const Column& col, int wx, int wy, int wz) {
    const int lx = ((wx % 16) + 16) % 16, lz = ((wz % 16) + 16) % 16;
    const auto& m = col[static_cast<std::size_t>(wy / 16)]->entities;
    auto it = m.find(static_cast<uint16_t>(Chunk::index(lx, wy % 16, lz)));
    return it == m.end() ? nullptr : &it->second;
}

// Column (-1, -2): a mid-burn kiln, a full crate of damaged tools, and a 40-item cairn.
Column entity_column() {
    Column col = pattern_column(9);
    KilnState k;
    k.in[0] = st(blk::COPPER_ORE, 7);
    k.in[1] = st(itm::TIN_INGOT, 2);
    k.fuel = st(itm::COAL, 3);
    k.out = st(itm::COPPER_INGOT, 11);
    k.burn_left = 37.123456f;
    k.burn_total = 80.f;
    k.progress = 3.3333333f;
    put_entity(col, -16, 70, -32, k);  // column (-1, -2) corner
    CrateState c;
    for (int i = 0; i < 27; ++i)
        c.slots[static_cast<std::size_t>(i)] = st(static_cast<ItemId>(itm::PICK_WOOD + i % 21), 1, i);
    put_entity(col, -1, 255, -17, c);  // opposite corner, top of the world
    CairnState cairn;
    for (int i = 0; i < 40; ++i) cairn.items.push_back(st(blk::DIRT, 1 + i));
    put_entity(col, -9, 0, -20, cairn);
    return col;
}

}  // namespace

// ---- Region files ----

TEST(test_region_roundtrip) {
    test::TempDir tmp;
    {
        RegionStore store(tmp.path() / "w");
        store.save_column(ColumnPos{0, 0}, pattern_column(1));
        store.save_column(ColumnPos{-1, -1}, pattern_column(2));
        store.save_column(ColumnPos{31, 31}, pattern_column(3));
        store.save_column(ColumnPos{32, 0}, pattern_column(4));
        CHECK(store.flush());
    }
    RegionStore store(tmp.path() / "w");
    const ColumnPos cs[] = {{0, 0}, {-1, -1}, {31, 31}, {32, 0}};
    for (int i = 0; i < 4; ++i) {
        auto col = store.load_column(cs[i]);
        CHECK(col.has_value());
        if (!col) continue;
        CHECK(same_blocks(*col, pattern_column(i + 1)));
        for (const auto& ch : *col) CHECK(ch->modified);
    }
    CHECK(!store.load_column(ColumnPos{5, 5}).has_value());
    CHECK(count_files(tmp.path() / "w", ".bin") == 3);
}

TEST(test_region_load_before_flush_sees_save) {
    test::TempDir tmp;
    RegionStore store(tmp.path());
    store.save_column(ColumnPos{3, 4}, pattern_column(9));
    auto col = store.load_column(ColumnPos{3, 4});
    CHECK(col.has_value() && same_blocks(*col, pattern_column(9)));
    CHECK(count_files(tmp.path(), ".bin") == 0);  // nothing written until flush
}

TEST(test_region_creates_dir) {
    test::TempDir tmp;
    RegionStore store(tmp.path() / "a" / "b");
    CHECK(fs::is_directory(tmp.path() / "a" / "b"));
    CHECK(store.flush());  // nothing to write is a success
}

TEST(test_region_flush_failure_reported) {
    test::TempDir tmp;
    RegionStore store(tmp.path() / "w");
    store.save_column(ColumnPos{0, 0}, pattern_column(1));
    fs::remove_all(tmp.path() / "w");
    write_text(tmp.path() / "w", "a file where the folder was");
    CHECK(!store.flush());
    // The failed region stays pending: once the folder is back, the next flush writes it.
    fs::remove(tmp.path() / "w");
    fs::create_directory(tmp.path() / "w");
    CHECK(store.flush());
    RegionStore fresh(tmp.path() / "w");
    auto col = fresh.load_column(ColumnPos{0, 0});
    CHECK(col.has_value() && same_blocks(*col, pattern_column(1)));
}

TEST(test_region_far_coords) {
    test::TempDir tmp;
    {
        RegionStore store(tmp.path());
        store.save_column(ColumnPos{1000000, -1000000}, pattern_column(7));
        CHECK(store.flush());
    }
    CHECK(fs::exists(tmp.path() / "r.31250.-31250.bin"));
    RegionStore store(tmp.path());
    auto col = store.load_column(ColumnPos{1000000, -1000000});
    CHECK(col.has_value() && same_blocks(*col, pattern_column(7)));
}

TEST(test_region_full_region) {
    // All 32 x 32 columns of one region round-trip.
    test::TempDir tmp;
    {
        RegionStore store(tmp.path());
        for (int z = 0; z < 32; ++z)
            for (int x = 0; x < 32; ++x) store.save_column(ColumnPos{x - 32, z}, pattern_column(x + z * 32));
        CHECK(store.flush());
    }
    CHECK(count_files(tmp.path(), ".bin") == 1);
    RegionStore store(tmp.path());
    bool all_ok = true;
    for (int z = 0; z < 32; ++z)
        for (int x = 0; x < 32; ++x) {
            auto col = store.load_column(ColumnPos{x - 32, z});
            all_ok = all_ok && col.has_value() && same_blocks(*col, pattern_column(x + z * 32));
        }
    CHECK(all_ok);
}

TEST(test_region_rejects_corrupt) {
    test::TempDir tmp;
    const fs::path dir = tmp.path() / "w";
    {
        RegionStore store(dir);
        Column col;
        for (std::size_t i = 0; i < col.size(); ++i) col[i] = std::make_unique<Chunk>();
        col[0]->blocks.fill(blk::STONE);  // RLE: (4096, STONE), (61440, AIR)
        store.save_column(ColumnPos{0, 0}, std::move(col));
        CHECK(store.flush());
    }
    const fs::path file = dir / "r.0.0.bin";
    const std::vector<uint8_t> good = read_bytes(file);
    CHECK(good.size() == 12 + 12 + 8 + 4);  // v2 (M4 Task 5): be_count 0 after the blocks
    CHECK(get_u16(good, 24) == 4096);
    CHECK(get_u16(good, 26) == blk::STONE);

    std::vector<std::vector<uint8_t>> bad;
    bad.emplace_back(good.begin(), good.begin() + 6);           // truncated header
    bad.emplace_back(good.begin(), good.end() - 3);             // truncated mid-entry
    bad.push_back(good);
    bad.back()[0] = 'X', bad.back()[1] = 'X', bad.back()[2] = 'X', bad.back()[3] = 'X';
    bad.push_back(good);
    bad.back()[4] = 3;                                          // version 3 (newer than v2)
    bad.push_back(good);
    bad.back()[4] = 0;                                          // version 0
    bad.push_back(good);
    put_u16(bad.back(), 24, 0);                                 // run 0
    bad.push_back(good);
    put_u16(bad.back(), 26, 500);                               // id 500
    bad.push_back(good);
    put_u16(bad.back(), 24, 4097);                              // runs one block over
    bad.push_back(good);
    put_u16(bad.back(), 24, 4095);                              // runs one block short
    bad.push_back(good);
    bad.back().push_back(0);                                    // trailing garbage
    bad.push_back(good);
    bad.back()[8] = 2;                                          // count says 2 entries
    bad.push_back(good);
    bad.back()[12] = 40;                                        // cx = 40 is not in region 0
    bad.push_back(std::vector<uint8_t>{});                      // empty file
    for (const auto& b : bad) {
        write_bytes(file, b);
        RegionStore store(dir);
        CHECK(!store.load_column(ColumnPos{0, 0}).has_value());
    }
    write_bytes(file, good);
    RegionStore store(dir);
    CHECK(store.load_column(ColumnPos{0, 0}).has_value());
}

TEST(test_region_corrupt_file_replaced_on_save) {
    test::TempDir tmp;
    write_text(tmp.path() / "r.0.0.bin", "NMC");
    {
        RegionStore store(tmp.path());
        CHECK(!store.load_column(ColumnPos{1, 1}).has_value());
        store.save_column(ColumnPos{1, 1}, pattern_column(5));
        CHECK(store.flush());
    }
    RegionStore store(tmp.path());
    auto col = store.load_column(ColumnPos{1, 1});
    CHECK(col.has_value() && same_blocks(*col, pattern_column(5)));
}

TEST(test_region_negative_and_borders) {
    test::TempDir tmp;
    const ColumnPos cs[] = {{-32, -32}, {-33, -32}, {31, 0}, {32, 0}};
    {
        RegionStore store(tmp.path());
        for (int i = 0; i < 4; ++i) store.save_column(cs[i], pattern_column(10 + i));
        CHECK(store.flush());
    }
    CHECK(fs::exists(tmp.path() / "r.-1.-1.bin"));
    CHECK(fs::exists(tmp.path() / "r.-2.-1.bin"));
    CHECK(fs::exists(tmp.path() / "r.0.0.bin"));
    CHECK(fs::exists(tmp.path() / "r.1.0.bin"));
    CHECK(count_files(tmp.path(), ".bin") == 4);
    RegionStore store(tmp.path());
    for (int i = 0; i < 4; ++i) {
        auto col = store.load_column(cs[i]);
        CHECK(col.has_value() && same_blocks(*col, pattern_column(10 + i)));
    }
}

TEST(test_region_overwrite) {
    test::TempDir tmp;
    RegionStore store(tmp.path());
    Column a = pattern_column(0);
    a[5]->blocks[7] = blk::STONE;
    store.save_column(ColumnPos{0, 0}, a);
    CHECK(store.flush());
    a[5]->blocks[7] = blk::PLANKS;
    store.save_column(ColumnPos{0, 0}, a);
    CHECK(store.flush());
    RegionStore fresh(tmp.path());
    auto col = fresh.load_column(ColumnPos{0, 0});
    CHECK(col.has_value() && (*col)[5]->blocks[7] == blk::PLANKS);
    CHECK(count_files(tmp.path(), ".bin") == 1);
}

TEST(test_region_keeps_other_columns_on_rewrite) {
    // Saving one column into a region already on disk keeps the region's other columns.
    test::TempDir tmp;
    {
        RegionStore store(tmp.path());
        store.save_column(ColumnPos{0, 0}, pattern_column(1));
        CHECK(store.flush());
    }
    {
        RegionStore store(tmp.path());
        store.save_column(ColumnPos{1, 0}, pattern_column(2));
        CHECK(store.flush());
    }
    RegionStore store(tmp.path());
    auto a = store.load_column(ColumnPos{0, 0});
    auto b = store.load_column(ColumnPos{1, 0});
    CHECK(a.has_value() && same_blocks(*a, pattern_column(1)));
    CHECK(b.has_value() && same_blocks(*b, pattern_column(2)));
}

TEST(test_region_all_air_and_all_one_id) {
    // A column of 65,536 equal blocks needs two runs (a run is at most 65,535).
    test::TempDir tmp;
    RegionStore store(tmp.path());
    Column air;
    for (std::size_t i = 0; i < air.size(); ++i) air[i] = std::make_unique<Chunk>();
    Column stone;
    for (std::size_t i = 0; i < stone.size(); ++i) {
        stone[i] = std::make_unique<Chunk>();
        stone[i]->blocks.fill(blk::STONE);
    }
    store.save_column(ColumnPos{0, 0}, air);
    store.save_column(ColumnPos{0, 1}, stone);
    CHECK(store.flush());
    RegionStore fresh(tmp.path());
    auto a = fresh.load_column(ColumnPos{0, 0});
    auto s = fresh.load_column(ColumnPos{0, 1});
    CHECK(a.has_value() && same_blocks(*a, air));
    CHECK(s.has_value() && same_blocks(*s, stone));
}

TEST(test_region_null_chunks_saved_as_air) {
    test::TempDir tmp;
    RegionStore store(tmp.path());
    Column partial;
    partial[3] = std::make_unique<Chunk>();
    partial[3]->blocks[0] = blk::SAND;
    store.save_column(ColumnPos{2, 2}, partial);
    CHECK(store.flush());
    RegionStore fresh(tmp.path());
    auto col = fresh.load_column(ColumnPos{2, 2});
    CHECK(col.has_value());
    if (col) {
        CHECK((*col)[3]->blocks[0] == blk::SAND);
        CHECK((*col)[0] && (*col)[0]->is_empty());
    }
}

TEST(test_region_no_tmp_left) {
    test::TempDir tmp;
    {
        RegionStore store(tmp.path());
        store.save_column(ColumnPos{0, 0}, pattern_column(1));
        CHECK(store.flush());
    }
    CHECK(count_files(tmp.path(), ".tmp") == 0);
    write_text(tmp.path() / "r.0.0.bin.tmp", "garbage that is not a region");
    RegionStore store(tmp.path());
    auto col = store.load_column(ColumnPos{0, 0});
    CHECK(col.has_value() && same_blocks(*col, pattern_column(1)));
}

TEST(test_region_fuzz_no_crash) {
    test::TempDir tmp;
    {
        RegionStore store(tmp.path());
        // v2 (M4 Task 5): the fuzzed file holds block entities too.
        Column first = pattern_column(1);
        KilnState k;
        k.in[0] = st(blk::IRON_ORE, 3);
        k.burn_left = 12.5f;
        put_entity(first, 5, 64, 5, k);
        CrateState c;
        c.slots[0] = st(itm::PICK_IRON, 1, 42);
        c.slots[26] = st(blk::DIRT, 64);
        put_entity(first, 6, 64, 5, c);
        put_entity(first, 7, 64, 5, CairnState{{st(itm::COAL, 9), st(blk::STONE, 1)}});
        store.save_column(ColumnPos{0, 0}, first);
        store.save_column(ColumnPos{1, 0}, pattern_column(2));
        CHECK(store.flush());
    }
    const fs::path file = tmp.path() / "r.0.0.bin";
    const std::vector<uint8_t> good = read_bytes(file);
    CHECK(get_u32(good, 4) == 2);
    Lcg rng{12345};
    int loaded = 0, rejected = 0;
    bool ids_ok = true;
    for (int i = 0; i < 2000; ++i) {
        std::vector<uint8_t> b = good;
        switch (rng.next() % 3) {
        case 0: b[rng.next() % b.size()] ^= static_cast<uint8_t>(1 + rng.next() % 255); break;
        case 1: b.resize(rng.next() % b.size()); break;
        default: {
            const std::size_t at = rng.next() % (b.size() + 1);
            const std::size_t n = 1 + rng.next() % 8;
            for (std::size_t k = 0; k < n; ++k)
                b.insert(b.begin() + static_cast<std::ptrdiff_t>(at), static_cast<uint8_t>(rng.next()));
        }
        }
        write_bytes(file, b);
        RegionStore store(tmp.path());
        auto col = store.load_column(ColumnPos{0, 0});
        if (col) {
            ++loaded;
            ids_ok = ids_ok && all_ids_valid(*col);
            for (const auto& ch : *col)
                for (const auto& [idx, be] : ch->entities)
                    for (const ItemStack& s : block_entity_contents(be))
                        ids_ok = ids_ok && idx < 4096 && item_def(s.id).max_stack >= s.count;
        } else {
            ++rejected;
        }
    }
    CHECK(ids_ok);
    CHECK(loaded + rejected == 2000);
    CHECK(rejected > 0);
}

TEST(test_store_concurrent_load_save) {
    test::TempDir tmp;
    RegionStore store(tmp.path());
    std::vector<std::thread> threads;
    for (int t = 0; t < 4; ++t)
        threads.emplace_back([&store, t] {
            for (int i = 0; i < 8; ++i) {
                const ColumnPos c{t * 8 + i, 0};  // all in region r.0.0
                store.save_column(c, pattern_column(t * 8 + i));
                (void)store.load_column(c);
                (void)store.load_column(ColumnPos{(t + 1) % 4 * 8 + i, 0});
            }
        });
    for (auto& th : threads) th.join();
    CHECK(store.flush());
    RegionStore fresh(tmp.path());
    bool all_ok = true;
    for (int k = 0; k < 32; ++k) {
        auto col = fresh.load_column(ColumnPos{k, 0});
        all_ok = all_ok && col.has_value() && same_blocks(*col, pattern_column(k));
    }
    CHECK(all_ok);
}

TEST(test_storage_non_ascii_dir) {
    test::TempDir tmp;
    const fs::path dir = tmp.path() / fs::path(u8"Mondé 世界");
    {
        RegionStore store(dir);
        store.save_column(ColumnPos{-3, 7}, pattern_column(6));
        CHECK(store.flush());
        CHECK(save_meta(dir / "world.meta", SAMPLE_META));
    }
    RegionStore store(dir);
    auto col = store.load_column(ColumnPos{-3, 7});
    CHECK(col.has_value() && same_blocks(*col, pattern_column(6)));
    auto m = load_meta(dir / "world.meta");
    CHECK(m.has_value() && meta_equal(*m, SAMPLE_META));
}

// ---- world.meta ----

TEST(test_meta_roundtrip) {
    test::TempDir tmp;
    CHECK(save_meta(tmp.path() / "world.meta", SAMPLE_META));
    auto m = load_meta(tmp.path() / "world.meta");
    CHECK(m.has_value() && meta_equal(*m, SAMPLE_META));
    // Normal floats only: some from_chars implementations report subnormals as out of range.
    const WorldMeta extremes{4294967295u, 3.4028235e38f, -1.17549435e-38f, 1e-30f, 0.f, -0.f};
    CHECK(save_meta(tmp.path() / "world.meta", extremes));
    auto e = load_meta(tmp.path() / "world.meta");
    CHECK(e.has_value() && meta_equal(*e, extremes));
    CHECK(count_files(tmp.path(), ".tmp") == 0);
}

TEST(test_meta_text_format) {
    test::TempDir tmp;
    CHECK(save_meta(tmp.path() / "world.meta", WorldMeta{7u, 0.5f, 70.f, -2.25f, 1.f, -0.5f}));
    const std::vector<uint8_t> bytes = read_bytes(tmp.path() / "world.meta");
    const std::string text(bytes.begin(), bytes.end());
    // M3 Task 4 appends the optional time key (default 6000); M4 Task 6 the mode, an empty
    // inventory (36 entries), empty armor (4), and the selected slot; M5 Task 3 full vitals (and
    // no spawn line while there is no bedroll).
    std::string empty_inv;
    for (int i = 0; i < 36; ++i) empty_inv += i == 0 ? "0:0:0" : ",0:0:0";
    CHECK(text == "version=1\nseed=7\npx=0.5\npy=70\npz=-2.25\nyaw=1\npitch=-0.5\ntime=6000\n"
                  "mode=survival\ninv=" + empty_inv + "\narmor=0:0:0,0:0:0,0:0:0,0:0:0\nsel=0\n"
                  "vitals=20,20,5,10,0,0,0\n");
}

TEST(test_meta_save_failure_reported) {
    test::TempDir tmp;
    CHECK(!save_meta(tmp.path() / "missing_dir" / "world.meta", SAMPLE_META));
}

TEST(test_meta_rejects_bad) {
    test::TempDir tmp;
    const fs::path f = tmp.path() / "world.meta";
    CHECK(!load_meta(f).has_value());  // missing file
    const std::string base = "version=1\npx=1\npy=2\npz=3\nyaw=0\npitch=0\n";
    const std::string cases[] = {
        "",
        base,                                        // missing seed
        base + "seed=abc\n",
        base + "seed=-1\n",
        base + "seed=4294967296\n",
        base + "seed=12x\n",
        base + "seed= 5\n",
        "version=1\nseed=1\npx=nan\npy=2\npz=3\nyaw=0\npitch=0\n",
        "version=1\nseed=1\npx=inf\npy=2\npz=3\nyaw=0\npitch=0\n",
        "version=1\nseed=1\npx=1e40\npy=2\npz=3\nyaw=0\npitch=0\n",
        "version=1\nseed=1\npx=\npy=2\npz=3\nyaw=0\npitch=0\n",
        "version=1\nseed=1\npy=2\npz=3\nyaw=0\npitch=0\n",            // missing px
        "version=1\nseed=1\npx=1\npy=2\npz=3\nyaw=0\n",                // missing pitch
        "seed=1\npx=1\npy=2\npz=3\nyaw=0\npitch=0\n",                  // missing version
        "version=2\nseed=1\npx=1\npy=2\npz=3\nyaw=0\npitch=0\n",       // unknown version
    };
    for (const std::string& c : cases) {
        write_text(f, c);
        CHECK(!load_meta(f).has_value());
    }
}

TEST(test_meta_tolerant_text) {
    test::TempDir tmp;
    const fs::path f = tmp.path() / "world.meta";
    write_text(f, sample_meta_text());
    auto plain = load_meta(f);
    CHECK(plain.has_value());
    write_text(f, "foo=bar\r\n\r\nversion=1\r\nseed=123456789\r\npx=1.5\r\n\npy=70.25\r\n"
                  "pz=-1000000.5\r\nyaw=0.5\r\npitch=-0.25");  // CRLF, blanks, no final newline
    auto tolerant = load_meta(f);
    CHECK(tolerant.has_value());
    if (plain && tolerant) CHECK(meta_equal(*plain, *tolerant));
    // A UTF-8 byte order mark and a stray line without '=' are ignored too.
    write_text(f, "\xEF\xBB\xBF" "version=1\nnote without equals sign\n" + sample_meta_text().substr(10));
    auto bom = load_meta(f);
    CHECK(bom.has_value());
    if (plain && bom) CHECK(meta_equal(*plain, *bom));
    if (plain) {
        CHECK(plain->seed == 123456789u);
        CHECK(plain->px == 1.5f);
        CHECK(plain->py == 70.25f);
        CHECK(plain->pz == -1000000.5f);
        CHECK(plain->yaw == 0.5f);
        CHECK(plain->pitch == -0.25f);
    }
}

TEST(test_meta_fuzz_no_crash) {
    test::TempDir tmp;
    const fs::path f = tmp.path() / "world.meta";
    const std::string good = sample_meta_text();
    Lcg rng{777};
    int loaded = 0;
    bool finite = true;
    for (int i = 0; i < 2000; ++i) {
        std::string s = good;
        switch (rng.next() % 3) {
        case 0: s[rng.next() % s.size()] = static_cast<char>(rng.next() & 0xFF); break;
        case 1: s.resize(rng.next() % s.size()); break;
        default: {
            const std::size_t at = rng.next() % (s.size() + 1);
            const std::size_t n = 1 + rng.next() % 6;
            for (std::size_t k = 0; k < n; ++k)
                s.insert(s.begin() + static_cast<std::ptrdiff_t>(at), static_cast<char>(rng.next() & 0xFF));
        }
        }
        write_text(f, s);
        if (auto m = load_meta(f)) {
            ++loaded;
            finite = finite && std::isfinite(m->px) && std::isfinite(m->py) && std::isfinite(m->pz) &&
                     std::isfinite(m->yaw) && std::isfinite(m->pitch);
        }
    }
    CHECK(finite);
    CHECK(loaded > 0);  // some mutations (inside an unknown spot, harmless bytes) still load
}

// ---- Region v2: block entities (M4 Task 5) ----

TEST(test_region_v2_roundtrip_block_entities) {
    test::TempDir tmp;
    {
        RegionStore store(tmp.path());
        store.save_column(ColumnPos{-1, -2}, entity_column());
        CHECK(store.flush());
    }
    const std::vector<uint8_t> bytes = read_bytes(tmp.path() / "r.-1.-1.bin");
    CHECK(get_u32(bytes, 4) == 2);  // written as version 2
    RegionStore store(tmp.path());
    const std::optional<Column> col = store.load_column(ColumnPos{-1, -2});
    CHECK(col.has_value());
    if (!col) return;
    CHECK(same_blocks(*col, pattern_column(9)));
    const BlockEntity* k = get_entity(*col, -16, 70, -32);
    CHECK(k && std::holds_alternative<KilnState>(*k));
    if (k && std::holds_alternative<KilnState>(*k)) {
        const KilnState& ks = std::get<KilnState>(*k);
        CHECK(same_stack(ks.in[0], st(blk::COPPER_ORE, 7)));
        CHECK(same_stack(ks.in[1], st(itm::TIN_INGOT, 2)));
        CHECK(same_stack(ks.fuel, st(itm::COAL, 3)));
        CHECK(same_stack(ks.out, st(itm::COPPER_INGOT, 11)));
        CHECK(ks.burn_left == 37.123456f);  // floats exact
        CHECK(ks.burn_total == 80.f);
        CHECK(ks.progress == 3.3333333f);
    }
    const BlockEntity* c = get_entity(*col, -1, 255, -17);
    CHECK(c && std::holds_alternative<CrateState>(*c));
    if (c && std::holds_alternative<CrateState>(*c))
        for (int i = 0; i < 27; ++i)
            CHECK(same_stack(std::get<CrateState>(*c).slots[static_cast<std::size_t>(i)],
                             st(static_cast<ItemId>(itm::PICK_WOOD + i % 21), 1, i)));
    const BlockEntity* g = get_entity(*col, -9, 0, -20);
    CHECK(g && std::holds_alternative<CairnState>(*g));
    if (g && std::holds_alternative<CairnState>(*g)) {
        const CairnState& cs = std::get<CairnState>(*g);
        CHECK(cs.items.size() == 40);
        for (std::size_t i = 0; i < cs.items.size(); ++i) CHECK(same_stack(cs.items[i], st(blk::DIRT, 1 + static_cast<int>(i))));
    }
    std::size_t total = 0;
    for (const auto& ch : *col) total += ch->entities.size();
    CHECK(total == 3);
    // An empty crate and an empty cairn round-trip too.
    {
        RegionStore s2(tmp.path() / "e");
        Column e = pattern_column(1);
        put_entity(e, 3, 5, 3, CrateState{});
        put_entity(e, 4, 5, 3, CairnState{});
        s2.save_column(ColumnPos{0, 0}, e);
        CHECK(s2.flush());
    }
    RegionStore s3(tmp.path() / "e");
    const std::optional<Column> e = s3.load_column(ColumnPos{0, 0});
    CHECK(e && get_entity(*e, 3, 5, 3) && get_entity(*e, 4, 5, 3));
    if (e && get_entity(*e, 4, 5, 3)) CHECK(std::get<CairnState>(*get_entity(*e, 4, 5, 3)).items.empty());
}

TEST(test_region_loads_v1) {
    test::TempDir tmp;
    write_bytes(tmp.path() / "r.0.0.bin", write_region_v1({{ColumnPos{0, 0}, blk::STONE}, {ColumnPos{3, 31}, blk::SAND}}));
    write_bytes(tmp.path() / "r.-1.0.bin", write_region_v1({{ColumnPos{-1, 5}, blk::DIRT}}));
    RegionStore store(tmp.path());
    const std::optional<Column> a = store.load_column(ColumnPos{0, 0});
    const std::optional<Column> b = store.load_column(ColumnPos{3, 31});
    const std::optional<Column> c = store.load_column(ColumnPos{-1, 5});
    CHECK(a && b && c);
    if (!a || !b || !c) return;
    CHECK((*a)[0]->blocks[0] == blk::STONE && (*a)[1]->blocks[0] == blk::AIR);
    CHECK((*b)[0]->blocks[4095] == blk::SAND);
    CHECK((*c)[0]->blocks[100] == blk::DIRT);
    for (const auto& ch : *a) CHECK(ch->entities.empty() && ch->modified);
    // The same v1 bytes with version 0 are rejected.
    std::vector<uint8_t> v0 = write_region_v1({{ColumnPos{0, 0}, blk::STONE}});
    v0[4] = 0;
    RegionStore v0_store(tmp.path() / "v0");  // creates the folder
    write_bytes(tmp.path() / "v0" / "r.0.0.bin", v0);
    CHECK(!v0_store.load_column(ColumnPos{0, 0}).has_value());
    // Saving another column rewrites the v1 region as v2 and keeps the old column.
    store.save_column(ColumnPos{1, 1}, pattern_column(3));
    CHECK(store.flush());
    CHECK(get_u32(read_bytes(tmp.path() / "r.0.0.bin"), 4) == 2);
    RegionStore again(tmp.path());
    const std::optional<Column> a2 = again.load_column(ColumnPos{0, 0});
    CHECK(a2 && (*a2)[0]->blocks[0] == blk::STONE);
}

TEST(test_region_v2_rejects_corrupt) {
    test::TempDir tmp;
    {
        RegionStore store(tmp.path());
        Column col;
        for (std::size_t i = 0; i < col.size(); ++i) col[i] = std::make_unique<Chunk>();
        col[0]->blocks.fill(blk::STONE);  // RLE: (4096, STONE), (61440, AIR)
        put_entity(col, 2, 3, 4, CairnState{{st(blk::DIRT, 5)}});
        store.save_column(ColumnPos{0, 0}, col);
        CHECK(store.flush());
    }
    const fs::path file = tmp.path() / "r.0.0.bin";
    const std::vector<uint8_t> good = read_bytes(file);
    // header 12, entry head 12, RLE 8, be_count at 32, entity at 36: x y z (12), kind (1), payload.
    CHECK(get_u32(good, 32) == 1);
    CHECK(good[48] == 2);  // kind 2 = cairn
    CHECK(good.size() == 36 + 13 + 4 + 5);
    auto set_u32 = [](std::vector<uint8_t>& b, std::size_t at, uint32_t v) {
        for (int k = 0; k < 4; ++k) b[at + static_cast<std::size_t>(k)] = static_cast<uint8_t>((v >> (8 * k)) & 0xFF);
    };
    std::vector<std::vector<uint8_t>> bad;
    bad.push_back(good);
    bad.back()[48] = 9;                       // kind 9
    bad.push_back(good);
    set_u32(bad.back(), 36, 16);              // x = 16 is outside column 0
    bad.push_back(good);
    set_u32(bad.back(), 36, static_cast<uint32_t>(-1));  // x = -1 is outside column 0
    bad.push_back(good);
    set_u32(bad.back(), 40, 256);             // y = 256
    bad.push_back(good);
    set_u32(bad.back(), 32, 1000);            // be_count larger than the remaining bytes
    bad.push_back(good);
    set_u32(bad.back(), 32, 0xFFFFFFFFu);     // absurd be_count
    bad.push_back(good);
    set_u32(bad.back(), 49, 1000);            // cairn item count over the remaining bytes
    bad.push_back(good);
    put_u16(bad.back(), 53, 9999);            // unknown item id in a stack
    bad.push_back(good);
    bad.back()[55] = 65;                      // 65 DIRT is over max_stack
    bad.push_back(good);
    put_u16(bad.back(), 56, 3);               // damage on an item that has no durability
    bad.push_back(good);
    bad.back().resize(good.size() - 1);       // truncated inside the entity
    bad.push_back(good);
    bad.back().resize(34);                    // truncated inside be_count
    for (const auto& b : bad) {
        write_bytes(file, b);
        RegionStore store(tmp.path());
        CHECK(!store.load_column(ColumnPos{0, 0}).has_value());
    }
    // Two entities at the same position.
    std::vector<uint8_t> dup(good.begin(), good.begin() + 36);
    set_u32(dup, 32, 2);
    dup.insert(dup.end(), good.begin() + 36, good.end());
    dup.insert(dup.end(), good.begin() + 36, good.end());
    write_bytes(file, dup);
    {
        RegionStore store(tmp.path());
        CHECK(!store.load_column(ColumnPos{0, 0}).has_value());
    }
    // A kiln with a NaN float.
    {
        RegionStore store(tmp.path() / "k");
        Column col = pattern_column(1);
        KilnState k;
        k.burn_left = 5.f;
        put_entity(col, 1, 1, 1, k);
        store.save_column(ColumnPos{0, 0}, col);
        CHECK(store.flush());
    }
    std::vector<uint8_t> kiln = read_bytes(tmp.path() / "k" / "r.0.0.bin");
    const std::size_t len = get_u32(kiln, 20);
    const std::size_t burn_left_at = 24 + len + 4 + 13 + 4 * 5;  // after the 4 stacks
    CHECK(get_u32(kiln, burn_left_at) == 0x40A00000u);           // 5.0f
    set_u32(kiln, burn_left_at, 0x7FC00000u);                    // NaN
    write_bytes(tmp.path() / "k" / "r.0.0.bin", kiln);
    RegionStore ks(tmp.path() / "k");
    CHECK(!ks.load_column(ColumnPos{0, 0}).has_value());
    // The good file still loads with its cairn.
    write_bytes(file, good);
    RegionStore store(tmp.path());
    const std::optional<Column> col = store.load_column(ColumnPos{0, 0});
    CHECK(col && get_entity(*col, 2, 3, 4));
}

// ---- world.meta inventory and mode (M4 Task 6) ----

TEST(test_meta_inventory_roundtrip) {
    test::TempDir tmp;
    WorldMeta m = SAMPLE_META;
    m.mode = GameMode::Creative;
    for (int i = 0; i < 36; ++i) {
        const auto id = static_cast<ItemId>(i % 2 == 0 ? itm::STICK + i : 1 + i % (blk::COUNT - 1));
        m.inv.slots[static_cast<std::size_t>(i)] = st(id, item_def(id).max_stack == 1 ? 1 : 1 + i % 3);
    }
    m.inv.slots[5] = st(itm::PICK_IRON, 1, 499);
    m.inv.slots[6] = ItemStack{};
    m.inv.armor = {st(itm::IRON_HELMET, 1), ItemStack{}, st(itm::HIDE_LEGS, 1), st(itm::IRON_BOOTS, 1)};
    m.inv.selected = 7;
    CHECK(save_meta(tmp.path() / "world.meta", m));
    const std::optional<WorldMeta> back = load_meta(tmp.path() / "world.meta");
    CHECK(back.has_value());
    if (!back) return;
    CHECK(meta_equal(*back, m));
    CHECK(back->mode == GameMode::Creative);
    for (std::size_t i = 0; i < 36; ++i) CHECK(same_stack(back->inv.slots[i], m.inv.slots[i]));
    CHECK(back->inv.slots[5].damage == 499);
    CHECK(back->inv.slots[6].empty());
    for (std::size_t i = 0; i < 4; ++i) CHECK(same_stack(back->inv.armor[i], m.inv.armor[i]));
    CHECK(back->inv.selected == 7);
    // The keys are plain text, in the task's id:count:damage list form.
    const std::vector<uint8_t> raw = read_bytes(tmp.path() / "world.meta");
    const std::string text(raw.begin(), raw.end());
    CHECK(text.find("\nmode=creative\n") != std::string::npos);
    CHECK(text.find("\ninv=") != std::string::npos);
    CHECK(text.find(":1:499,") != std::string::npos);
    CHECK(text.find("\narmor=298:1:0,0:0:0,296:1:0,301:1:0\n") != std::string::npos);
    CHECK(text.find("\nsel=7\n") != std::string::npos);
}

TEST(test_meta_inventory_default_empty) {
    test::TempDir tmp;
    write_text(tmp.path() / "world.meta", sample_meta_text());  // an M1-M3 file
    const std::optional<WorldMeta> m = load_meta(tmp.path() / "world.meta");
    CHECK(m.has_value());
    if (!m) return;
    CHECK(m->mode == GameMode::Survival);
    for (const ItemStack& s : m->inv.slots) CHECK(s.empty());
    for (const ItemStack& s : m->inv.armor) CHECK(s.empty());
    CHECK(m->inv.selected == 0);
}

TEST(test_meta_inventory_invalid_entries) {
    test::TempDir tmp;
    write_text(tmp.path() / "world.meta",
               sample_meta_text() +
                   "inv=9999:5:0,2:0:0,2:200:0,2:7:0,x:1:0,3:1,273:1:60,256:3:4,,257:64:0\n"
                   "armor=294:1:0,3:1:0,banana,301:1:0\n"
                   "sel=12\n");
    const std::optional<WorldMeta> m = load_meta(tmp.path() / "world.meta");
    CHECK(m.has_value());
    if (!m) return;
    CHECK(m->inv.slots[0].empty());                // id 9999
    CHECK(m->inv.slots[1].empty());                // count 0
    CHECK(m->inv.slots[2].empty());                // count 200
    CHECK(same_stack(m->inv.slots[3], st(blk::DIRT, 7)));
    CHECK(m->inv.slots[4].empty());                // not a number
    CHECK(m->inv.slots[5].empty());                // missing damage
    CHECK(m->inv.slots[6].empty());                // wooden pick damage 60 >= durability 60
    CHECK(m->inv.slots[7].empty());                // damage on a stick
    CHECK(m->inv.slots[8].empty());                // empty entry
    CHECK(same_stack(m->inv.slots[9], st(itm::COAL, 64)));
    for (std::size_t i = 10; i < 36; ++i) CHECK(m->inv.slots[i].empty());
    CHECK(same_stack(m->inv.armor[0], st(itm::HIDE_HELMET, 1)));
    CHECK(m->inv.armor[1].empty());                // STONE is no chest armor
    CHECK(m->inv.armor[2].empty());
    CHECK(same_stack(m->inv.armor[3], st(itm::IRON_BOOTS, 1)));
    CHECK(m->inv.selected == 0);                   // sel outside 0..8
    for (const auto& [text, want] : {std::pair{"sel=9\n", 0}, std::pair{"sel=8\n", 8}, std::pair{"sel=-1\n", 0}}) {
        write_text(tmp.path() / "world.meta", sample_meta_text() + text);
        const std::optional<WorldMeta> s = load_meta(tmp.path() / "world.meta");
        CHECK(s && s->inv.selected == want);
    }
    // More than 36 entries: the extra ones are ignored.
    std::string many = sample_meta_text() + "inv=";
    for (int i = 0; i < 40; ++i) many += "2:1:0,";
    many += "\n";
    write_text(tmp.path() / "world.meta", many);
    const std::optional<WorldMeta> n = load_meta(tmp.path() / "world.meta");
    CHECK(n && n->inv.count(blk::DIRT) == 36);
}

TEST(test_meta_mode_roundtrip) {
    test::TempDir tmp;
    WorldMeta m = SAMPLE_META;
    m.mode = GameMode::Creative;
    CHECK(save_meta(tmp.path() / "world.meta", m));
    CHECK(load_meta(tmp.path() / "world.meta")->mode == GameMode::Creative);
    m.mode = GameMode::Survival;
    CHECK(save_meta(tmp.path() / "world.meta", m));
    CHECK(load_meta(tmp.path() / "world.meta")->mode == GameMode::Survival);
    write_text(tmp.path() / "world.meta", sample_meta_text() + "mode=banana\n");
    const std::optional<WorldMeta> b = load_meta(tmp.path() / "world.meta");
    CHECK(b && b->mode == GameMode::Survival);
    write_text(tmp.path() / "world.meta", sample_meta_text() + "mode=creative\r\n");  // edited on Windows
    const std::optional<WorldMeta> c = load_meta(tmp.path() / "world.meta");
    CHECK(c && c->mode == GameMode::Creative);
}
