// Deterministic terrain from a seed: lattice hash, value noise, fBm, climate and biomes, heights,
// caves, ravines, ores, geodes, biome features (trees, cacti, plants), and columns.
// Generation math uses only integer ops, + - * /, std::sqrt, and std::floor (plan Global
// Constraints; std::abs and std::min/max/clamp are exact), so a
// seed builds the same world on GCC, MinGW, and MSVC; TERRAIN_GOLDEN in tests/test_terrain.cpp
// fingerprints it. Every function here is pure and thread-safe.
#pragma once

#include "chunk.hpp"
#include "coords.hpp"

#include <array>
#include <cstdint>
#include <vector>

// Lattice hash: x*374761393 + z*668265263 + seed*2246822519 (mod 2^32), then xor-shift mixing.
uint32_t hash2(uint32_t seed, int x, int z);

// Value noise in [0,1]: lattice values hash2(seed, i, j) mapped to [0,1], smoothstep-interpolated.
// x and z are finite with |x|, |z| < 2^30.
float value_noise2(uint32_t seed, float x, float z);

// Fractal sum of `octaves` (>= 1) value-noise layers, normalized to [0,1]: octave i adds
// 0.5^i * value_noise2(seed + i * 0x9E3779B9, x * 2^i, z * 2^i), and the total is divided by the
// sum of the weights 0.5^i (gain 0.5, lacunarity 2). The highest octave's x * 2^(octaves-1) and
// z * 2^(octaves-1) must meet value_noise2's range.
float fbm2(uint32_t seed, float x, float z, int octaves);

// Value noise in [0,1] on a 3D lattice: lattice values hash2(seed + y * 0x27D4EB2F, x, z) mapped
// to [0,1], smoothstep-interpolated along x, z, then y. Same coordinate range as value_noise2.
float value_noise3(uint32_t seed, float x, float y, float z);

// fbm2's octave rule (gain 0.5, lacunarity 2, seed + i * 0x9E3779B9) over value_noise3.
float fbm3(uint32_t seed, float x, float y, float z, int octaves);

// Spaghetti caves: 5 <= y <= surface_h - 4, and both
// |fbm3(seed ^ 0xCA7E, x/48, y/32, z/48, 3) - 0.5| and |fbm3(seed ^ 0x5A17, ...) - 0.5| < 0.035.
bool is_cave(uint32_t seed, int x, int y, int z, int surface_h);

// A straight ravine from (x0, z0). Its direction is the point at fraction `angle` (in [0,1)) of
// the way around the square (1,-1) -> (1,1) -> (-1,1) -> (-1,-1) -> (1,-1), normalized with
// std::sqrt, so no trigonometry is needed. length 48..111 blocks, depth 20..30 blocks below the local
// surface, width 4 = the widest point, ravine_width(0.5).
struct Ravine {
    int x0, z0;
    float angle;
    int length, depth, width;
};

// Ravine candidates: one per 64x64-block cell, present when hash2(seed ^ 0xEA71, cell) % 24 == 0,
// for the 5x5 cells around the cell holding the column (a ravine never reaches past 2 cells).
// Sorted by cell, z-major.
std::vector<Ravine> ravines_near(uint32_t seed, ColumnPos c);

// Width across the ravine at t in [0,1] along it: 2 + 8 * t * (1 - t).
float ravine_width(float t);

// Generation stages that can be switched off (tests compare a column with and without one).
struct GenOptions {
    bool caves = true;
    bool ravines = true;
    bool ores = true;
    bool geodes = true;
    bool features = true;
};

// Ore veins (spec depth table): a vein is a random walk of vein_size steps from a hashed origin,
// each step writing a 2x2x2 blob. The walk never leaves y_min .. y_max - 1, so blobs stay in
// [y_min, y_max].
struct OreRule {
    BlockId ore;
    int y_min, y_max;
    int veins_per_column;
    int vein_size;
};
// COAL 5-140 x20 size 8; COPPER 20-100 x10 size 6; TIN 10-70 x7 size 5; IRON 5-60 x8 size 6;
// SILVER 5-30 x2 size 4.
extern const std::array<OreRule, 5> ORE_RULES;

// Writes the ore veins of column c and its 8 neighbors into col (the blocks of column c),
// replacing STONE only. A vein's origin lies in its own column at a hashed (x, z) and a hashed y in
// [y_min, min(y_max - 1, terrain_height(origin) - 4)] (the stone below the surface layers); a rule
// whose range lies above that is skipped for the vein. Rules apply in table order. A neighbor
// column past the supported range |pos| <= 2^27 - 2 contributes no veins.
// Independent of the order in which columns are generated.
void place_ores(Column& col, ColumnPos c, uint32_t seed);

// A quartz geode centre: one candidate per 4x4-column (64x64-block) cell, present when
// hash2(seed ^ 0x6E0D, cell) % 40 == 0, at a hashed (x, z) in the cell and y in 5..40.
struct Geode {
    int x, y, z;
};
// Geodes of the 3x3 cells around the cell holding column c (a radius-4 sphere never reaches
// further), sorted by cell, z-major.
std::vector<Geode> geodes_near(uint32_t seed, ColumnPos c);

// Writes the geodes that reach column c into col, replacing STONE only: cells with
// d^2 <= 9 from the centre become AIR, cells with 9 < d^2 <= 16 QUARTZ (a 1-thick shell).
void place_geodes(Column& col, ColumnPos c, uint32_t seed);

enum class Biome : uint8_t { Meadow, Forest, Taiga, Desert, Snowfield, Highlands, Ocean, COUNT };

// Each field in [0,1]: fbm2 with 3 octaves of (x, z) divided by 512 (temperature, humidity),
// 768 (continental), and 384 (mountain), each with its own seed offset.
struct Climate {
    float temperature, humidity, continental, mountain;
};
Climate climate_at(uint32_t seed, int x, int z);

// continental < 0.35 -> Ocean; mountain > 0.7 -> Highlands; temperature < 0.3 -> Snowfield;
// temperature < 0.45 -> Taiga; temperature > 0.7 and humidity < 0.35 -> Desert;
// humidity > 0.5 -> Forest; else Meadow.
Biome biome_from_climate(const Climate& c);

// biome_from_climate(climate_at(seed, x, z)), computing only the fields the table needs.
Biome biome_at(uint32_t seed, int x, int z);

// Biome-blended ground height in [20, 200]. Each biome has a base and an amplitude (Ocean 34+20,
// Meadow 66+12, Forest 68+16, Taiga 68+20, Desert 66+10, Snowfield 68+14, Highlands 90+110).
// Blend points sit every 4 blocks; each averages base and amplitude over the 5x5 biome samples
// spaced 4 blocks apart around it, and (x, z) interpolates bilinearly between its 4 surrounding
// blend points, so the height changes by at most about 8 per block even across biome borders.
// land = base + amp * fbm2(seed, x/96, z/96, 5). A coastal cap keeps Ocean below sea level:
// h = int(min(land, max(54, SEA_LEVEL - 1 + (continental - 0.35) * 1500))), clamped to [20, 200];
// the cap only binds near coasts (it passes 200 at continental 0.44).
int terrain_height(uint32_t seed, int x, int z);

enum class FeatureType : uint8_t { OakTree, PineTree, Cactus, TallGrass, Flower, Glowcap };

// A feature standing on the ground block below (x, y, z); y = ground height + 1 (a glowcap: its
// cave floor + 1). size: oak trunk 5, pine trunk 6-8, cactus height 1-3, plants and glowcaps 1.
struct Feature {
    int x, y, z;
    FeatureType type;
    uint8_t size;
};

// The features on (x, z) cells up to `margin` blocks outside column c (margin clamped to
// [0, 16]), at most one surface feature and one glowcap per cell, sorted by (x, z, type).
// Glowcap: the lowest y in [5, 49] with hash2((seed ^ 0x61C0) + y * 0x27D4EB2F, x, z) % 90 == 0,
// is_cave at y, and no is_cave at y - 1 (a cave floor), on any ground height. A surface feature
// or glowcap on a cell a ravine covers is dropped. A surface cell qualifies only when its top
// block, after carving, is the generated surface at ground h >= SEA_LEVEL (a ravine that cuts the
// surface leaves no feature). By the cell's biome, with r(salt) = hash2(seed ^ salt, x, z):
// Forest oak r(0x7EE5) % 12 == 0 on GRASS; Meadow oak r(0x7EE5) % 200, else flower r(0xF10E) % 24,
// else tall grass r(0x6A55) % 4, on GRASS; Taiga pine r(0x714E) % 14 and Snowfield pine
// r(0x714E) % 60 on GRASS or SNOW; Desert cactus r(0xCAC7) % 150 on SAND; Highlands oak
// r(0x7EE5) % 80 on GRASS with h < 140; Ocean nothing. Pine trunk 6 + (r(0x714E) >> 8) % 3;
// cactus height 1 + (r(0xCAC7) >> 8) % 3. Requires |c.x|, |c.z| <= 2^27 - 2 (as generate_column);
// neighbor cells in columns past that range have none.
std::vector<Feature> features_near(uint32_t seed, ColumnPos c, int margin = 3);

// Writes the cells of f that lie inside column c (and inside y 0..255) into col. Logs overwrite;
// leaves, cacti, and plants go only into AIR. Oak: LOG y..y+size-1, LEAVES at the top 2 trunk
// layers (5x5 without corners) and the 2 layers above the trunk (3x3). Glowcap: GLOWCAP at y only
// into AIR whose block below is STONE (a floor that ores and geodes left alone). Pine: PINE_LOG
// y..y+size-1, then a cone of PINE_LEAVES over the 5 layers y+size-4 .. y+size with radius
// 2, 2, 1, 1, 0 (cells with dx^2 + dz^2 <= r^2 + r). Cactus: CACTUS y..y+size-1. TallGrass and
// Flower: one block at y.
void place_feature(Column& col, ColumnPos c, const Feature& f);

// All 16 chunks of column pos, each non-null with pos set and modified == false.
// Layers for ground height h and the cell's biome: y = 0 DEEPSTONE, STONE up to the top 4 blocks
// h-3..h, which are: Ocean one of SAND/GRAVEL/CLAY by hash2(seed ^ 0x0CEA, x, z) % 3; Desert
// SAND, with 4 SANDSTONE below them; any other biome with h <= SEA_LEVEL + 1 SAND (beach);
// Snowfield SNOW over 3 DIRT; Highlands with h > 140 STONE; else GRASS over 3 DIRT. Every cell
// from h + 1 to SEA_LEVEL is WATER, except ICE at SEA_LEVEL in Snowfield.
// Then caves (is_cave cells) and then ravines (ravines_near) are carved to AIR, except DEEPSTONE,
// WATER, and any cell with WATER within 3 blocks above it. Then place_geodes and place_ores
// replace remaining STONE. Last, place_feature applies features_near(seed, pos, 3) in order.
// Independent of the order in which columns are generated. |pos.x| and |pos.z| are at most
// 2^27 - 2, so every block x and z the features and ravines reach fits in an int.
Column generate_column(uint32_t seed, ColumnPos pos, GenOptions opt = {});
