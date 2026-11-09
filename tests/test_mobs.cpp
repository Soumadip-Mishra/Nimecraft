// Tests for src/mobs.*: creature definitions, ids, falling and freezing, and the mob raycast.
#include "block.hpp"
#include "chunk.hpp"
#include "coords.hpp"
#include "items.hpp"
#include "mobs.hpp"
#include "terrain.hpp"
#include "test.hpp"
#include "world.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <memory>
#include <optional>
#include <set>
#include <vector>

namespace {

// Columns (-3..2, -3..2) with STONE at y = 0..69: the floor top is y = 70, x and z in [-48, 48).
World flat() {
    World w(1);
    for (int cx = -3; cx <= 2; ++cx)
        for (int cz = -3; cz <= 2; ++cz) {
            Column col;
            for (std::size_t i = 0; i < col.size(); ++i) col[i] = std::make_unique<Chunk>();
            for (int y = 0; y < 70; ++y)
                for (int lz = 0; lz < CHUNK; ++lz)
                    for (int lx = 0; lx < CHUNK; ++lx)
                        col[static_cast<std::size_t>(y / CHUNK)]->blocks[static_cast<std::size_t>(
                            Chunk::index(lx, y % CHUNK, lz))] = blk::STONE;
            w.insert_column(ColumnPos{cx, cz}, std::move(col));
        }
    return w;
}

// No player to react to: AI leaves every mob idle or wandering near its spawn.
AiContext quiet(const World& w) {
    AiContext c{};
    c.player_pos = Vector3{0.f, -500.f, 0.f};
    c.player_alive = false;
    c.daylight = 0.f;
    c.world = &w;
    return c;
}

void run(MobManager& m, const World& w, float seconds, float step = 1.f / 60.f) {
    const AiContext c = quiet(w);
    const int n = static_cast<int>(std::lround(seconds / step));
    for (int i = 0; i < n; ++i) m.update(w, c, step);
}

const Mob* find(const MobManager& m, uint32_t id) {
    for (const Mob& mob : m.all())
        if (mob.id == id) return &mob;
    return nullptr;
}

constexpr MobType ALL_TYPES[] = {MobType::Tusker,      MobType::Fluffhorn,    MobType::Hopper,
                                 MobType::Gloomwalker, MobType::Thornspitter, MobType::CaveCrawler};

}  // namespace

TEST(test_mob_falls_and_lands) {
    const World w = flat();
    MobManager m;
    std::vector<uint32_t> ids;
    float x = -30.5f;
    for (MobType t : ALL_TYPES) {
        ids.push_back(m.spawn(t, Vector3{x, 80.f, -20.5f}, 7u));
        x += 11.f;  // apart, so no herds form; wandering (within 8) stays on the floor
    }
    run(m, w, 3.f);
    for (uint32_t id : ids) {
        const Mob* mob = find(m, id);
        CHECK(mob != nullptr);
        if (mob == nullptr) continue;
        CHECK_NEAR(mob->pos.y, 70.0, 1e-4);
        CHECK(mob->vel.y <= 0.f && mob->vel.y > -1.f);
    }
}

TEST(test_mob_falls_negative_coords) {
    const World w = flat();
    MobManager m;
    const uint32_t id = m.spawn(MobType::Gloomwalker, Vector3{-16.f, 75.f, -0.01f}, 3u);
    run(m, w, 2.f);
    const Mob* mob = find(m, id);
    CHECK(mob != nullptr && std::fabs(mob->pos.y - 70.f) < 1e-4f);
}

TEST(test_mob_long_dt_matches_steps) {
    // dt = 1.0 runs as 60 steps of 1/60, so the fall ends where 60 small updates end.
    const World w = flat();
    MobManager a, b;
    const uint32_t ia = a.spawn(MobType::Hopper, Vector3{0.5f, 90.f, 0.5f}, 5u);
    const uint32_t ib = b.spawn(MobType::Hopper, Vector3{0.5f, 90.f, 0.5f}, 5u);
    const AiContext c = quiet(w);
    a.update(w, c, 1.f);
    for (int i = 0; i < 60; ++i) b.update(w, c, 1.f / 60.f);
    const Mob* ma = find(a, ia);
    const Mob* mb = find(b, ib);
    CHECK(ma != nullptr && mb != nullptr);
    if (ma == nullptr || mb == nullptr) return;
    CHECK_NEAR(ma->pos.x, mb->pos.x, 1e-4);
    CHECK_NEAR(ma->pos.y, mb->pos.y, 1e-4);
    CHECK_NEAR(ma->pos.z, mb->pos.z, 1e-4);
    CHECK(ma->pos.y < 90.f);  // it fell
}

TEST(test_mob_update_bad_dt) {
    const World w = flat();
    MobManager m;
    const uint32_t id = m.spawn(MobType::Tusker, Vector3{0.5f, 80.f, 0.5f}, 1u);
    const AiContext c = quiet(w);
    m.update(w, c, 0.f);
    m.update(w, c, -1.f);
    m.update(w, c, std::numeric_limits<float>::quiet_NaN());
    m.update(w, c, std::numeric_limits<float>::infinity());
    const Mob* mob = find(m, id);
    CHECK(mob != nullptr && mob->pos.y == 80.f && mob->vel.y == 0.f);
}

TEST(test_mob_frozen_when_unloaded) {
    const World w = flat();
    MobManager m;
    const uint32_t id = m.spawn(MobType::Fluffhorn, Vector3{100.5f, 80.f, 0.5f}, 9u);  // column 6: unloaded
    const uint32_t neg = m.spawn(MobType::Gloomwalker, Vector3{-48.5f, 80.f, 0.5f}, 9u);  // column -4
    run(m, w, 2.f);
    const Mob* mob = find(m, id);
    CHECK(mob != nullptr);
    if (mob == nullptr) return;
    CHECK(mob->pos.x == 100.5f && mob->pos.y == 80.f && mob->pos.z == 0.5f);
    CHECK(mob->vel.y == 0.f);
    const Mob* n = find(m, neg);
    CHECK(n != nullptr && n->pos.y == 80.f && n->vel.y == 0.f);
    // A mob given a falling speed and then left in an unloaded column loses it.
    m.all()[0].vel.y = -10.f;
    run(m, w, 0.5f);
    CHECK(m.all()[0].vel.y == 0.f && m.all()[0].pos.y == 80.f);
}

TEST(test_mob_raycast_hits_nearest) {
    const World w = flat();
    MobManager m;
    const uint32_t far = m.spawn(MobType::Tusker, Vector3{5.f, 70.f, 0.5f}, 1u);  // spawned first
    const uint32_t near = m.spawn(MobType::Tusker, Vector3{3.f, 70.f, 0.5f}, 2u);
    const Vector3 o{0.f, 70.5f, 0.5f}, dir{1.f, 0.f, 0.f};
    CHECK(m.raycast(o, dir, 10.f) == std::optional<uint32_t>(near));
    CHECK(!m.raycast(o, dir, 2.f).has_value());
    // The near box starts at x = 3 - 0.45 = 2.55: exactly at max hits, just below misses.
    CHECK(m.raycast(o, dir, 2.55f) == std::optional<uint32_t>(near));
    CHECK(!m.raycast(o, dir, 2.54f).has_value());
    CHECK(!m.raycast(Vector3{0.f, 70.5f, 3.f}, dir, 10.f).has_value());  // beside both
    CHECK(m.raycast(Vector3{5.f, 70.5f, 0.5f}, dir, 2.f) == std::optional<uint32_t>(far));  // inside
    CHECK(!m.raycast(o, Vector3{-1.f, 0.f, 0.f}, 10.f).has_value());  // pointing away
    CHECK(!m.raycast(o, Vector3{0.f, 0.f, 0.f}, 10.f).has_value());
    CHECK(!m.raycast(o, dir, std::numeric_limits<float>::quiet_NaN()).has_value());
    CHECK(!m.raycast(o, dir, std::numeric_limits<float>::infinity()).has_value());
    // Over a mob's top and below its feet.
    CHECK(!m.raycast(Vector3{0.f, 71.f, 0.5f}, dir, 10.f).has_value());  // Tusker height 0.9
    CHECK(!m.raycast(Vector3{0.f, 69.9f, 0.5f}, dir, 10.f).has_value());
}

TEST(test_mob_raycast_negative_coords) {
    MobManager m;
    const uint32_t id = m.spawn(MobType::Gloomwalker, Vector3{-1000.5f, 10.f, -2000.5f}, 1u);
    const Vector3 o{-1000.5f, 11.f, -1995.f};
    CHECK(m.raycast(o, Vector3{0.f, 0.f, -1.f}, 6.f) == std::optional<uint32_t>(id));  // box at z <= -2000.2
    CHECK(!m.raycast(o, Vector3{0.f, 0.f, -1.f}, 4.f).has_value());
    CHECK(!MobManager{}.raycast(o, Vector3{0.f, 0.f, -1.f}, 6.f).has_value());  // no mobs
}

TEST(test_mob_ids_unique) {
    MobManager m;
    std::set<uint32_t> seen;
    for (int i = 0; i < 100; ++i) seen.insert(m.spawn(MobType::Hopper, Vector3{0.f, 80.f, 0.f}, 1u));
    CHECK(seen.size() == 100);
    CHECK(m.all().size() == 100);
    const uint32_t gone = m.all()[10].id;
    m.remove(gone);
    CHECK(m.all().size() == 99);
    for (const Mob& mob : m.all()) CHECK(mob.id != gone);
    const uint32_t fresh = m.spawn(MobType::Hopper, Vector3{0.f, 80.f, 0.f}, 1u);
    CHECK(seen.count(fresh) == 0);  // never reused
    m.remove(fresh);
    m.remove(fresh);       // already removed: nothing happens
    m.remove(0xFFFFFFFFu);  // unknown
    CHECK(m.all().size() == 99);
    MobManager empty;
    empty.remove(1);
    CHECK(empty.all().empty());
}

TEST(test_mob_spawn_state) {
    MobManager m;
    const uint32_t id = m.spawn(MobType::Gloomwalker, Vector3{1.f, 2.f, 3.f}, 0u);
    const Mob* mob = find(m, id);
    CHECK(mob != nullptr);
    if (mob == nullptr) return;
    CHECK(mob->type == MobType::Gloomwalker);
    CHECK(mob->pos.x == 1.f && mob->pos.y == 2.f && mob->pos.z == 3.f);
    CHECK(mob->vel.x == 0.f && mob->vel.y == 0.f && mob->vel.z == 0.f);
    CHECK(mob->health == 20.f);
    CHECK(mob->state == AiState::Idle);
    CHECK(mob->path.empty() && mob->path_i == 0);
    CHECK(mob->rng != 0u);  // seed 0 is replaced, xorshift would stay at 0 forever
    CHECK(m.find(id) == &m.all()[0]);
    CHECK(m.find(id + 1) == nullptr);
    const uint32_t a = m.spawn(MobType::Tusker, Vector3{}, 42u);
    const uint32_t b = m.spawn(MobType::Tusker, Vector3{}, 42u);
    CHECK(find(m, a)->rng == find(m, b)->rng);
}

TEST(test_mob_defs_table) {
    struct Row {
        MobType t;
        float half_w, height, health, speed, damage;
    };
    const Row rows[] = {
        {MobType::Tusker, 0.45f, 0.9f, 10.f, 3.f, 3.f},
        {MobType::Fluffhorn, 0.45f, 1.3f, 8.f, 2.5f, 0.f},
        {MobType::Hopper, 0.2f, 0.5f, 3.f, 5.f, 0.f},
        {MobType::Gloomwalker, 0.3f, 1.9f, 20.f, 3.5f, 4.f},
        {MobType::Thornspitter, 0.35f, 1.2f, 14.f, 2.5f, 3.f},
        {MobType::CaveCrawler, 0.3f, 0.5f, 6.f, 5.f, 2.f},
    };
    for (const Row& r : rows) {
        const MobDef& d = mob_def(r.t);
        CHECK(d.half_w == r.half_w);
        CHECK(d.height == r.height);
        CHECK(d.health == r.health);
        CHECK(d.speed == r.speed);
        CHECK(d.damage == r.damage);
        CHECK(d.reach == 1.2f);
        CHECK(!d.name.empty());
        const bool hostile = r.t == MobType::Gloomwalker || r.t == MobType::Thornspitter ||
                             r.t == MobType::CaveCrawler;
        CHECK(d.hostile == hostile);
        for (const LootEntry& e : d.loot) {
            CHECK(e.min <= e.max);
            CHECK(e.chance > 0.f && e.chance <= 1.f);
        }
    }
    CHECK(mob_def(MobType::Gloomwalker).night_creature);
    CHECK(mob_def(MobType::Thornspitter).night_creature);
    CHECK(!mob_def(MobType::Tusker).night_creature);
    CHECK(!mob_def(MobType::CaveCrawler).night_creature);
    // Names are distinct.
    for (int i = 0; i < 6; ++i)
        for (int j = i + 1; j < 6; ++j) CHECK(mob_def(ALL_TYPES[i]).name != mob_def(ALL_TYPES[j]).name);
    // An out-of-range type gives the Tusker definition instead of reading past the table.
    CHECK(mob_def(MobType::COUNT).name == mob_def(MobType::Tusker).name);
    CHECK(mob_def(static_cast<MobType>(200)).name == mob_def(MobType::Tusker).name);
}

TEST(test_mob_loot_table) {
    struct Want {
        ItemId item;
        uint8_t min, max;
    };
    auto has = [](MobType t, std::vector<Want> want) {
        const std::vector<LootEntry>& loot = mob_def(t).loot;
        if (loot.size() != want.size()) return false;
        for (std::size_t i = 0; i < want.size(); ++i)
            if (loot[i].item != want[i].item || loot[i].min != want[i].min || loot[i].max != want[i].max)
                return false;
        return true;
    };
    CHECK(has(MobType::Tusker, {{itm::RAW_MEAT, 1, 3}, {itm::HIDE, 0, 2}}));
    CHECK(has(MobType::Fluffhorn, {{itm::WOOL, 1, 2}, {itm::RAW_MEAT, 1, 2}}));
    CHECK(has(MobType::Hopper, {{itm::SMALL_MEAT, 1, 1}}));
    CHECK(has(MobType::Gloomwalker, {{itm::GLOOM_DUST, 0, 2}}));
    CHECK(has(MobType::Thornspitter, {{itm::FIBER, 1, 3}}));
    CHECK(has(MobType::CaveCrawler, {{itm::SILK, 0, 2}}));
}

TEST(test_mob_biomes_table) {
    auto lives = [](MobType t, Biome b) {
        const std::vector<Biome>& v = mob_def(t).biomes;
        return std::find(v.begin(), v.end(), b) != v.end();
    };
    CHECK(lives(MobType::Tusker, Biome::Forest) && lives(MobType::Tusker, Biome::Meadow));
    CHECK(!lives(MobType::Tusker, Biome::Desert));
    CHECK(lives(MobType::Fluffhorn, Biome::Highlands) && lives(MobType::Fluffhorn, Biome::Snowfield));
    CHECK(!lives(MobType::Fluffhorn, Biome::Meadow));
    CHECK(lives(MobType::Hopper, Biome::Meadow) && lives(MobType::Hopper, Biome::Desert));
    CHECK(!lives(MobType::Hopper, Biome::Forest));
    CHECK(lives(MobType::Thornspitter, Biome::Forest) && !lives(MobType::Thornspitter, Biome::Meadow));
    // Gloomwalker and Cave Crawler spawn by light and depth, in every biome: empty list.
    CHECK(mob_def(MobType::Gloomwalker).biomes.empty());
    CHECK(mob_def(MobType::CaveCrawler).biomes.empty());
}

TEST(test_mob_column_stash_and_restore) {
    MobManager m;
    const uint32_t a = m.spawn(MobType::Tusker, Vector3{-0.5f, 70.f, 3.5f}, 1u);      // column (-1, 0)
    const uint32_t b = m.spawn(MobType::Gloomwalker, Vector3{-8.5f, 70.f, 8.5f}, 1u);  // column (-1, 0)
    const uint32_t c = m.spawn(MobType::Hopper, Vector3{0.f, 70.f, 3.5f}, 1u);         // column (0, 0)
    m.find(a)->yaw = 2.25f;
    m.find(a)->health = 6.5f;
    // Copy only.
    const std::vector<SavedMob> copy = m.saved_in(ColumnPos{-1, 0});
    CHECK(copy.size() == 1 && m.all().size() == 3);
    // Take: every mob in the column leaves, the passive ones come back as SavedMob.
    const std::vector<SavedMob> taken = m.take_column(ColumnPos{-1, 0});
    CHECK(taken.size() == 1);
    if (taken.size() == 1)
        CHECK(taken[0].type == 0 && taken[0].x == -0.5f && taken[0].y == 70.f && taken[0].z == 3.5f &&
              taken[0].yaw == 2.25f && taken[0].health == 6.5f);
    CHECK(m.find(a) == nullptr && m.find(b) == nullptr && m.find(c) != nullptr);
    CHECK(m.take_column(ColumnPos{5, 5}).empty());
    // Into a column: by feet height, the chunk marked modified.
    Column col;
    for (std::size_t i = 0; i < col.size(); ++i) col[i] = std::make_unique<Chunk>();
    stash_mobs(col, taken);
    CHECK(col[4]->mobs.size() == 1 && col[4]->modified);
    CHECK(!col[5]->modified);
    std::vector<SavedMob> edge{SavedMob{1, 1.f, -9.f, 1.f, 0.f, 8.f}, SavedMob{1, 1.f, 999.f, 1.f, 0.f, 8.f}};
    stash_mobs(col, edge);
    CHECK(col[0]->mobs.size() == 1 && col[15]->mobs.size() == 1);
    stash_mobs(col, {});
    // Restore: position, yaw, and health come back; bad entries are skipped.
    MobManager r;
    uint32_t rng = 3;
    std::vector<SavedMob> back = taken;
    back.push_back(SavedMob{200, 0.f, 70.f, 0.f, 0.f, 5.f});       // unknown type
    back.push_back(SavedMob{3, 0.f, 70.f, 0.f, 0.f, 5.f});         // hostile
    back.push_back(SavedMob{1, 0.f, 70.f, 0.f, 0.f, 50.f});        // health over the max: clamped
    back.push_back(SavedMob{1, 0.f, 70.f, 0.f, 0.f, -1.f});        // dead
    r.restore(back, rng);
    CHECK(r.all().size() == 2);
    if (r.all().size() == 2) {
        const Mob& t = r.all()[0];
        CHECK(t.type == MobType::Tusker && t.pos.x == -0.5f && t.pos.z == 3.5f && t.yaw == 2.25f && t.health == 6.5f);
        CHECK(r.all()[1].type == MobType::Fluffhorn && r.all()[1].health == 8.f);
    }
}

TEST(test_mob_walk_distance) {
    // Walking adds the horizontal distance moved (for the leg swing); standing adds nothing.
    const World w = flat();
    MobManager m;
    const uint32_t id = m.spawn(MobType::Gloomwalker, Vector3{0.5f, 70.f, 0.5f}, 1u);
    AiContext c = quiet(w);
    c.player_alive = true;
    c.player_pos = Vector3{10.5f, 70.f, 0.5f};
    for (int i = 0; i < 30; ++i) m.update(w, c, 1.f / 60.f);
    const Mob* mob = m.find(id);
    CHECK(mob != nullptr && mob->walk > 1.5f && mob->walk < 2.f);  // 3.5 blocks/s for 0.5 s
}

TEST(test_regress_mob_stays_in_loaded_column) {
    // World::get reads unloaded columns as AIR, so a mob walking or knocked over the loaded edge
    // used to step into column 3 (x >= 48), freeze there, and miss every save.
    const World w = flat();
    MobManager m;
    const uint32_t knocked = m.spawn(MobType::Hopper, Vector3{47.f, 70.f, 0.5f}, 1u);
    CHECK(m.damage(knocked, 1.f, Vector3{40.f, 70.f, 0.5f}));  // pushed toward +X
    const uint32_t chaser = m.spawn(MobType::CaveCrawler, Vector3{46.5f, 70.f, -10.5f}, 1u);
    AiContext c = quiet(w);
    c.player_alive = true;
    c.player_pos = Vector3{58.5f, 70.f, -10.5f};  // beyond the edge, in sight
    for (int i = 0; i < 120; ++i) m.update(w, c, 1.f / 60.f);
    for (uint32_t id : {knocked, chaser}) {
        const Mob* mob = m.find(id);
        CHECK(mob != nullptr && mob->pos.x < 48.f);
        if (mob != nullptr) CHECK(mob->pos.y == 70.f);  // standing on the floor, not falling
    }
    // Negative side too (column -4 starts below x = -48).
    const uint32_t west = m.spawn(MobType::Hopper, Vector3{-47.5f, 70.f, 0.5f}, 2u);
    CHECK(m.damage(west, 1.f, Vector3{-40.f, 70.f, 0.5f}));
    for (int i = 0; i < 60; ++i) m.update(w, quiet(w), 1.f / 60.f);
    CHECK(m.find(west) != nullptr && m.find(west)->pos.x >= -48.f);
}
