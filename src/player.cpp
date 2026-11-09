#include "player.hpp"

#include "block.hpp"
#include "coords.hpp"
#include "physics.hpp"
#include "world.hpp"

#include <algorithm>
#include <cmath>

namespace {

constexpr float PITCH_LIMIT = 1.55f, MAX_DT = 0.1f, SUBSTEP = 1.f / 120.f;
constexpr Vector3 HALF_EXTENTS{PLAYER_HALF_W, 0.f, PLAYER_HALF_W};

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
    const bool wet = !p.flying && in_water(p, w);
    const float land = in.sprint ? PLAYER_SPEED * SPRINT_MUL : PLAYER_SPEED;
    const float speed = p.flying ? FLY_SPEED : (wet ? PLAYER_SPEED * WATER_SPEED : land);
    p.vel.x = wx * speed;
    p.vel.z = wz * speed;
    if (in.jump && p.on_ground && !wet && !p.flying) {
        p.vel.y = JUMP_VEL;
        p.on_ground = false;
    }

    const int steps = std::max(1, static_cast<int>(std::ceil(static_cast<double>(dt) / SUBSTEP)));
    const float h = dt / static_cast<float>(steps);
    for (int i = 0; i < steps; ++i) {
        if (p.flying) {
            p.vel.y = (static_cast<float>(in.jump) - static_cast<float>(in.descend)) * FLY_SPEED;
        } else if (!in_water(p, w)) {
            p.vel.y = std::max(p.vel.y - GRAVITY * h, -MAX_FALL);
        } else if (in.jump) {
            p.vel.y = SWIM_VEL;
        } else {
            p.vel.y = std::max(p.vel.y - WATER_GRAVITY * h, -MAX_SINK);
        }
        const MoveResult r = move_aabb(w, p.pos, p.vel, HALF_EXTENTS, PLAYER_H, h);
        p.pos = r.pos;
        p.vel = r.vel;
        p.on_ground = r.on_ground;
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
    return aabb_overlaps_block(aabb_at(p.pos, HALF_EXTENTS, PLAYER_H), x, y, z);
}
