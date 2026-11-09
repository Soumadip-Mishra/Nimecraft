// Loaded columns, world-space block get/set, and the set of chunks whose mesh is stale.
// Main thread only (Global Constraints): worker jobs get copies, never a World.
#pragma once

#include "block.hpp"
#include "block_entities.hpp"
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
    // chunks, and every chunk's pos is set to {c.x, y, c.z}. Saved creatures in its chunks move to
    // the arrival list (chunk order).
    void insert_column(ColumnPos c, Column col);
    std::vector<SavedMob> take_arrived_mobs();  // returns and clears the arrival list
    // Returns the column (empty Column if absent) and drops its chunks from the dirty set.
    Column remove_column(ColumnPos c);
    std::vector<ColumnPos> loaded_columns() const;  // unspecified order

    Chunk* chunk(ChunkPos p);  // nullptr if unloaded or y outside [0, SECTIONS)
    const Chunk* chunk(ChunkPos p) const;

    BlockId get(int x, int y, int z) const;  // y < 0 -> DEEPSTONE, y >= 256 -> AIR, unloaded -> AIR
    // false if y is out of range or the column is unloaded. Setting the block already there
    // returns true and changes nothing. Otherwise marks the chunk modified and dirty, marks
    // dirty each loaded chunk whose mesh snapshot holds the block (the face, edge, and corner
    // neighbors it borders), removes the cell's block entity, and updates light (relight_block).
    bool set(int x, int y, int z, BlockId b);

    // Block entities live in their chunk (Chunk::entities), so they leave and return with
    // remove_column / insert_column. nullptr when absent, unloaded, or y outside [0, 256).
    BlockEntity* block_entity(BlockPos p);
    const BlockEntity* block_entity(BlockPos p) const;
    // Replaces the entity at p and marks the chunk modified; ignored when unloaded or y is out of
    // range. The caller keeps the block and its entity in step (World::set removes stale ones).
    void set_block_entity(BlockPos p, BlockEntity be);
    void remove_block_entity(BlockPos p);  // marks the chunk modified when an entity was removed
    std::vector<BlockPos> block_entity_positions() const;  // every loaded entity, unspecified order
    std::vector<ChunkPos> take_dirty();  // returns and clears, no duplicates, unspecified order
    void mark_dirty(ChunkPos p);         // adds p to the dirty set if its chunk is loaded

private:

    uint32_t seed_;
    std::unordered_map<ColumnPos, Column, ColumnPosHash> columns_;
    std::unordered_set<ChunkPos, ChunkPosHash> dirty_;
    std::vector<SavedMob> arrived_mobs_;
};
