// Tests for src/death.*: grave cairn spot and placement, bedroll respawn, sleeping; and the M5
// world.meta keys `vitals` and `spawn` (src/storage.*).
#include "block.hpp"
#include "block_entities.hpp"
#include "chunk.hpp"
#include "coords.hpp"
#include "death.hpp"
#include "inventory.hpp"
#include "items.hpp"
#include "mining.hpp"
#include "storage.hpp"
#include "test.hpp"
#include "vitals.hpp"
#include "world.hpp"
#include "world_time.hpp"

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <variant>

namespace fs = std::filesystem;

namespace {

// Columns cx0..cx1 x cz0..cz1 loaded, every chunk allocated and all AIR, with a STONE floor at
// y = 63 so spawn_player stands at (0.5, 64, 0.5).
World air_world(int cx0 = -1, int cx1 = 1, int cz0 = -1, int cz1 = 1) {
    World w(1);
    for (int cx = cx0; cx <= cx1; ++cx)
        for (int cz = cz0; cz <= cz1; ++cz) {
            Column col;
            for (std::size_t i = 0; i < col.size(); ++i) col[i] = std::make_unique<Chunk>();
            w.insert_column(ColumnPos{cx, cz}, std::move(col));
        }
    for (int x = cx0 * CHUNK; x < (cx1 + 1) * CHUNK; ++x)
        for (int z = cz0 * CHUNK; z < (cz1 + 1) * CHUNK; ++z) w.set(x, 63, z, blk::STONE);
    return w;
}

// Fills the box x0..x1, y0..y1, z0..z1 with b.
void fill(World& w, int x0, int y0, int z0, int x1, int y1, int z1, BlockId b) {
    for (int x = x0; x <= x1; ++x)
        for (int y = y0; y <= y1; ++y)
            for (int z = z0; z <= z1; ++z) w.set(x, y, z, b);
}

ItemStack st(ItemId id, int count, int damage = 0) {
    return ItemStack{id, static_cast<uint8_t>(count), static_cast<uint16_t>(damage)};
}

bool same_stack(const ItemStack& a, const ItemStack& b) {
    return a.id == b.id && a.count == b.count && a.damage == b.damage;
}

bool inventory_empty(const Inventory& inv) {
    for (const ItemStack& s : inv.slots)
        if (!s.empty()) return false;
    for (const ItemStack& s : inv.armor)
        if (!s.empty()) return false;
    return true;
}

void write_text(const fs::path& p, const std::string& s) {
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    out << s;
}

const char* const BASE_META = "version=1\nseed=7\npx=1\npy=70\npz=2\nyaw=0\npitch=0\n";

bool full_vitals(const Vitals& v) {
    const Vitals f;
    return v.health == f.health && v.hunger == f.hunger && v.saturation == f.saturation && v.air == f.air &&
           v.exhaustion == f.exhaustion && v.regen_timer == f.regen_timer && v.starve_timer == f.starve_timer;
}

}  // namespace

TEST(test_cairn_spot_finds_air) {
    World w = air_world();
    std::optional<BlockPos> p = find_cairn_spot(w, Vector3{5.5f, 70.f, 5.5f});
    CHECK(p && *p == (BlockPos{5, 70, 5}));
    // Feet in water: the first non-liquid cell above.
    fill(w, 5, 64, 5, 5, 74, 5, blk::WATER);
    p = find_cairn_spot(w, Vector3{5.5f, 70.2f, 5.5f});
    CHECK(p && *p == (BlockPos{5, 75, 5}));
    // Within 8: water up to 78 still finds 79 (8 above the feet at 71).
    fill(w, 5, 75, 5, 5, 78, 5, blk::WATER);
    p = find_cairn_spot(w, Vector3{5.5f, 71.f, 5.5f});
    CHECK(p && *p == (BlockPos{5, 79, 5}));
    // A plant or torch cell counts as free (it is replaced).
    w.set(-3, 64, -7, blk::TALL_GRASS);
    p = find_cairn_spot(w, Vector3{-2.5f, 64.f, -6.5f});
    CHECK(p && *p == (BlockPos{-3, 64, -7}));
}

TEST(test_cairn_spot_horizontal_fallback) {
    World w = air_world();
    // Feet cell and the 8 above all STONE, in a 7 x 7 box; one AIR cell 2 blocks to the side.
    fill(w, 2, 70, 2, 8, 78, 8, blk::STONE);
    w.set(7, 70, 5, blk::AIR);
    std::optional<BlockPos> p = find_cairn_spot(w, Vector3{5.5f, 70.f, 5.5f});
    CHECK(p && *p == (BlockPos{7, 70, 5}));
    // A nearer (diagonal, distance^2 2) free cell wins; a farther one does not.
    w.set(8, 70, 5, blk::AIR);
    w.set(4, 70, 4, blk::AIR);
    p = find_cairn_spot(w, Vector3{5.5f, 70.f, 5.5f});
    CHECK(p && *p == (BlockPos{4, 70, 4}));
    // Equally near cells: the lowest dz wins, then the lowest dx.
    World tie = air_world();
    fill(tie, 2, 70, 2, 8, 78, 8, blk::STONE);
    tie.set(5, 70, 6, blk::AIR);
    tie.set(5, 70, 4, blk::AIR);
    tie.set(6, 70, 5, blk::AIR);
    p = find_cairn_spot(tie, Vector3{5.5f, 70.f, 5.5f});
    CHECK(p && *p == (BlockPos{5, 70, 4}));
    tie.set(5, 70, 4, blk::STONE);
    tie.set(4, 70, 5, blk::AIR);
    p = find_cairn_spot(tie, Vector3{5.5f, 70.f, 5.5f});
    CHECK(p && *p == (BlockPos{4, 70, 5}));
    // Water to the side is not a spot.
    World v = air_world();
    fill(v, 2, 70, 2, 8, 78, 8, blk::STONE);
    v.set(6, 70, 5, blk::WATER);
    v.set(5, 70, 8, blk::AIR);
    p = find_cairn_spot(v, Vector3{5.5f, 70.f, 5.5f});
    CHECK(p && *p == (BlockPos{5, 70, 8}));
    // Only 9 cells up count: a free cell 9 above the feet is not used.
    World t = air_world();
    fill(t, 2, 70, 2, 8, 79, 8, blk::STONE);
    t.set(5, 79, 5, blk::AIR);
    CHECK(!find_cairn_spot(t, Vector3{5.5f, 70.f, 5.5f}));
}

TEST(test_cairn_spot_none) {
    World w = air_world();
    fill(w, 1, 66, 1, 9, 80, 9, blk::STONE);
    CHECK(!find_cairn_spot(w, Vector3{5.5f, 70.f, 5.5f}));
    // Negative coordinates.
    World n = air_world();
    fill(n, -12, 66, -12, -4, 80, -4, blk::STONE);
    CHECK(!find_cairn_spot(n, Vector3{-7.5f, 70.f, -7.5f}));
    n.set(-10, 70, -8, blk::AIR);
    std::optional<BlockPos> p = find_cairn_spot(n, Vector3{-7.5f, 70.f, -7.5f});
    CHECK(p && *p == (BlockPos{-10, 70, -8}));
}

TEST(test_cairn_spot_unloaded_neighbor) {
    // Only column (0, 0) is loaded; the cells at x >= 16 read as AIR but are never chosen.
    World w = air_world(0, 0, 0, 0);
    fill(w, 12, 66, 2, 15, 80, 8, blk::STONE);
    CHECK(!find_cairn_spot(w, Vector3{15.5f, 70.f, 5.5f}));
    // The player standing in an unloaded column: no spot.
    CHECK(!find_cairn_spot(w, Vector3{40.5f, 70.f, 5.5f}));
}

TEST(test_cairn_spot_chunk_border) {
    // Feet at x = 15 (the last cell of column 0), boxed in; the free cell is x = 16 in column 1.
    World w = air_world();
    fill(w, 12, 66, 2, 18, 80, 8, blk::STONE);
    w.set(16, 70, 5, blk::AIR);
    const std::optional<BlockPos> p = find_cairn_spot(w, Vector3{15.5f, 70.f, 5.5f});
    CHECK(p && *p == (BlockPos{16, 70, 5}));
    Inventory inv;
    inv.slots[0] = st(blk::DIRT, 9);
    place_cairn(w, BlockPos{16, 70, 5}, inv);
    CHECK(w.get(16, 70, 5) == blk::GRAVE_CAIRN);
    CHECK(std::get_if<CairnState>(w.block_entity(BlockPos{16, 70, 5})) != nullptr);
    CHECK(inv.slots[0].empty());
    // Feet at x = -1 (column -1), the free cell at x = 0 (column 0).
    World n = air_world();
    fill(n, -4, 66, 2, 2, 80, 8, blk::STONE);
    n.set(0, 70, 5, blk::AIR);
    const std::optional<BlockPos> q = find_cairn_spot(n, Vector3{-0.5f, 70.f, 5.5f});
    CHECK(q && *q == (BlockPos{0, 70, 5}));
}

TEST(test_cairn_spot_world_top) {
    World w = air_world();
    fill(w, 2, 250, 2, 8, 255, 8, blk::STONE);
    CHECK(!find_cairn_spot(w, Vector3{5.5f, 254.f, 5.5f}));
    w.set(5, 255, 5, blk::AIR);
    std::optional<BlockPos> p = find_cairn_spot(w, Vector3{5.5f, 254.f, 5.5f});
    CHECK(p && *p == (BlockPos{5, 255, 5}));
    // Feet above the world (standing on a block at y = 255) search from y = 255.
    w.set(6, 255, 5, blk::AIR);
    p = find_cairn_spot(w, Vector3{5.5f, 256.f, 5.5f});
    CHECK(p && p->y < 256);
    CHECK(p && *p == (BlockPos{5, 255, 5}));
    World a = air_world();
    p = find_cairn_spot(a, Vector3{5.5f, 300.f, 5.5f});
    CHECK(p && *p == (BlockPos{5, 255, 5}));
    // Feet below the world search from y = 0 (DEEPSTONE-level cells).
    p = find_cairn_spot(a, Vector3{5.5f, -4.f, 5.5f});
    CHECK(p && *p == (BlockPos{5, 0, 5}));
}

TEST(test_cairn_spot_bad_position) {
    World w = air_world();
    const float nan = std::numeric_limits<float>::quiet_NaN();
    CHECK(!find_cairn_spot(w, Vector3{nan, 70.f, 0.5f}));
    CHECK(!find_cairn_spot(w, Vector3{1e30f, 70.f, 0.5f}));
    CHECK(!find_cairn_spot(w, Vector3{-1e30f, 70.f, 0.5f}));
    // A NaN height searches from y = 0.
    const std::optional<BlockPos> p = find_cairn_spot(w, Vector3{0.5f, nan, 0.5f});
    CHECK(p && *p == (BlockPos{0, 0, 0}));
}

TEST(test_cairn_holds_all_items) {
    World w = air_world();
    Inventory inv;
    for (std::size_t i = 0; i < inv.slots.size(); ++i)
        inv.slots[i] = i % 3 == 0 ? st(itm::PICK_IRON, 1, static_cast<int>(i)) : st(blk::DIRT, static_cast<int>(1 + i));
    inv.armor[0] = st(itm::IRON_HELMET, 1);
    inv.armor[1] = st(itm::IRON_CHEST, 1);
    inv.armor[2] = st(itm::HIDE_LEGS, 1);
    inv.armor[3] = st(itm::HIDE_BOOTS, 1);
    const Inventory before = inv;
    const BlockPos p{-3, 70, 4};
    place_cairn(w, p, inv);
    CHECK(w.get(p.x, p.y, p.z) == blk::GRAVE_CAIRN);
    const BlockEntity* be = w.block_entity(p);
    CHECK(be != nullptr && std::holds_alternative<CairnState>(*be));
    if (be != nullptr && std::holds_alternative<CairnState>(*be)) {
        const auto& items = std::get<CairnState>(*be).items;
        CHECK(items.size() == 40);
        if (items.size() == 40) {
            for (std::size_t i = 0; i < 36; ++i) CHECK(same_stack(items[i], before.slots[i]));
            for (std::size_t i = 0; i < 4; ++i) CHECK(same_stack(items[36 + i], before.armor[i]));
        }
    }
    CHECK(inventory_empty(inv));
    // Only non-empty slots are stored, in slot order then armor order.
    World w2 = air_world();
    Inventory few;
    few.slots[4] = st(itm::COAL, 5);
    few.slots[30] = st(itm::STICK, 2);
    few.armor[3] = st(itm::HIDE_BOOTS, 1);
    place_cairn(w2, BlockPos{0, 64, 0}, few);
    const auto* cs = std::get_if<CairnState>(w2.block_entity(BlockPos{0, 64, 0}));
    CHECK(cs != nullptr && cs->items.size() == 3);
    if (cs != nullptr && cs->items.size() == 3) {
        CHECK(same_stack(cs->items[0], st(itm::COAL, 5)));
        CHECK(same_stack(cs->items[1], st(itm::STICK, 2)));
        CHECK(same_stack(cs->items[2], st(itm::HIDE_BOOTS, 1)));
    }
}

TEST(test_cairn_empty_inventory) {
    World w = air_world();
    Inventory inv;
    place_cairn(w, BlockPos{1, 70, 1}, inv);
    CHECK(w.get(1, 70, 1) == blk::AIR);
    CHECK(w.block_entity(BlockPos{1, 70, 1}) == nullptr);
    // An unloaded or out-of-world cell places nothing and leaves the inventory alone.
    Inventory some;
    some.slots[0] = st(blk::DIRT, 3);
    place_cairn(w, BlockPos{100, 70, 1}, some);
    CHECK(same_stack(some.slots[0], st(blk::DIRT, 3)));
    place_cairn(w, BlockPos{1, 256, 1}, some);
    CHECK(same_stack(some.slots[0], st(blk::DIRT, 3)));
}

TEST(test_cairn_break_drops) {
    World w = air_world();
    Inventory inv;
    inv.slots[0] = st(itm::SWORD_IRON, 1, 17);
    inv.slots[9] = st(blk::PLANKS, 64);
    inv.armor[1] = st(itm::HIDE_CHEST, 1);
    const BlockPos p{2, 70, 2};
    place_cairn(w, p, inv);
    ItemStack hand{};
    const std::vector<ItemStack> drops =
        break_drops(w.get(p.x, p.y, p.z), w.block_entity(p), hand, GameMode::Survival, 12345u);
    CHECK(drops.size() == 3);  // the stored stacks, and no cairn block item
    if (drops.size() == 3) {
        CHECK(same_stack(drops[0], st(itm::SWORD_IRON, 1, 17)));
        CHECK(same_stack(drops[1], st(blk::PLANKS, 64)));
        CHECK(same_stack(drops[2], st(itm::HIDE_CHEST, 1)));
    }
    // The cairn block itself is breakable by hand and drops nothing.
    CHECK(block_def(blk::GRAVE_CAIRN).breakable);
    CHECK(block_def(blk::GRAVE_CAIRN).drop == 0);
}

TEST(test_respawn_uses_bedroll) {
    World w = air_world();
    w.set(10, 70, -3, blk::BEDROLL);
    Vector3 r = respawn_point(w, BlockPos{10, 70, -3});
    CHECK(r.x == 10.5f && r.y == 71.f && r.z == -2.5f);
    // Plants or a torch above do not block it.
    w.set(10, 71, -3, blk::TORCH);
    w.set(10, 72, -3, blk::FLOWER);
    r = respawn_point(w, BlockPos{10, 70, -3});
    CHECK(r.x == 10.5f && r.y == 71.f && r.z == -2.5f);
}

TEST(test_respawn_fallback_when_bedroll_gone) {
    World w = air_world();
    w.set(10, 70, -3, blk::BEDROLL);
    w.set(10, 70, -3, blk::AIR);
    Vector3 r = respawn_point(w, BlockPos{10, 70, -3});
    CHECK(r.x == 0.5f && r.y == 64.f && r.z == 0.5f);
    r = respawn_point(w, std::nullopt);
    CHECK(r.x == 0.5f && r.y == 64.f && r.z == 0.5f);
    // A bedroll in an unloaded column reads as AIR: the world spawn.
    r = respawn_point(w, BlockPos{200, 70, 0});
    CHECK(r.x == 0.5f && r.y == 64.f && r.z == 0.5f);
}

TEST(test_respawn_fallback_when_bedroll_blocked) {
    for (int above : {1, 2}) {
        World w = air_world();
        w.set(10, 70, -3, blk::BEDROLL);
        w.set(10, 70 + above, -3, blk::STONE);
        const Vector3 r = respawn_point(w, BlockPos{10, 70, -3});
        CHECK(r.x == 0.5f && r.y == 64.f && r.z == 0.5f);
    }
    World wet = air_world();
    wet.set(10, 70, -3, blk::BEDROLL);
    wet.set(10, 71, -3, blk::WATER);
    const Vector3 r = respawn_point(wet, BlockPos{10, 70, -3});
    CHECK(r.x == 0.5f && r.y == 64.f && r.z == 0.5f);
    // Three cells up is not checked.
    World high = air_world();
    high.set(10, 70, -3, blk::BEDROLL);
    high.set(10, 73, -3, blk::STONE);
    const Vector3 h = respawn_point(high, BlockPos{10, 70, -3});
    CHECK(h.x == 10.5f && h.y == 71.f && h.z == -2.5f);
}

TEST(test_bedroll_usable) {
    World w = air_world();
    const BlockPos b{-4, 64, 9};
    CHECK(!bedroll_usable(w, b));  // AIR
    w.set(b.x, b.y, b.z, blk::BEDROLL);
    CHECK(bedroll_usable(w, b));
    w.set(b.x, b.y + 2, b.z, blk::LEAVES);
    CHECK(!bedroll_usable(w, b));
    w.set(b.x, b.y + 2, b.z, blk::AIR);
    w.set(b.x, b.y + 1, b.z, blk::WATER);
    CHECK(!bedroll_usable(w, b));
    CHECK(!bedroll_usable(w, BlockPos{300, 64, 9}));  // unloaded
    // At the world top the cells above read as AIR.
    w.set(b.x, 255, b.z, blk::BEDROLL);
    CHECK(bedroll_usable(w, BlockPos{b.x, 255, b.z}));
}

TEST(test_can_sleep) {
    WorldTime t;
    t.ticks = 6000;
    CHECK(!can_sleep(t, false));
    t.ticks = 15599;
    CHECK(!can_sleep(t, false));
    t.ticks = 15600;
    CHECK(can_sleep(t, false));
    CHECK(!can_sleep(t, true));
    t.ticks = 23999;
    CHECK(can_sleep(t, false));
    t.ticks = 24000;  // next sunrise
    CHECK(!can_sleep(t, false));
    t.ticks = 24000.0 * 3 + 20000;
    CHECK(can_sleep(t, false));
}

TEST(test_sleep_until_dawn) {
    WorldTime t;
    t.ticks = 20000;
    sleep_until_dawn(t);
    CHECK(t.ticks == 24000.0);
    CHECK(t.day_tick() == 0);
    t.ticks = 24000.0 + 15600;
    sleep_until_dawn(t);
    CHECK(t.ticks == 48000.0);
    t.ticks = 24000.0 * 100 + 23999.5;
    sleep_until_dawn(t);
    CHECK(t.ticks == 24000.0 * 101);
}

TEST(test_meta_vitals_roundtrip) {
    test::TempDir tmp;
    const fs::path file = tmp.path() / "world.meta";
    WorldMeta m{7, 1.f, 70.f, 2.f, 0.f, 0.f};
    m.vitals.health = 13.5f;
    m.vitals.hunger = 7.f;
    m.vitals.saturation = 2.4f;
    m.vitals.air = 3.25f;
    m.vitals.exhaustion = 1.7f;
    m.vitals.regen_timer = 2.5f;
    m.vitals.starve_timer = 0.75f;
    m.spawn = BlockPos{-100, 70, 2000000};
    CHECK(save_meta(file, m));
    const std::optional<WorldMeta> l = load_meta(file);
    CHECK(l.has_value());
    if (!l) return;
    CHECK(l->vitals.health == 13.5f);
    CHECK(l->vitals.hunger == 7.f);
    CHECK(l->vitals.saturation == 2.4f);
    CHECK(l->vitals.air == 3.25f);
    CHECK(l->vitals.exhaustion == 1.7f);
    CHECK(l->vitals.regen_timer == 2.5f);
    CHECK(l->vitals.starve_timer == 0.75f);
    CHECK(l->spawn && *l->spawn == (BlockPos{-100, 70, 2000000}));
    // No bedroll: no spawn key, loads as nullopt. Dead (health 0) round-trips too.
    WorldMeta d{7, 1.f, 70.f, 2.f, 0.f, 0.f};
    d.vitals.health = 0.f;
    CHECK(save_meta(file, d));
    const std::optional<WorldMeta> ld = load_meta(file);
    CHECK(ld && !ld->spawn && ld->vitals.health == 0.f);
}

TEST(test_meta_vitals_invalid) {
    test::TempDir tmp;
    const fs::path file = tmp.path() / "world.meta";
    auto load = [&](const std::string& extra) {
        write_text(file, std::string(BASE_META) + extra);
        return load_meta(file);
    };
    std::optional<WorldMeta> m = load("vitals=25,20,5,10,0,0,0\n");
    CHECK(m && m->vitals.health == 20.f && m->vitals.hunger == 20.f);
    m = load("vitals=20,-3,5,10,0,0,0\n");
    CHECK(m && m->vitals.hunger == 0.f);
    CHECK(m && m->vitals.saturation == 0.f);  // saturation never above hunger
    m = load("vitals=20,20,5,99,12,7,-1\n");
    CHECK(m && m->vitals.air == 10.f && m->vitals.exhaustion == 10.f);
    CHECK(m && m->vitals.regen_timer == 4.f && m->vitals.starve_timer == 0.f);
    // Exhaustion just after a regeneration (3.9 + 6) is a real in-game value and is kept.
    m = load("vitals=20,20,5,10,9.9,0,0\n");
    CHECK(m && m->vitals.exhaustion == 9.9f);
    m = load("vitals=-4,20,5,10,0,0,0\n");
    CHECK(m && m->vitals.health == 0.f);
    for (const char* bad : {"vitals=nan,20,5,10,0,0,0\n", "vitals=20,20,inf,10,0,0,0\n", "vitals=1,2\n",
                            "vitals=1,2,3,4,5,6,7,8\n", "vitals=a,b,c,d,e,f,g\n", "vitals=\n",
                            "vitals=20,20,5,10,0,0,\n"}) {
        m = load(bad);
        CHECK(m && full_vitals(m->vitals));
    }
    // A pre-M5 file: full vitals and no bedroll spawn.
    m = load("time=100\nmode=survival\n");
    CHECK(m && full_vitals(m->vitals) && !m->spawn);
    // Bad spawn values load as no bedroll; the rest of the file still loads.
    for (const char* bad : {"spawn=1,2\n", "spawn=1,256,2\n", "spawn=1,-1,2\n", "spawn=a,b,c\n", "spawn=1,2,3,4\n",
                            "spawn=1.5,2,3\n", "spawn=\n"}) {
        m = load(bad);
        CHECK(m && !m->spawn);
    }
    m = load("spawn=-5,0,7\n");
    CHECK(m && m->spawn && *m->spawn == (BlockPos{-5, 0, 7}));
    m = load("spawn=-5,255,7\n");
    CHECK(m && m->spawn && *m->spawn == (BlockPos{-5, 255, 7}));
}
