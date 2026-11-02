// Deterministic terrain from a seed: lattice hash, value noise, fBm, heights, trees, and columns.
// Generation math uses only integer ops, + - * /, and std::floor (plan Global Constraints), so a
// seed builds the same world on GCC, MinGW, and MSVC; TERRAIN_GOLDEN in tests/test_terrain.cpp
// fingerprints it. Every function here is pure and thread-safe.
#pragma once

#include "chunk.hpp"
#include "coords.hpp"

#include <cstdint>

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

// 48 + int(fbm2(seed, x/96.f, z/96.f, 5) * 56), clamped to [48, 103].
int terrain_height(uint32_t seed, int x, int z);

// A tree stands on (x, z): hash2(seed ^ 0x7EE5, x, z) % 64 == 0 and the ground is grass
// (terrain_height > SEA_LEVEL + 1).
bool tree_at(uint32_t seed, int x, int z);

// All 16 chunks of column pos, each non-null with pos set and modified == false.
// Layers for ground height h: y = 0 DEEPSTONE, 1..h-4 STONE, h-3..h-1 DIRT, h GRASS; when
// h <= SEA_LEVEL + 1 the top 4 are SAND instead. Trees whose origin lies within 2 blocks outside
// the column are included: LOG at h+1..h+5, then LEAVES only into AIR at h+4..h+5 (5x5 without
// the corners) and h+6..h+7 (3x3). Independent of the order in which columns are generated.
// |pos.x| and |pos.z| are at most 2^27 - 2, so every block x and z the trees reach fits in an int.
Column generate_column(uint32_t seed, ColumnPos pos);
