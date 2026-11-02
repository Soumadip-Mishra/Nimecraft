// Block ids, face and tile enums, and the BlockDef table.
// Block ids are persisted in saves: append only, never renumber or reuse.
#pragma once

#include <array>
#include <cstdint>
#include <string_view>

using BlockId = uint16_t;

namespace blk {
constexpr BlockId AIR = 0, GRASS = 1, DIRT = 2, STONE = 3, SAND = 4, LOG = 5, LEAVES = 6, PLANKS = 7,
                  DEEPSTONE = 8, COUNT = 9;
}

enum Face : uint8_t { PX, NX, PY, NY, PZ, NZ };

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
    T_COUNT
};

struct BlockDef {
    std::string_view name;
    bool solid;
    bool opaque;
    bool breakable;
    std::array<Tile, 6> tiles;  // indexed by Face
};

const BlockDef& block_def(BlockId id);  // ids >= blk::COUNT return the AIR def
