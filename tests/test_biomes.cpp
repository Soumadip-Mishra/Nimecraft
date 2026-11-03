// Tests for the climate, biome, and terrain-height part of src/terrain.* (M2 Task 2).
#include "block.hpp"
#include "chunk.hpp"
#include "coords.hpp"
#include "terrain.hpp"
#include "test.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>

namespace {

constexpr uint32_t SEED = 7;

// Deterministic positions for the 100,000-sample tests (64-bit LCG, high bits).
struct Lcg {
    uint64_t s;
    int next(int lo, int hi) {  // [lo, hi]
        s = s * 6364136223846793005ULL + 1442695040888963407ULL;
        const uint64_t span = static_cast<uint64_t>(static_cast<int64_t>(hi) - lo + 1);
        return static_cast<int>(static_cast<int64_t>(lo) + static_cast<int64_t>((s >> 33) % span));
    }
};

BlockId block_at(const Column& col, ColumnPos c, int x, int y, int z) {
    return col[static_cast<std::size_t>(y / CHUNK)]->get(x - c.x * CHUNK, y % CHUNK, z - c.z * CHUNK);
}

ColumnPos column_at(int x, int z) {
    return ColumnPos{floor_div(x, CHUNK), floor_div(z, CHUNK)};
}

struct Pos {
    int x, z;
    bool found;
};

// First grid point (step 32 over [-8192, 8192)^2, z-major) with biome b and, when min_h > 0, a
// height of at least min_h.
Pos find_biome(Biome b, int min_h) {
    for (int z = -8192; z < 8192; z += 32)
        for (int x = -8192; x < 8192; x += 32)
            if (biome_at(SEED, x, z) == b && (min_h <= 0 || terrain_height(SEED, x, z) >= min_h))
                return Pos{x, z, true};
    return Pos{0, 0, false};
}

Climate base_climate() {
    return Climate{0.5f, 0.4f, 0.5f, 0.5f};  // -> Meadow
}

}  // namespace

TEST(test_biomes_all_present) {
    std::array<bool, static_cast<std::size_t>(Biome::COUNT)> seen{};
    int kinds = 0;
    for (int z = -8192; z < 8192 && kinds < 7; z += 16)
        for (int x = -8192; x < 8192 && kinds < 7; x += 16) {
            const auto b = static_cast<std::size_t>(biome_at(SEED, x, z));
            CHECK(b < seen.size());
            if (b < seen.size() && !seen[b]) {
                seen[b] = true;
                ++kinds;
            }
        }
    CHECK(kinds == 7);
}

TEST(test_height_continuity) {
    Lcg rng{12345};
    int worst = 0;
    for (int i = 0; i < 100000; ++i) {
        const int x = rng.next(-200000, 200000), z = rng.next(-200000, 200000);
        const int h = terrain_height(SEED, x, z);
        worst = std::max(worst, std::abs(h - terrain_height(SEED, x + 1, z)));
        if (i % 4 == 0) worst = std::max(worst, std::abs(h - terrain_height(SEED, x, z + 1)));
    }
    CHECK(worst <= 8);
}

TEST(test_ocean_below_sea) {
    int oceans = 0;
    bool below = true;
    for (int z = -4096; z < 4096; z += 16)
        for (int x = -4096; x < 4096; x += 16) {
            if (biome_at(SEED, x, z) != Biome::Ocean) continue;
            ++oceans;
            below = below && terrain_height(SEED, x, z) < SEA_LEVEL;
        }
    CHECK(oceans > 0);
    CHECK(below);
}

TEST(test_column_water_fill) {
    const Pos p = find_biome(Biome::Ocean, 0);
    CHECK(p.found);
    const ColumnPos c = column_at(p.x, p.z);
    const Column col = generate_column(SEED, c);
    const int h = terrain_height(SEED, p.x, p.z);
    CHECK(h < SEA_LEVEL);
    for (int y = h + 1; y <= SEA_LEVEL; ++y) CHECK(block_at(col, c, p.x, y, p.z) == blk::WATER);
    bool air = true;
    for (int y = SEA_LEVEL + 1; y < WORLD_HEIGHT; ++y) air = air && block_at(col, c, p.x, y, p.z) == blk::AIR;
    CHECK(air);
}

TEST(test_biome_surface_blocks) {
    const Pos d = find_biome(Biome::Desert, 0);
    CHECK(d.found);
    const ColumnPos dc = column_at(d.x, d.z);
    const Column desert = generate_column(SEED, dc);
    const int dh = terrain_height(SEED, d.x, d.z);
    CHECK(block_at(desert, dc, d.x, dh, d.z) == blk::SAND);
    CHECK(block_at(desert, dc, d.x, dh - 5, d.z) == blk::SANDSTONE);

    const Pos s = find_biome(Biome::Snowfield, SEA_LEVEL + 2);  // above the beach rule
    CHECK(s.found);
    const ColumnPos sc = column_at(s.x, s.z);
    const Column snow = generate_column(SEED, sc);
    CHECK(block_at(snow, sc, s.x, terrain_height(SEED, s.x, s.z), s.z) == blk::SNOW);
}

TEST(test_biome_thresholds) {
    CHECK(biome_from_climate(base_climate()) == Biome::Meadow);
    auto with = [](float t, float h, float c, float m) { return biome_from_climate(Climate{t, h, c, m}); };
    CHECK(with(0.5f, 0.4f, 0.349f, 0.9f) == Biome::Ocean);
    CHECK(with(0.5f, 0.4f, 0.35f, 0.5f) != Biome::Ocean);
    CHECK(with(0.5f, 0.4f, 0.5f, 0.7f) == Biome::Meadow);
    CHECK(with(0.5f, 0.4f, 0.5f, 0.701f) == Biome::Highlands);
    CHECK(with(0.299f, 0.4f, 0.5f, 0.5f) == Biome::Snowfield);
    CHECK(with(0.3f, 0.4f, 0.5f, 0.5f) == Biome::Taiga);
    CHECK(with(0.45f, 0.4f, 0.5f, 0.5f) == Biome::Meadow);
    CHECK(with(0.701f, 0.349f, 0.5f, 0.5f) == Biome::Desert);
    CHECK(with(0.701f, 0.35f, 0.5f, 0.5f) == Biome::Meadow);
    CHECK(with(0.7f, 0.2f, 0.5f, 0.5f) == Biome::Meadow);
    CHECK(with(0.5f, 0.501f, 0.5f, 0.5f) == Biome::Forest);
    CHECK(with(0.5f, 0.5f, 0.5f, 0.5f) == Biome::Meadow);
}

TEST(test_biome_enum_values) {
    CHECK(static_cast<int>(Biome::Meadow) == 0);
    CHECK(static_cast<int>(Biome::Forest) == 1);
    CHECK(static_cast<int>(Biome::Taiga) == 2);
    CHECK(static_cast<int>(Biome::Desert) == 3);
    CHECK(static_cast<int>(Biome::Snowfield) == 4);
    CHECK(static_cast<int>(Biome::Highlands) == 5);
    CHECK(static_cast<int>(Biome::Ocean) == 6);
    CHECK(static_cast<int>(Biome::COUNT) == 7);
}

TEST(test_climate_range) {
    Lcg rng{777};
    bool in_range = true;
    auto check = [&in_range](int x, int z) {
        const Climate c = climate_at(SEED, x, z);
        for (float v : {c.temperature, c.humidity, c.continental, c.mountain})
            in_range = in_range && v >= 0.f && v <= 1.f;
    };
    for (int i = 0; i < 100000; ++i) check(rng.next(-1000000, 1000000), rng.next(-1000000, 1000000));
    for (int x : {-1000000, 1000000})
        for (int z : {-1000000, 1000000}) check(x, z);
    CHECK(in_range);
}

TEST(test_height_clamped) {
    Lcg rng{4242};
    bool in_range = true;
    for (int i = 0; i < 100000; ++i) {
        const int h = terrain_height(SEED, rng.next(-1000000, 1000000), rng.next(-1000000, 1000000));
        in_range = in_range && h >= 20 && h <= 200;
    }
    CHECK(in_range);
}

// ---- Coverage Checklist additions ----

TEST(test_biome_deterministic) {
    // biome_at is biome_from_climate(climate_at(...)); the same seed repeats exactly, and two
    // seeds give different climates somewhere.
    bool same = true, consistent = true, seeds_differ = false;
    for (int z = -2048; z < 2048; z += 64)
        for (int x = -2048; x < 2048; x += 64) {
            const Climate a = climate_at(SEED, x, z), b = climate_at(SEED, x, z);
            same = same && a.temperature == b.temperature && a.humidity == b.humidity &&
                   a.continental == b.continental && a.mountain == b.mountain;
            consistent = consistent && biome_at(SEED, x, z) == biome_from_climate(a);
            seeds_differ = seeds_differ || climate_at(8, x, z).temperature != a.temperature;
        }
    CHECK(same);
    CHECK(consistent);
    CHECK(seeds_differ);
}

TEST(test_biome_climate_fields_distinct) {
    // Each field uses its own seed offset: no two fields are equal everywhere.
    int equal[6] = {0, 0, 0, 0, 0, 0}, n = 0;
    for (int z = -2048; z < 2048; z += 128)
        for (int x = -2048; x < 2048; x += 128) {
            const Climate c = climate_at(SEED, x, z);
            const float f[4] = {c.temperature, c.humidity, c.continental, c.mountain};
            int k = 0;
            for (int i = 0; i < 4; ++i)
                for (int j = i + 1; j < 4; ++j) equal[k++] += f[i] == f[j] ? 1 : 0;
            ++n;
        }
    for (int k : equal) CHECK(k < n);
}

TEST(test_biome_height_uniform_formula) {
    // Where every block within 12 of (x, z) has the same biome, the blend is that biome's own
    // base and amplitude: h = base + int(amp * fbm2(seed, x/96, z/96, 5)), clamped to [20, 200].
    // Land positions also need continental >= 0.45 so the coastal cap (SEA_LEVEL - 1 +
    // (continental - 0.35) * 1500 >= 213) is above every land height.
    struct Case {
        Biome b;
        float base, amp;
        int found;
    };
    Case cases[3] = {{Biome::Meadow, 66.f, 12.f, 0}, {Biome::Ocean, 34.f, 20.f, 0}, {Biome::Highlands, 90.f, 110.f, 0}};
    for (Case& k : cases) {
        for (int z = -16384; z < 16384 && k.found < 5; z += 331)
            for (int x = -16384; x < 16384 && k.found < 5; x += 331) {
                if (biome_at(SEED, x, z) != k.b) continue;
                if (k.b != Biome::Ocean && climate_at(SEED, x, z).continental < 0.45f) continue;
                bool uniform = true;
                for (int dz = -12; dz <= 12 && uniform; ++dz)
                    for (int dx = -12; dx <= 12 && uniform; ++dx)
                        uniform = biome_at(SEED, x + dx, z + dz) == k.b;
                if (!uniform) continue;
                ++k.found;
                const float n = fbm2(SEED, static_cast<float>(x) / 96.f, static_cast<float>(z) / 96.f, 5);
                CHECK(terrain_height(SEED, x, z) == std::clamp(static_cast<int>(k.base + k.amp * n), 20, 200));
            }
        CHECK(k.found == 5);
    }
}

TEST(test_biome_snowfield_ice) {
    // A Snowfield cell under sea level freezes at y = SEA_LEVEL and keeps WATER below.
    bool found = false;
    for (int z = -8192; z < 8192 && !found; z += 8)
        for (int x = -8192; x < 8192 && !found; x += 8) {
            if (biome_at(SEED, x, z) != Biome::Snowfield) continue;
            const int h = terrain_height(SEED, x, z);
            if (h >= SEA_LEVEL - 1) continue;
            found = true;
            const ColumnPos c = column_at(x, z);
            const Column col = generate_column(SEED, c);
            CHECK(block_at(col, c, x, SEA_LEVEL, z) == blk::ICE);
            for (int y = h + 1; y < SEA_LEVEL; ++y) CHECK(block_at(col, c, x, y, z) == blk::WATER);
            CHECK(block_at(col, c, x, SEA_LEVEL + 1, z) == blk::AIR);
        }
    CHECK(found);
}

TEST(test_biome_highlands_stone_line) {
    // Highlands: STONE at the surface above 140, GRASS at or below it (above the beach rule).
    bool high = false, low = false;
    for (int z = -8192; z < 8192 && !(high && low); z += 16)
        for (int x = -8192; x < 8192 && !(high && low); x += 16) {
            if (biome_at(SEED, x, z) != Biome::Highlands) continue;
            const int h = terrain_height(SEED, x, z);
            const bool want_high = h > 140 && !high, want_low = h <= 140 && h > SEA_LEVEL + 1 && !low;
            if (!want_high && !want_low) continue;
            const ColumnPos c = column_at(x, z);
            const Column col = generate_column(SEED, c);
            if (want_high) {
                high = true;
                CHECK(block_at(col, c, x, h, z) == blk::STONE);
                CHECK(block_at(col, c, x, h - 2, z) == blk::STONE);
            } else {
                low = true;
                CHECK(block_at(col, c, x, h, z) == blk::GRASS);
                CHECK(block_at(col, c, x, h - 2, z) == blk::DIRT);
            }
        }
    CHECK(high);
    CHECK(low);
}

TEST(test_biome_ocean_floor_materials) {
    // Ocean floors use all three of SAND, GRAVEL, and CLAY, four deep, over STONE.
    bool seen[3] = {false, false, false};
    for (int z = -4096; z < 4096 && !(seen[0] && seen[1] && seen[2]); z += 64)
        for (int x = -4096; x < 4096 && !(seen[0] && seen[1] && seen[2]); x += 64) {
            if (biome_at(SEED, x, z) != Biome::Ocean) continue;
            const ColumnPos c = column_at(x, z);
            const Column col = generate_column(SEED, c);
            const int h = terrain_height(SEED, x, z);
            const BlockId top = block_at(col, c, x, h, z);
            CHECK(top == blk::SAND || top == blk::GRAVEL || top == blk::CLAY);
            for (int y = h - 3; y < h; ++y) CHECK(block_at(col, c, x, y, z) == top);
            CHECK(block_at(col, c, x, h - 4, z) == blk::STONE);
            seen[0] = seen[0] || top == blk::SAND;
            seen[1] = seen[1] || top == blk::GRAVEL;
            seen[2] = seen[2] || top == blk::CLAY;
        }
    CHECK(seen[0] && seen[1] && seen[2]);
}

TEST(test_biome_surface_exact_boundaries) {
    // Highlands at exactly 140 keep GRASS, at 141 turn to STONE. Snowfield at 63 gets ICE at
    // SEA_LEVEL straight on the ground (no WATER), at 64 no ICE at all.
    bool h140 = false, h141 = false, s63 = false, s64 = false;
    for (int z = -16384; z < 16384 && !(h140 && h141 && s63 && s64); z += 4)
        for (int x = -16384; x < 16384 && !(h140 && h141 && s63 && s64); x += 4) {
            const Biome b = biome_at(SEED, x, z);
            if (b != Biome::Highlands && b != Biome::Snowfield) continue;
            const int h = terrain_height(SEED, x, z);
            const bool want = (b == Biome::Highlands && ((h == 140 && !h140) || (h == 141 && !h141))) ||
                              (b == Biome::Snowfield && ((h == 63 && !s63) || (h == 64 && !s64)));
            if (!want) continue;
            const ColumnPos c = column_at(x, z);
            const Column col = generate_column(SEED, c);
            if (h == 140) {
                h140 = true;
                CHECK(block_at(col, c, x, 140, z) == blk::GRASS);
            } else if (h == 141) {
                h141 = true;
                CHECK(block_at(col, c, x, 141, z) == blk::STONE);
            } else if (h == 63) {
                s63 = true;
                CHECK(block_at(col, c, x, 63, z) == blk::SAND);  // beach: h <= SEA_LEVEL + 1
                CHECK(block_at(col, c, x, 64, z) == blk::ICE);
            } else {
                s64 = true;
                CHECK(block_at(col, c, x, 64, z) == blk::SAND);
                CHECK(block_at(col, c, x, 65, z) != blk::ICE);
            }
        }
    CHECK(h140);
    CHECK(h141);
    CHECK(s63);
    CHECK(s64);
}

TEST(test_biome_coast_cap_narrow) {
    // The coastal cap binds only near coasts: tall uniform Highlands at continental 0.45-0.5 keep
    // their pure height (a cap below 63 + 0.15 * 1500 = 288 would cut them).
    int found = 0;
    for (int z = -65536; z < 65536 && found < 3; z += 61)
        for (int x = -65536; x < 65536 && found < 3; x += 61) {
            if (biome_at(SEED, x, z) != Biome::Highlands) continue;
            const float c = climate_at(SEED, x, z).continental;
            if (c < 0.45f || c > 0.5f) continue;
            const float n = fbm2(SEED, static_cast<float>(x) / 96.f, static_cast<float>(z) / 96.f, 5);
            const int pure = std::clamp(static_cast<int>(90.f + 110.f * n), 20, 200);
            if (pure <= 160) continue;
            bool uniform = true;
            for (int dz = -12; dz <= 12 && uniform; ++dz)
                for (int dx = -12; dx <= 12 && uniform; ++dx)
                    uniform = biome_at(SEED, x + dx, z + dz) == Biome::Highlands;
            if (!uniform) continue;
            ++found;
            CHECK(terrain_height(SEED, x, z) == pure);
        }
    CHECK(found == 3);
}
