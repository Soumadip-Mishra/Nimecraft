// Sky and block light: computing a fresh column on a worker, stitching it to loaded neighbors,
// and updating light after a block edit. Levels are 0..15, stored per cell in Chunk::light.
//
// BFS rule: light moving into a neighbor cell becomes max(0, l - 1 - opacity(neighbor)), except
// sky light moving straight down, which becomes max(0, l - opacity(neighbor)) (unchanged through
// air). Sky light enters the world top at 15, so a cell at y = 255 has at least
// 15 - opacity(its block); an emitter has at least its BlockDef::emission as block light. Each
// cell holds the maximum over its sources, never a sum.
#pragma once

#include "block.hpp"
#include "chunk.hpp"
#include "coords.hpp"

#include <cstdint>

class World;

// Worker-safe (touches only col): resets every cell's light, then fills sky light straight down
// from the top and spreads it inside the column, and spreads block light from emitters inside the
// column. Light that would leave the column is dropped; stitch_column_light brings it across
// once the column sits in a World. Every slot of col is non-null.
void compute_column_light(Column& col);

// Main thread: spreads light across the borders between loaded column c and each loaded one of
// its 4 horizontal neighbors, in both directions and onward through any loaded columns. Light only
// rises. Marks dirty every loaded chunk whose mesh snapshot (the chunk plus a 1-cell border) holds a
// changed cell. Does nothing if c is not loaded.
void stitch_column_light(World& w, ColumnPos c);

// Main thread: called by World::set after the block at (x, y, z) changed from `before` to
// `after`. Removes the light that depended on the cell (removal BFS) and spreads light back in
// (add BFS), for sky light when the opacity changed and for block light when the opacity or the
// emission changed. Reads and writes loaded columns only, and marks changed chunks dirty as
// stitch_column_light does.
void relight_block(World& w, int x, int y, int z, BlockId before, BlockId after);

// Light at a world cell. Above the world: sky 15, block 0. Below it, and in unloaded columns: 0.
uint8_t sky_light_at(const World& w, int x, int y, int z);
uint8_t block_light_at(const World& w, int x, int y, int z);
