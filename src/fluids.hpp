// Flowing water: one scheduled tick recomputes one cell from its neighbors.
// WATER meta: 0 = source, 1..7 = flowing level (higher = farther), 8 = falling; any other value
// counts as falling. Main thread only, like World.
#pragma once

#include "coords.hpp"

#include <cstdint>

class World;
class TickScheduler;

constexpr uint8_t WATER_SOURCE = 0, WATER_MAX_LEVEL = 7, WATER_FALLING = 8;
constexpr uint32_t FLUID_DELAY = 5;  // ticks between a water change and its neighbors' update
constexpr int FLUID_DROP_REACH = 4;  // flow steers toward a drop at most this many cells away

// Recomputes the cell at p (AIR or non-source WATER; anything else, and unloaded cells, are left
// alone), in this order:
// - WATER directly above: falling water.
// - 2+ horizontal source neighbors and a solid block or a source below: a source.
// - Fed by a horizontal water neighbor q: level(q) + 1, the lowest over every q that feeds, where
//   level is 0 for a source or falling water. q feeds p when level(q) + 1 <= 7, level(q) is below
//   p's own flowing level (if p is flowing), q spreads sideways (below q is neither AIR nor, for
//   flowing q, any WATER; a source over WATER spreads once the water under it stops falling), and
//   p lies in one of q's flow directions: among the open cells beside q (loaded AIR or non-source
//   WATER), those whose shortest open path to a drop (an open cell with AIR or non-source WATER
//   below) is shortest, if one lies within 4 cells; otherwise every open cell beside q.
// - Otherwise AIR: flowing water without a feeding neighbor recedes.
// On a change it schedules the 6 face neighbors after FLUID_DELAY ticks and, unless p is water
// still falling into AIR below, when the water column above p ends in a source, that source's 4
// horizontal neighbors (the column landed or broke, so the source may now spread or stop).
void fluid_update(World& w, TickScheduler& t, BlockPos p);

// For a column that just loaded: schedules (after FLUID_DELAY) its flowing water, the cells across
// its border from flowing water in it, and its cells beside flowing water in loaded neighbor columns,
// so water that met an unloaded column moves on. Sources are left at rest. Nothing if c is unloaded.
void schedule_column_water(const World& w, TickScheduler& t, ColumnPos c);
