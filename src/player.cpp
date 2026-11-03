#include "player.hpp"

#include "block.hpp"
#include "coords.hpp"
#include "world.hpp"

#include <algorithm>
#include <cmath>

namespace {

constexpr float PITCH_LIMIT = 1.55f, MAX_DT = 0.1f, SUBSTEP = 1.f / 120.f;
// Box faces exactly on a block boundary touch the block but do not overlap it; this margin keeps
// float rounding at such a boundary from counting as overlap.
constexpr double TOUCH = 1e-5;

float& component(Vector3& v, int axis) {
    return axis == 0 ? v.x : (axis == 1 ? v.y : v.z);
}

// The cells a box [mn, mx] overlaps per axis, lo[i]..hi[i]; faces that only touch a cell do not
// count. Shared by collision and in_water so both see the same cells.
void cell_span(const double mn[3], const double mx[3], int lo[3], int hi[3]) {
    for (int i = 0; i < 3; ++i) {
        lo[i] = static_cast<int>(std::floor(mn[i] + TOUCH));
        hi[i] = static_cast<int>(std::ceil(mx[i] - TOUCH)) - 1;
    }
}

// Moves the player by d along one axis and stops it flush against the first solid block in the
// way. Returns true when blocked. Substeps move less than one block, so one pass suffices.
bool move_axis(Player& p, const World& w, int axis, float d) {
    if (d == 0.f) return false;
    float& coord = component(p.pos, axis);
    const double ext_lo[3] = {PLAYER_HALF_W, 0.0, PLAYER_HALF_W};  // feet center to box min
    const double ext_hi[3] = {PLAYER_HALF_W, PLAYER_H, PLAYER_HALF_W};
    const double moved = static_cast<double>(coord) + d;

    double mn[3] = {p.pos.x - ext_lo[0], p.pos.y - ext_lo[1], p.pos.z - ext_lo[2]};
    double mx[3] = {p.pos.x + ext_hi[0], p.pos.y + ext_hi[1], p.pos.z + ext_hi[2]};
    mn[axis] = moved - ext_lo[axis];
    mx[axis] = moved + ext_hi[axis];
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
    coord = static_cast<float>(d > 0 ? limit - ext_hi[axis] : limit + 1 + ext_lo[axis]);
    return true;
}

int cell_of(float v) {
    return static_cast<int>(std::floor(v));
}

}  // namespace

Player spawn_player(const World& w) {
    Player p{};
    p.pos = Vector3{0.5f, 0.f, 0.5f};
    for (int y = WORLD_HEIGHT - 1; y >= 0; --y) {
        const BlockId b = w.get(0, y, 0);
        if (block_def(b).solid || b == blk::WATER) {
            p.pos.y = static_cast<float>(y + 1);
            break;
        }
    }
    return p;
}

bool in_water(const Player& p, const World& w) {
    const double px = p.pos.x, py = p.pos.y, pz = p.pos.z;
    const double mn[3] = {px - PLAYER_HALF_W, py, pz - PLAYER_HALF_W};
    const double mx[3] = {px + PLAYER_HALF_W, py + PLAYER_H, pz + PLAYER_HALF_W};
    int lo[3], hi[3];
    cell_span(mn, mx, lo, hi);
    for (int y = lo[1]; y <= hi[1]; ++y)
        for (int z = lo[2]; z <= hi[2]; ++z)
            for (int x = lo[0]; x <= hi[0]; ++x)
                if (w.get(x, y, z) == blk::WATER) return true;
    return false;
}

bool eye_in_water(const Player& p, const World& w) {
    const Vector3 e = player_eye(p);
    return w.get(cell_of(e.x), cell_of(e.y), cell_of(e.z)) == blk::WATER;
}

void player_update(Player& p, const World& w, const PlayerInput& in, float dt) {
    p.yaw += in.mouse_dx * MOUSE_SENS;
    p.pitch = std::clamp(p.pitch - in.mouse_dy * MOUSE_SENS, -PITCH_LIMIT, PITCH_LIMIT);

    const ChunkPos feet = chunk_of(static_cast<int>(std::floor(p.pos.x)), 0,
                                   static_cast<int>(std::floor(p.pos.z)));
    if (!w.column_loaded(column_of(feet))) return;  // frozen until its column streams in
    if (!(dt > 0.f)) return;                         // zero, negative, or NaN
    dt = std::min(dt, MAX_DT);

    const float c = std::cos(p.yaw), s = std::sin(p.yaw);
    float wx = c * in.forward - s * in.right;
    float wz = s * in.forward + c * in.right;
    const float len = std::sqrt(wx * wx + wz * wz);
    if (len > 1.f) {
        wx /= len;
        wz /= len;
    }
    const bool wet = in_water(p, w);
    const float speed = wet ? PLAYER_SPEED * WATER_SPEED : PLAYER_SPEED;
    p.vel.x = wx * speed;
    p.vel.z = wz * speed;
    if (in.jump && p.on_ground && !wet) {
        p.vel.y = JUMP_VEL;
        p.on_ground = false;
    }

    const int steps = std::max(1, static_cast<int>(std::ceil(static_cast<double>(dt) / SUBSTEP)));
    const float h = dt / static_cast<float>(steps);
    for (int i = 0; i < steps; ++i) {
        if (!in_water(p, w))
            p.vel.y = std::max(p.vel.y - GRAVITY * h, -MAX_FALL);
        else if (in.jump)
            p.vel.y = SWIM_VEL;
        else
            p.vel.y = std::max(p.vel.y - WATER_GRAVITY * h, -MAX_SINK);
        if (move_axis(p, w, 0, p.vel.x * h)) p.vel.x = 0.f;
        const float dy = p.vel.y * h;
        const bool blocked_y = move_axis(p, w, 1, dy);
        p.on_ground = blocked_y && dy < 0.f;
        if (blocked_y) p.vel.y = 0.f;
        if (move_axis(p, w, 2, p.vel.z * h)) p.vel.z = 0.f;
    }
}

Vector3 player_eye(const Player& p) {
    return Vector3{p.pos.x, p.pos.y + PLAYER_EYE, p.pos.z};
}

Vector3 player_look(const Player& p) {
    const float cp = std::cos(p.pitch);
    return Vector3{cp * std::cos(p.yaw), std::sin(p.pitch), cp * std::sin(p.yaw)};
}

bool player_intersects_block(const Player& p, int x, int y, int z) {
    return p.pos.x - PLAYER_HALF_W < x + 1 && p.pos.x + PLAYER_HALF_W > x &&
           p.pos.y < y + 1 && p.pos.y + PLAYER_H > y &&
           p.pos.z - PLAYER_HALF_W < z + 1 && p.pos.z + PLAYER_HALF_W > z;
}
