// Player movement: look angles, walking, jumping, gravity, and AABB collision against the World.
//
// Playable range: |x|, |z| <= 30,000 blocks. Positions are floats (Vector3), whose spacing grows
// with distance: beyond this range walking speed drifts by more than 2%, and near x = 1,000,000 a
// substep rounds to a whole 1/16 block (wrong speed, wall climbing). Going further needs double or
// origin-rebased positions; test_player_far_coordinates guards the limit.
#pragma once

#include "raylib.h"  // Vector3 only

class World;

constexpr float PLAYER_HALF_W = 0.3f, PLAYER_H = 1.8f, PLAYER_EYE = 1.62f, PLAYER_SPEED = 4.3f,
                GRAVITY = 28.f, JUMP_VEL = 9.f, MAX_FALL = 50.f, MOUSE_SENS = 0.003f, REACH = 6.f;
// In water: weaker gravity, a slow sink, swimming up while jump is held, slower walking.
constexpr float WATER_GRAVITY = 6.f, MAX_SINK = 3.f, SWIM_VEL = 3.5f, WATER_SPEED = 0.6f;

struct Player {
    Vector3 pos;  // feet center
    Vector3 vel{};
    float yaw = 0, pitch = 0;
    bool on_ground = false;
};

struct PlayerInput {
    float forward = 0, right = 0;
    bool jump = false;
    float mouse_dx = 0, mouse_dy = 0;
};

// x = z = 0.5, feet on top of the highest solid or WATER block at (0,0) (on the surface of an
// ocean, not its floor); the column must be loaded. With no such block the feet are at y = 0, on
// the DEEPSTONE below the world.
Player spawn_player(const World& w);

// Any cell the player's box overlaps (touching faces do not count) is WATER.
bool in_water(const Player& p, const World& w);

// The cell holding player_eye(p) is WATER (the underwater screen tint).
bool eye_in_water(const Player& p, const World& w);

// yaw += dx * MOUSE_SENS, pitch -= dy * MOUSE_SENS clamped to [-1.55, 1.55]. Walking uses
// forward (cos yaw, 0, sin yaw) and right (-sin yaw, 0, cos yaw), the input normalized when longer
// than 1. dt is clamped to [0, 0.1] (NaN counts as 0) and integrated in substeps of at most
// 1/120 s, resolving collisions along X, then Y, then Z. If the player's column is not loaded,
// only the look angles change. When in_water at the start of the update, walking speed is
// x WATER_SPEED and jump does not jump; in each substep that starts in_water, vel.y becomes
// SWIM_VEL while jump is held, else max(vel.y - WATER_GRAVITY * h, -MAX_SINK).
void player_update(Player& p, const World& w, const PlayerInput& in, float dt);

Vector3 player_eye(const Player& p);
Vector3 player_look(const Player& p);  // (cos p cos y, sin p, cos p sin y)
bool player_intersects_block(const Player& p, int x, int y, int z);
