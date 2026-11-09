// Shared axis-aligned box movement against the World's solid blocks, used by the player, item
// entities, and (M6) mobs. Logic only: raylib for Vector3.
#pragma once

#include "raylib.h"  // Vector3 only

class World;

struct AABB {
    Vector3 min, max;
};

// The box of an entity whose feet center is `feet`: x and z span feet -+ half_extents.x / .z,
// y spans feet.y .. feet.y + height (half_extents.y is unused).
AABB aabb_at(Vector3 feet, Vector3 half_extents, float height);

// The box's interior overlaps block (x, y, z); faces that only touch the block do not count.
bool aabb_overlaps_block(const AABB& b, int x, int y, int z);

// The cells a box [mn, mx] overlaps on each axis, lo[i]..hi[i] inclusive; faces that only touch a
// cell (within 1e-5) do not count.
void cell_span(const double mn[3], const double mx[3], int lo[3], int hi[3]);

struct MoveResult {
    Vector3 pos;
    Vector3 vel;
    bool on_ground;     // some substep was stopped by a solid block while moving down
    bool hit_x, hit_z;  // some substep was stopped along X / Z
};

// Moves the box at `pos` (feet center, sized as aabb_at) by vel * dt and stops it flush against
// solid blocks. No gravity: callers change vel themselves. dt is clamped to at most 1 s; zero,
// negative, or NaN dt, a non-finite velocity, or a feet position in an unloaded column return pos
// and vel unchanged with every flag false. The motion runs in substeps of at most 1/120 s, and more
// when needed so no substep moves half a block or more (at most 65,536 substeps), each resolved
// along X, then Y, then Z. A blocked axis zeroes that velocity component.
MoveResult move_aabb(const World& w, Vector3 pos, Vector3 vel, Vector3 half_extents_feet, float height,
                     float dt);
