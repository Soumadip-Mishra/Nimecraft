#include "block.hpp"

namespace {

constexpr std::array<Tile, 6> all(Tile t) {
    return {t, t, t, t, t, t};
}

// Face order: PX, NX, PY, NY, PZ, NZ.
constexpr std::array<Tile, 6> top_bottom_side(Tile top, Tile bottom, Tile side) {
    return {side, side, top, bottom, side, side};
}

// Indexed by BlockId; entries follow the blk:: order exactly.
constexpr std::array<BlockDef, blk::COUNT> DEFS{{
    {"Air", false, false, true, all(T_STONE), Layer::Opaque, Shape::Cube, 0, 0},  // tiles unused: never meshed
    {"Grass", true, true, true, top_bottom_side(T_GRASS_TOP, T_DIRT, T_GRASS_SIDE)},
    {"Dirt", true, true, true, all(T_DIRT)},
    {"Stone", true, true, true, all(T_STONE)},
    {"Sand", true, true, true, all(T_SAND)},
    {"Log", true, true, true, top_bottom_side(T_LOG_TOP, T_LOG_TOP, T_LOG_SIDE)},
    {"Leaves", true, false, true, all(T_LEAVES), Layer::Cutout, Shape::Cube, 0, 1},
    {"Planks", true, true, true, all(T_PLANKS)},
    {"Deepstone", true, true, false, all(T_DEEPSTONE)},
    {"Water", false, false, false, all(T_WATER), Layer::Translucent, Shape::Liquid, 0, 2},
    {"Gravel", true, true, true, all(T_GRAVEL)},
    {"Clay", true, true, true, all(T_CLAY)},
    {"Snow", true, true, true, top_bottom_side(T_SNOW, T_DIRT, T_SNOW_SIDE)},
    {"Ice", true, false, true, all(T_ICE), Layer::Translucent, Shape::Cube, 0, 2},
    {"Sandstone", true, true, true, top_bottom_side(T_SANDSTONE_TOP, T_SANDSTONE_TOP, T_SANDSTONE_SIDE)},
    {"Cactus", true, true, true, top_bottom_side(T_CACTUS_TOP, T_CACTUS_TOP, T_CACTUS_SIDE)},
    {"Pine Log", true, true, true, top_bottom_side(T_PINE_LOG_TOP, T_PINE_LOG_TOP, T_PINE_LOG_SIDE)},
    {"Pine Leaves", true, false, true, all(T_PINE_LEAVES), Layer::Cutout, Shape::Cube, 0, 1},
    {"Tall Grass", false, false, true, all(T_TALL_GRASS), Layer::Cutout, Shape::Cross, 0, 0},
    {"Flower", false, false, true, all(T_FLOWER), Layer::Cutout, Shape::Cross, 0, 0},
    {"Coal Ore", true, true, true, all(T_COAL_ORE)},
    {"Copper Ore", true, true, true, all(T_COPPER_ORE)},
    {"Tin Ore", true, true, true, all(T_TIN_ORE)},
    {"Iron Ore", true, true, true, all(T_IRON_ORE)},
    {"Silver Ore", true, true, true, all(T_SILVER_ORE)},
    {"Quartz", true, true, true, all(T_QUARTZ)},
    {"Torch", false, false, true, all(T_TORCH), Layer::Cutout, Shape::Cross, 14, 0},
    {"Lantern", true, false, true, all(T_LANTERN), Layer::Cutout, Shape::Cube, 15, 0},
    {"Glowcap", false, false, true, all(T_GLOWCAP), Layer::Cutout, Shape::Cross, 7, 0},
}};

}  // namespace

const BlockDef& block_def(BlockId id) {
    return DEFS[id < blk::COUNT ? id : blk::AIR];
}
