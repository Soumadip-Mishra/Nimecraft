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
    {"Air", false, false, true, all(T_STONE)},  // tiles unused: AIR is never meshed
    {"Grass", true, true, true, top_bottom_side(T_GRASS_TOP, T_DIRT, T_GRASS_SIDE)},
    {"Dirt", true, true, true, all(T_DIRT)},
    {"Stone", true, true, true, all(T_STONE)},
    {"Sand", true, true, true, all(T_SAND)},
    {"Log", true, true, true, top_bottom_side(T_LOG_TOP, T_LOG_TOP, T_LOG_SIDE)},
    {"Leaves", true, true, true, all(T_LEAVES)},
    {"Planks", true, true, true, all(T_PLANKS)},
    {"Deepstone", true, true, false, all(T_DEEPSTONE)},
}};

}  // namespace

const BlockDef& block_def(BlockId id) {
    return DEFS[id < blk::COUNT ? id : blk::AIR];
}
