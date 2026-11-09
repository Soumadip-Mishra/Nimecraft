// Block ids, face and tile enums, render layers and shapes, and the BlockDef table.
// Block ids are persisted in saves: append only, never renumber or reuse.
#pragma once

#include "items.hpp"

#include <array>
#include <cstdint>
#include <string_view>

using BlockId = uint16_t;

namespace blk {
constexpr BlockId AIR = 0, GRASS = 1, DIRT = 2, STONE = 3, SAND = 4, LOG = 5, LEAVES = 6, PLANKS = 7,
                  DEEPSTONE = 8, WATER = 9, GRAVEL = 10, CLAY = 11, SNOW = 12, ICE = 13,
                  SANDSTONE = 14, CACTUS = 15, PINE_LOG = 16, PINE_LEAVES = 17, TALL_GRASS = 18,
                  FLOWER = 19, COAL_ORE = 20, COPPER_ORE = 21, TIN_ORE = 22, IRON_ORE = 23,
                  SILVER_ORE = 24, QUARTZ = 25, TORCH = 26, LANTERN = 27, GLOWCAP = 28,
                  WORKBENCH = 29, KILN = 30, CRATE = 31, GLASS = 32, COUNT = 33;
}

enum Face : uint8_t { PX, NX, PY, NY, PZ, NZ };

// Atlas cells, row-major in the 8x8 atlas. Not persisted; appended for readability only.
enum Tile : uint8_t {
    T_GRASS_TOP,
    T_GRASS_SIDE,
    T_DIRT,
    T_STONE,
    T_SAND,
    T_LOG_SIDE,
    T_LOG_TOP,
    T_LEAVES,
    T_PLANKS,
    T_DEEPSTONE,
    T_WATER,
    T_GRAVEL,
    T_CLAY,
    T_SNOW,
    T_SNOW_SIDE,
    T_ICE,
    T_SANDSTONE_TOP,
    T_SANDSTONE_SIDE,
    T_CACTUS_TOP,
    T_CACTUS_SIDE,
    T_PINE_LOG_SIDE,
    T_PINE_LOG_TOP,
    T_PINE_LEAVES,
    T_TALL_GRASS,
    T_FLOWER,
    T_COAL_ORE,
    T_COPPER_ORE,
    T_TIN_ORE,
    T_IRON_ORE,
    T_SILVER_ORE,
    T_QUARTZ,
    T_TORCH,
    T_LANTERN,
    T_GLOWCAP,
    T_WORKBENCH_TOP,
    T_WORKBENCH_SIDE,
    T_KILN_TOP,
    T_KILN_SIDE,
    T_CRATE_TOP,
    T_CRATE_SIDE,
    T_GLASS,
    T_CRACK_0,  // mining overlay stages, drawn over the targeted block (never on a block face)
    T_CRACK_1,
    T_CRACK_2,
    T_CRACK_3,
    T_COUNT
};

// Which mesh a block's faces go into. Opaque: fully covering, hides its neighbors' faces.
// Cutout: alpha-tested holes (leaves, plants). Translucent: alpha-blended (water, ice).
enum class Layer : uint8_t { Opaque, Cutout, Translucent };

// Cube: six faces. Cross: two double-sided diagonal quads (plants). Liquid: a cube whose top sits
// at 0.875 when the block above is not the same liquid.
enum class Shape : uint8_t { Cube, Cross, Liquid };

struct BlockDef {
    std::string_view name;
    bool solid;      // collides; the block raycast targets every block but AIR and WATER
    bool opaque;     // hides neighbor faces; true exactly when layer == Layer::Opaque
    bool breakable;
    std::array<Tile, 6> tiles;  // indexed by Face
    Layer layer = Layer::Opaque;
    Shape shape = Shape::Cube;
    uint8_t emission = 0;  // block light the block gives off, 0..15
    // Light a cell of this block takes away: 15 for every opaque block, LEAVES and PINE_LEAVES 1,
    // WATER and ICE 2, everything else 0.
    uint8_t opacity = 15;
    // Mining (M4 Task 3, see mining.hpp): seconds = hardness * 1.5 by hand; `tool` speeds it up;
    // a held tier below min_tier (with the right tool) slows it to hardness * 5 and drops nothing.
    float hardness = 0;
    ToolKind tool = ToolKind::None;
    uint8_t min_tier = 0;
    // Breaking drops drop_min..drop_max of `drop` (0 = nothing), with drop_chance percent.
    ItemId drop = 0;
    uint8_t drop_min = 1, drop_max = 1;
    uint8_t drop_chance = 100;
};

const BlockDef& block_def(BlockId id);  // ids >= blk::COUNT return the AIR def
