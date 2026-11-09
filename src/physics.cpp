#include "physics.hpp"

#include "block.hpp"
#include "coords.hpp"
#include "world.hpp"

#include <algorithm>
#include <cmath>

namespace {

constexpr float MAX_DT = 1.f, SUBSTEP = 1.f / 120.f;
constexpr double MAX_STEP_DIST = 0.5;  // blocks one substep may move along an axis, exclusive
// Bounds the work per call; a box faster than MAX_STEPS * MAX_STEP_DIST blocks/s may tunnel.
constexpr double MAX_STEPS = 65536;
// Box faces exactly on a block boundary touch the block but do not overlap it; this margin keeps
// float rounding at such a boundary from counting as overlap.
constexpr double TOUCH = 1e-5;

float& component(Vector3& v, int axis) {
    return axis == 0 ? v.x : (axis == 1 ? v.y : v.z);
}

struct Extents {
    double lo[3], hi[3];  // feet center to box min / max, per axis
};

// Moves pos by d along one axis and stops it flush against the first solid block in the way.
// Returns true when blocked. A substep moves less than one block, so one pass suffices.
bool move_axis(Vector3& pos, const World& w, const Extents& e, int axis, float d) {
    if (d == 0.f) return false;
    float& coord = component(pos, axis);
    const double moved = static_cast<double>(coord) + d;

    double mn[3] = {pos.x - e.lo[0], pos.y - e.lo[1], pos.z - e.lo[2]};
    double mx[3] = {pos.x + e.hi[0], pos.y + e.hi[1], pos.z + e.hi[2]};
    mn[axis] = moved - e.lo[axis];
    mx[axis] = moved + e.hi[axis];
    int lo[3], hi[3];
    cell_span(mn, mx, lo, hi);

    bool blocked = false;
    int limit = d > 0 ? hi[axis] : lo[axis];  // nearest blocking cell along the motion
    for (int y = lo[1]; y <= hi[1]; ++y)
        for (int z = lo[2]; z <= hi[2]; ++z)
            for (int x = lo[0]; x <= hi[0]; ++x) {
                if (!block_def(w.get(x, y, z)).solid) continue;
                const int c = axis == 0 ? x : (axis == 1 ? y : z);
                limit = blocked ? (d > 0 ? std::min(limit, c) : std::max(limit, c)) : c;
                blocked = true;
            }
    if (!blocked) {
        coord = static_cast<float>(moved);
        return false;
    }
    coord = static_cast<float>(d > 0 ? limit - e.hi[axis] : limit + 1 + e.lo[axis]);
    return true;
}

}  // namespace

AABB aabb_at(Vector3 feet, Vector3 half_extents, float height) {
    return AABB{Vector3{feet.x - half_extents.x, feet.y, feet.z - half_extents.z},
                Vector3{feet.x + half_extents.x, feet.y + height, feet.z + half_extents.z}};
}

bool aabb_overlaps_block(const AABB& b, int x, int y, int z) {
    return b.min.x < x + 1 && b.max.x > x && b.min.y < y + 1 && b.max.y > y && b.min.z < z + 1 &&
           b.max.z > z;
}

void cell_span(const double mn[3], const double mx[3], int lo[3], int hi[3]) {
    for (int i = 0; i < 3; ++i) {
        lo[i] = static_cast<int>(std::floor(mn[i] + TOUCH));
        hi[i] = static_cast<int>(std::ceil(mx[i] - TOUCH)) - 1;
    }
}

MoveResult move_aabb(const World& w, Vector3 pos, Vector3 vel, Vector3 half_extents_feet, float height,
                     float dt) {
    MoveResult r{pos, vel, false, false, false};
    if (!(dt > 0.f)) return r;  // zero, negative, or NaN
    if (!std::isfinite(vel.x) || !std::isfinite(vel.y) || !std::isfinite(vel.z)) return r;
    const ChunkPos feet = chunk_of(static_cast<int>(std::floor(pos.x)), 0, static_cast<int>(std::floor(pos.z)));
    if (!w.column_loaded(column_of(feet))) return r;  // frozen until its column streams in
    dt = std::min(dt, MAX_DT);

    const double speed = std::max({std::fabs(vel.x), std::fabs(vel.y), std::fabs(vel.z)});
    const double by_time = std::ceil(static_cast<double>(dt) / SUBSTEP);
    const double by_dist = std::floor(speed * dt / MAX_STEP_DIST) + 1;
    const int steps = static_cast<int>(std::min(std::max(by_time, by_dist), MAX_STEPS));
    const float h = dt / static_cast<float>(steps);
    const Extents e{{half_extents_feet.x, 0.0, half_extents_feet.z},
                    {half_extents_feet.x, height, half_extents_feet.z}};
    for (int i = 0; i < steps; ++i) {
        if (move_axis(r.pos, w, e, 0, r.vel.x * h)) {
            r.vel.x = 0.f;
            r.hit_x = true;
        }
        const float dy = r.vel.y * h;
        if (move_axis(r.pos, w, e, 1, dy)) {
            r.vel.y = 0.f;
            if (dy < 0.f) r.on_ground = true;
        }
        if (move_axis(r.pos, w, e, 2, r.vel.z * h)) {
            r.vel.z = 0.f;
            r.hit_z = true;
        }
    }
    return r;
}
