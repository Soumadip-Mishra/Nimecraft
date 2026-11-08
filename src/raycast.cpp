#include "raycast.hpp"

#include "block.hpp"
#include "coords.hpp"
#include "world.hpp"

#include <cmath>
#include <limits>

namespace {

bool solid_in_world(const World& w, int x, int y, int z) {
    return y >= 0 && y < WORLD_HEIGHT && block_def(w.get(x, y, z)).solid;
}

// Every block but AIR and WATER, so non-solid TORCH, GLOWCAP, and plants can be broken.
bool targetable_in_world(const World& w, int x, int y, int z) {
    if (y < 0 || y >= WORLD_HEIGHT) return false;
    const BlockId b = w.get(x, y, z);
    return b != blk::AIR && b != blk::WATER;
}

// Per-axis DDA state; double keeps the boundary distances exact enough far from the origin.
struct Axis {
    int cell;
    int step;      // -1, 0, or 1
    double t_max;  // ray distance to the next cell boundary on this axis
    double t_delta;

    Axis(float origin, float dir) {
        const double o = origin, d = dir;
        cell = static_cast<int>(std::floor(o));
        constexpr double inf = std::numeric_limits<double>::infinity();
        if (d > 0) {
            step = 1;
            t_delta = 1.0 / d;
            t_max = (cell + 1.0 - o) / d;
        } else if (d < 0) {
            step = -1;
            t_delta = -1.0 / d;
            t_max = (o - cell) / -d;
        } else {
            step = 0;
            t_delta = inf;
            t_max = inf;
        }
    }
};

}  // namespace

RayHit raycast_voxel(const World& w, Vector3 origin, Vector3 dir, float max_dist) {
    if (!std::isfinite(max_dist)) return RayHit{};
    Axis a[3] = {Axis(origin.x, dir.x), Axis(origin.y, dir.y), Axis(origin.z, dir.z)};
    RayHit h{};
    if (solid_in_world(w, a[0].cell, a[1].cell, a[2].cell)) {
        h.hit = true;
        h.x = a[0].cell;
        h.y = a[1].cell;
        h.z = a[2].cell;
        return h;
    }
    for (;;) {
        // Leaving the world vertically: nothing above or below can be hit.
        if ((a[1].cell < 0 && a[1].step <= 0) || (a[1].cell >= WORLD_HEIGHT && a[1].step >= 0))
            return RayHit{};
        // Nearest boundary; ties go to X, then Y, then Z, one axis per step, so a ray through an
        // edge or corner visits a block next to it instead of skipping diagonally.
        int k = 0;
        if (a[1].t_max < a[k].t_max) k = 1;
        if (a[2].t_max < a[k].t_max) k = 2;
        const double t = a[k].t_max;
        if (!(t <= max_dist)) return RayHit{};  // also stops a zero direction (t = infinity)
        a[k].cell += a[k].step;
        a[k].t_max += a[k].t_delta;
        if (targetable_in_world(w, a[0].cell, a[1].cell, a[2].cell)) {
            h.hit = true;
            h.x = a[0].cell;
            h.y = a[1].cell;
            h.z = a[2].cell;
            h.nx = k == 0 ? -a[0].step : 0;
            h.ny = k == 1 ? -a[1].step : 0;
            h.nz = k == 2 ? -a[2].step : 0;
            h.distance = static_cast<float>(t);
            return h;
        }
    }
}
