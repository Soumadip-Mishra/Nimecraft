// A* over walkable voxel cells for creatures. Pure logic on the World (main thread only).
#pragma once

#include "coords.hpp"

#include <cstdint>
#include <optional>
#include <vector>

class World;

struct PathParams {
    int height = 2;     // body height in cells; below 1 finds nothing
    int max_drop = 3;   // deepest drop taken; negative counts as 0
    int max_nodes = 2000;  // expansions before giving up
    int avoid_light_at_or_above = 16;  // cells with cell_light >= this are forbidden (16: none)
    float daylight = 1.f;  // the sky light multiplier for cell_light (daylight_at)
};

// max(block light, floor(sky light * day_light)) at p; 0 in unloaded columns and below the world,
// sky 15 above it.
int cell_light(const World& w, BlockPos p, float day_light);

// A walkable cell is in a loaded column with 0 <= y and y + height <= 256, has `height` non-solid
// cells from y up, and either a solid block below or WATER in the cell itself. Moves go to the 8
// horizontal neighbors (a diagonal only when both side cells are free for the body's height):
// level, up 1 (needing the cell above the head free), or down through free cells to the first
// walkable cell at most max_drop below. A move costs 10 (14 diagonal), x3 into a WATER cell.
// Cells (other than start) with cell_light >= avoid_light_at_or_above are forbidden. Returns the
// cells from start to goal inclusive, or nullopt when goal is not walkable or forbidden, nothing is
// found within max_nodes expansions, or the params are invalid. start == goal returns {start}.
// The same query returns the same path (ties broken by insertion order). `expanded`, when given,
// receives the number of expansions.
std::optional<std::vector<BlockPos>> find_path(const World& w, BlockPos start, BlockPos goal, const PathParams& p,
                                               int* expanded = nullptr);
