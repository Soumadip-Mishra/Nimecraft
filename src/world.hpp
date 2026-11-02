// Loaded columns, world-space block get/set, and the set of chunks whose mesh is stale.
// Main thread only (Global Constraints): worker jobs get copies, never a World.
#pragma once

#include "block.hpp"
#include "chunk.hpp"
#include "coords.hpp"

#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class World {
public:
    explicit World(uint32_t seed);
    uint32_t seed() const;

    bool column_loaded(ColumnPos c) const;
    // Replaces any column at c and marks its 16 chunks dirty. Null slots are filled with AIR
    // chunks, and every chunk's pos is set to {c.x, y, c.z}.
    void insert_column(ColumnPos c, Column col);
    // Returns the column (empty Column if absent) and drops its chunks from the dirty set.
    Column remove_column(ColumnPos c);
    std::vector<ColumnPos> loaded_columns() const;  // unspecified order

    Chunk* chunk(ChunkPos p);  // nullptr if unloaded or y outside [0, SECTIONS)
    const Chunk* chunk(ChunkPos p) const;

    BlockId get(int x, int y, int z) const;  // y < 0 -> DEEPSTONE, y >= 256 -> AIR, unloaded -> AIR
    // false if y is out of range or the column is unloaded. Setting the block already there
    // returns true and changes nothing. Otherwise marks the chunk modified and dirty, and marks
    // dirty each loaded neighbor chunk the block borders.
    bool set(int x, int y, int z, BlockId b);
    std::vector<ChunkPos> take_dirty();  // returns and clears, no duplicates, unspecified order

private:
    void mark_dirty_if_loaded(ChunkPos p);

    uint32_t seed_;
    std::unordered_map<ColumnPos, Column, ColumnPosHash> columns_;
    std::unordered_set<ChunkPos, ChunkPosHash> dirty_;
};
