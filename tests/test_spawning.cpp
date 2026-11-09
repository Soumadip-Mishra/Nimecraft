// Tests for src/spawning.*: spawn rules by light, biome, depth, and space; caps; the spawn ring;
// and despawning far hostiles.
#include "block.hpp"
#include "chunk.hpp"
#include "coords.hpp"
#include "mobs.hpp"
#include "spawning.hpp"
#include "terrain.hpp"
#include "test.hpp"
#include "world.hpp"

#include <cmath>
#include <limits>
#include <cstddef>
#include <memory>
#include <vector>

namespace {

constexpr uint32_t SEED = 1;

// Columns within `r` columns of (ccx, ccz): STONE at y = 0..68 and `ground` at y = 69, so feet
// stand at y = 70. All light is 0.
World flat_at(int ccx, int ccz, int r, BlockId ground) {
    World w(SEED);
    for (int cx = ccx - r; cx < ccx + r; ++cx)
        for (int cz = ccz - r; cz < ccz + r; ++cz) {
            Column col;
            for (std::size_t i = 0; i < col.size(); ++i) col[i] = std::make_unique<Chunk>();
            for (int y = 0; y < 70; ++y)
                for (int lz = 0; lz < CHUNK; ++lz)
                    for (int lx = 0; lx < CHUNK; ++lx)
                        col[static_cast<std::size_t>(y / CHUNK)]->blocks[static_cast<std::size_t>(
                            Chunk::index(lx, y % CHUNK, lz))] = y == 69 ? ground : blk::STONE;
            w.insert_column(ColumnPos{cx, cz}, std::move(col));
        }
    return w;
}

void put(World& w, int x, int y, int z, BlockId b) {
    w.chunk(chunk_of(x, y, z))->set(floor_mod(x, CHUNK), floor_mod(y, CHUNK), floor_mod(z, CHUNK), b);
}

void light(World& w, BlockPos p, uint8_t sky, uint8_t block) {
    Chunk* c = w.chunk(chunk_of(p.x, p.y, p.z));
    const int i = Chunk::index(floor_mod(p.x, CHUNK), floor_mod(p.y, CHUNK), floor_mod(p.z, CHUNK));
    set_sky_light(*c, i, sky);
    set_block_light(*c, i, block);
}

// Sky light 15 on every feet cell (y = 70) of the loaded columns.
void open_sky(World& w) {
    for (ColumnPos c : w.loaded_columns())
        for (int lz = 0; lz < CHUNK; ++lz)
            for (int lx = 0; lx < CHUNK; ++lx) light(w, BlockPos{c.x * CHUNK + lx, 70, c.z * CHUNK + lz}, 15, 0);
}

// The first cell on a 16-block grid (spiral-free scan outward) whose biome is b.
BlockPos find_biome(Biome b) {
    for (int r = 0; r < 400; ++r)
        for (int dx = -r; dx <= r; ++dx)
            for (int dz = -r; dz <= r; ++dz) {
                if (std::abs(dx) != r && std::abs(dz) != r) continue;
                const int x = dx * 16 + 8, z = dz * 16 + 8;
                if (biome_at(SEED, x, z) == b) return BlockPos{x, 70, z};
            }
    return BlockPos{0, -1, 0};
}

// A player far from the cells tested near the origin.
const Vector3 FAR_PLAYER{0.5f, 70.f, 0.5f};

// A player 40 blocks from p, still inside p's loaded columns or not: can_spawn only measures.
Vector3 away(BlockPos p) {
    return Vector3{static_cast<float>(p.x) + 40.5f, 70.f, static_cast<float>(p.z) + 0.5f};
}

}  // namespace

TEST(test_spawn_hostile_needs_dark) {
    World w = flat_at(0, 0, 3, blk::STONE);
    const BlockPos f{30, 70, 0};
    light(w, f, 0, 4);
    CHECK(can_spawn(MobType::Gloomwalker, w, f, 1.f, FAR_PLAYER));
    light(w, f, 0, 5);
    CHECK(!can_spawn(MobType::Gloomwalker, w, f, 1.f, FAR_PLAYER));
    CHECK(!can_spawn(MobType::Gloomwalker, w, f, 0.15f, FAR_PLAYER));  // block light ignores daylight
    light(w, f, 15, 0);
    CHECK(can_spawn(MobType::Gloomwalker, w, f, 0.15f, FAR_PLAYER));  // 15 x 0.15 = 2.25
    CHECK(!can_spawn(MobType::Gloomwalker, w, f, 1.f, FAR_PLAYER));
    light(w, f, 4, 0);
    CHECK(can_spawn(MobType::Gloomwalker, w, f, 1.f, FAR_PLAYER));
    light(w, f, 5, 0);
    CHECK(!can_spawn(MobType::Gloomwalker, w, f, 1.f, FAR_PLAYER));
    // Passive creatures never spawn by darkness rules: STONE ground, no sky.
    light(w, f, 0, 0);
    CHECK(!can_spawn(MobType::Tusker, w, f, 0.15f, FAR_PLAYER));
}

TEST(test_spawn_not_near_player) {
    const World w = flat_at(0, 0, 3, blk::STONE);
    const BlockPos f{24, 70, 0};  // its center (24.5, 70, 0.5)
    CHECK(can_spawn(MobType::Gloomwalker, w, f, 0.15f, Vector3{0.5f, 70.f, 0.5f}));   // 24.0
    CHECK(!can_spawn(MobType::Gloomwalker, w, f, 0.15f, Vector3{0.6f, 70.f, 0.5f}));  // 23.9
    CHECK(!can_spawn(MobType::Gloomwalker, w, BlockPos{-24, 70, 0}, 0.15f, Vector3{-0.4f, 70.f, 0.5f}));
    CHECK(can_spawn(MobType::Gloomwalker, w, BlockPos{-24, 70, 0}, 0.15f, Vector3{0.5f, 70.f, 0.5f}));
    // Height counts: 24 straight above.
    CHECK(can_spawn(MobType::Gloomwalker, w, BlockPos{0, 70, 0}, 0.15f, Vector3{0.5f, 46.f, 0.5f}));
}

TEST(test_spawn_passive_biome) {
    const BlockPos forest = find_biome(Biome::Forest), desert = find_biome(Biome::Desert);
    CHECK(forest.y == 70 && desert.y == 70);
    {
        World w = flat_at(floor_div(forest.x, CHUNK), floor_div(forest.z, CHUNK), 1, blk::GRASS);
        light(w, forest, 15, 0);
        CHECK(can_spawn(MobType::Tusker, w, forest, 1.f, away(forest)));
        CHECK(can_spawn(MobType::Tusker, w, forest, 0.15f, away(forest)));  // raw sky light: night too
        CHECK(!can_spawn(MobType::Hopper, w, forest, 1.f, away(forest)));  // Hoppers: Meadow, Desert
        light(w, forest, 8, 0);
        CHECK(!can_spawn(MobType::Tusker, w, forest, 1.f, away(forest)));
        light(w, forest, 9, 0);
        CHECK(can_spawn(MobType::Tusker, w, forest, 1.f, away(forest)));
        light(w, forest, 0, 15);  // block light does not count
        CHECK(!can_spawn(MobType::Tusker, w, forest, 1.f, away(forest)));
    }
    {
        World w = flat_at(floor_div(forest.x, CHUNK), floor_div(forest.z, CHUNK), 1, blk::STONE);
        light(w, forest, 15, 0);
        CHECK(!can_spawn(MobType::Tusker, w, forest, 1.f, away(forest)));  // STONE is no pasture
    }
    {
        World w = flat_at(floor_div(desert.x, CHUNK), floor_div(desert.z, CHUNK), 1, blk::SAND);
        light(w, desert, 15, 0);
        CHECK(!can_spawn(MobType::Tusker, w, desert, 1.f, away(desert)));
        CHECK(can_spawn(MobType::Hopper, w, desert, 1.f, away(desert)));
    }
    const BlockPos snow = find_biome(Biome::Snowfield);
    CHECK(snow.y == 70);
    {
        World w = flat_at(floor_div(snow.x, CHUNK), floor_div(snow.z, CHUNK), 1, blk::SNOW);
        light(w, snow, 15, 0);
        CHECK(can_spawn(MobType::Fluffhorn, w, snow, 1.f, away(snow)));
        CHECK(!can_spawn(MobType::Tusker, w, snow, 1.f, away(snow)));
    }
}

TEST(test_spawn_cave_crawler_depth) {
    World w = flat_at(0, 0, 3, blk::STONE);
    // Pockets 2 high with floors under feet 49 and 50.
    put(w, 30, 49, 0, blk::AIR);
    put(w, 30, 50, 0, blk::AIR);
    put(w, 34, 50, 0, blk::AIR);
    put(w, 34, 51, 0, blk::AIR);
    CHECK(can_spawn(MobType::CaveCrawler, w, BlockPos{30, 49, 0}, 1.f, FAR_PLAYER));
    CHECK(!can_spawn(MobType::CaveCrawler, w, BlockPos{34, 50, 0}, 1.f, FAR_PLAYER));
    light(w, BlockPos{30, 49, 0}, 0, 5);
    CHECK(!can_spawn(MobType::CaveCrawler, w, BlockPos{30, 49, 0}, 1.f, FAR_PLAYER));  // too bright
    // Not on the dark surface either.
    CHECK(!can_spawn(MobType::CaveCrawler, w, BlockPos{30, 70, 0}, 0.15f, FAR_PLAYER));
}

TEST(test_spawn_thornspitter_forest_night) {
    const BlockPos forest = find_biome(Biome::Forest), meadow = find_biome(Biome::Meadow);
    CHECK(forest.y == 70 && meadow.y == 70);
    {
        World w = flat_at(floor_div(forest.x, CHUNK), floor_div(forest.z, CHUNK), 1, blk::GRASS);
        light(w, forest, 15, 0);
        CHECK(can_spawn(MobType::Thornspitter, w, forest, 0.15f, away(forest)));  // night
        CHECK(!can_spawn(MobType::Thornspitter, w, forest, 1.f, away(forest)));   // noon, open sky
        light(w, forest, 0, 0);
        CHECK(!can_spawn(MobType::Thornspitter, w, forest, 1.f, away(forest)));  // noon, in the shade
    }
    {
        World w = flat_at(floor_div(meadow.x, CHUNK), floor_div(meadow.z, CHUNK), 1, blk::GRASS);
        CHECK(!can_spawn(MobType::Thornspitter, w, meadow, 0.15f, away(meadow)));
        CHECK(can_spawn(MobType::Gloomwalker, w, meadow, 0.15f, away(meadow)));  // Gloomwalkers anywhere
    }
}

TEST(test_spawn_body_space) {
    World w = flat_at(0, 0, 3, blk::STONE);
    put(w, 30, 71, 0, blk::STONE);  // a 1-high gap over feet (30, 70, 0)
    CHECK(!can_spawn(MobType::Gloomwalker, w, BlockPos{30, 70, 0}, 0.15f, FAR_PLAYER));
    CHECK(can_spawn(MobType::Gloomwalker, w, BlockPos{31, 70, 0}, 0.15f, FAR_PLAYER));
    put(w, 32, 72, 0, blk::STONE);  // 2 high: room for a 1.9-high body
    CHECK(can_spawn(MobType::Gloomwalker, w, BlockPos{32, 70, 0}, 0.15f, FAR_PLAYER));
    // Feet in a block, in the air, in water, or in a plant cell.
    CHECK(!can_spawn(MobType::Gloomwalker, w, BlockPos{30, 69, 0}, 0.15f, FAR_PLAYER));
    CHECK(!can_spawn(MobType::Gloomwalker, w, BlockPos{30, 75, 0}, 0.15f, FAR_PLAYER));
    put(w, 33, 70, 0, blk::WATER);
    CHECK(!can_spawn(MobType::Gloomwalker, w, BlockPos{33, 70, 0}, 0.15f, FAR_PLAYER));
    put(w, 35, 70, 0, blk::TORCH);
    CHECK(!can_spawn(MobType::Gloomwalker, w, BlockPos{35, 70, 0}, 0.15f, FAR_PLAYER));
    // Outside the world's height.
    CHECK(!can_spawn(MobType::Gloomwalker, w, BlockPos{30, 256, 0}, 0.15f, FAR_PLAYER));
    CHECK(!can_spawn(MobType::Gloomwalker, w, BlockPos{30, -1, 0}, 0.15f, FAR_PLAYER));
}

TEST(test_spawn_unloaded) {
    const World w = flat_at(0, 0, 3, blk::STONE);
    CHECK(!can_spawn(MobType::Gloomwalker, w, BlockPos{60, 70, 0}, 0.15f, FAR_PLAYER));  // column 3
    CHECK(!can_spawn(MobType::Gloomwalker, w, BlockPos{-60, 70, 0}, 0.15f, FAR_PLAYER));
    // Spawning next to the loaded edge never puts a mob in an unloaded column.
    MobManager m;
    uint32_t rng = 99;
    for (int i = 0; i < 300; ++i) spawn_tick(m, w, Vector3{40.5f, 70.f, 0.5f}, 0.15f, rng, SpawnCaps{});
    for (const Mob& mob : m.all())
        CHECK(w.column_loaded(column_of(chunk_of(static_cast<int>(std::floor(mob.pos.x)), 0,
                                                 static_cast<int>(std::floor(mob.pos.z))))));
}

TEST(test_regress_spawn_bad_player_position) {
    // A NaN player position made the ring search loop forever.
    const World w = flat_at(0, 0, 3, blk::STONE);
    MobManager m;
    uint32_t rng = 5;
    spawn_tick(m, w, Vector3{std::nanf(""), 70.f, 0.5f}, 0.15f, rng, SpawnCaps{});
    spawn_tick(m, w, Vector3{0.5f, 70.f, std::numeric_limits<float>::infinity()}, 0.15f, rng, SpawnCaps{});
    CHECK(m.all().empty());
}

TEST(test_spawn_respects_caps) {
    const BlockPos meadow = find_biome(Biome::Meadow);
    World w = flat_at(floor_div(meadow.x, CHUNK), floor_div(meadow.z, CHUNK), 5, blk::GRASS);
    open_sky(w);  // passive: raw sky 15; hostile: 15 x 0.15 = 2 at night
    const Vector3 player{meadow.x + 0.5f, 70.f, meadow.z + 0.5f};
    auto count = [](const MobManager& m, bool hostile) {
        int n = 0;
        for (const Mob& mob : m.all()) n += mob_def(mob.type).hostile == hostile ? 1 : 0;
        return n;
    };
    {
        MobManager m;
        for (int i = 0; i < 25; ++i) m.spawn(MobType::Gloomwalker, Vector3{0.f, 300.f, 0.f}, 1u);
        uint32_t rng = 5;
        for (int i = 0; i < 300; ++i) spawn_tick(m, w, player, 0.15f, rng, SpawnCaps{});
        CHECK(count(m, true) == 25);
        CHECK(count(m, false) > 0 && count(m, false) <= 20);
    }
    {
        MobManager m;
        for (int i = 0; i < 20; ++i) m.spawn(MobType::Hopper, Vector3{0.f, 300.f, 0.f}, 1u);
        uint32_t rng = 5;
        for (int i = 0; i < 300; ++i) spawn_tick(m, w, player, 0.15f, rng, SpawnCaps{});
        CHECK(count(m, false) == 20);
        CHECK(count(m, true) > 0 && count(m, true) <= 25);
    }
    {
        // Custom caps, and zero caps spawn nothing.
        MobManager m;
        uint32_t rng = 5;
        for (int i = 0; i < 300; ++i) spawn_tick(m, w, player, 0.15f, rng, SpawnCaps{3, 4});
        CHECK(count(m, false) <= 3 && count(m, true) <= 4);
        CHECK(count(m, false) == 3 && count(m, true) == 4);
        MobManager none;
        for (int i = 0; i < 100; ++i) spawn_tick(none, w, player, 0.15f, rng, SpawnCaps{0, 0});
        CHECK(none.all().empty());
    }
}

TEST(test_spawn_ring) {
    const World w = flat_at(-2, -3, 5, blk::STONE);  // x in [-112, 48), z in [-128, 32)
    const Vector3 player{-31.5f, 70.f, -47.5f};
    MobManager m;
    uint32_t rng = 1234;
    int spawned = 0;
    for (int i = 0; i < 1000; ++i) {
        spawn_tick(m, w, player, 0.15f, rng, SpawnCaps{});
        for (const Mob& mob : m.all()) {
            const float dx = mob.pos.x - player.x, dz = mob.pos.z - player.z, dy = mob.pos.y - player.y;
            const float h = std::sqrt(dx * dx + dz * dz);
            CHECK(h <= 64.f + 1e-3f);
            CHECK(std::sqrt(h * h + dy * dy) >= 24.f - 1e-3f);
            CHECK(mob.pos.y == 70.f);
            CHECK(mob_def(mob.type).hostile);  // bare stone in the dark: only hostiles
            ++spawned;
        }
        while (!m.all().empty()) m.remove(m.all().front().id);
    }
    CHECK(spawned > 100);
}

TEST(test_spawn_deterministic) {
    const World w = flat_at(0, 0, 5, blk::STONE);
    MobManager a, b;
    uint32_t ra = 77, rb = 77;
    for (int i = 0; i < 50; ++i) {
        spawn_tick(a, w, Vector3{0.5f, 70.f, 0.5f}, 0.15f, ra, SpawnCaps{});
        spawn_tick(b, w, Vector3{0.5f, 70.f, 0.5f}, 0.15f, rb, SpawnCaps{});
    }
    CHECK(ra == rb);
    CHECK(a.all().size() == b.all().size() && !a.all().empty());
    for (std::size_t i = 0; i < a.all().size() && i < b.all().size(); ++i) {
        CHECK(a.all()[i].type == b.all()[i].type);
        CHECK(a.all()[i].pos.x == b.all()[i].pos.x && a.all()[i].pos.z == b.all()[i].pos.z);
        CHECK(a.all()[i].rng == b.all()[i].rng);
    }
}

TEST(test_despawn_far_hostiles_only) {
    MobManager m;
    const Vector3 player{0.5f, 70.f, 0.5f};
    const uint32_t gone = m.spawn(MobType::Gloomwalker, Vector3{96.6f, 70.f, 0.5f}, 1u);  // 96.1
    const uint32_t kept = m.spawn(MobType::CaveCrawler, Vector3{95.5f, 70.f, 0.5f}, 1u);  // 95
    const uint32_t passive = m.spawn(MobType::Tusker, Vector3{1000.5f, 70.f, 0.5f}, 1u);
    despawn_tick(m, player, 0.f);
    CHECK(m.find(gone) == nullptr);
    CHECK(m.find(kept) != nullptr);
    CHECK(m.find(passive) != nullptr);
    m.remove(kept);

    const uint32_t lurker = m.spawn(MobType::Thornspitter, Vector3{50.5f, 70.f, 0.5f}, 1u);  // 50
    despawn_tick(m, player, 29.9f);
    CHECK(m.find(lurker) != nullptr);
    despawn_tick(m, player, 0.1f);
    CHECK(m.find(lurker) == nullptr);

    // Coming within 48 resets the timer.
    const uint32_t back = m.spawn(MobType::Gloomwalker, Vector3{50.5f, 70.f, 0.5f}, 1u);
    despawn_tick(m, player, 20.f);
    m.find(back)->pos.x = 48.5f;  // exactly 48: near enough
    despawn_tick(m, player, 1.f);
    m.find(back)->pos.x = 50.5f;
    despawn_tick(m, player, 20.f);
    CHECK(m.find(back) != nullptr);
    despawn_tick(m, player, 10.f);
    CHECK(m.find(back) == nullptr);

    // Passive creatures are kept however far and long.
    for (int i = 0; i < 10; ++i) despawn_tick(m, player, 60.f);
    CHECK(m.find(passive) != nullptr);
    // NaN or negative dt adds no time.
    const uint32_t odd = m.spawn(MobType::Gloomwalker, Vector3{60.5f, 70.f, 0.5f}, 1u);
    despawn_tick(m, player, std::nanf(""));
    despawn_tick(m, player, -50.f);
    despawn_tick(m, player, 29.f);
    CHECK(m.find(odd) != nullptr);
}
