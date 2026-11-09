// Creature behaviors: one AI step per mob. Pure logic on the World (main thread only).
#pragma once

#include "mobs.hpp"

#include "raylib.h"  // Vector3 only

// move_dir is the wanted horizontal velocity in blocks/s (y = 0); jump asks for a jump from the
// ground; attack is a melee hit on the player (damage = the mob's MobDef::damage); shoot fires a
// thorn at the player; next is the state the mob is in after this step.
struct AiOutput {
    Vector3 move_dir;
    bool jump;
    bool attack;
    bool shoot;
    AiState next;
};

constexpr float WANDER_RADIUS = 8.f, WANDER_MIN_S = 4.f, WANDER_MAX_S = 10.f, WANDER_ARRIVE = 0.5f;
constexpr float FLEE_S = 4.f, CHARGE_SPEED = 6.f, CHARGE_MAX_S = 3.f;
constexpr float HERD_RANGE = 10.f, HERD_CLOSE = 3.f;
constexpr float CHASE_RANGE = 24.f, REPATH_S = 1.f, ATTACK_S = 1.f;
constexpr float KITE_MIN = 8.f, KITE_MAX = 12.f, SHOOT_S = 2.f;
constexpr int GLOOM_AVOID_LIGHT = 10, FADE_SKY_LIGHT = 12;
constexpr float FADE_DAYLIGHT = 0.5f, FADE_DPS = 1.f;
constexpr float MOB_EYE = 0.9f;  // eye height as a fraction of the body height

// Eye of a mob: feet + 0.9 x height.
Vector3 mob_eye(const Mob& m);
// No opaque block on the segment a-b (sampled every 0.25 blocks).
bool line_of_sight(const World& w, Vector3 a, Vector3 b);

// A NaN, infinite, or non-positive dt changes no timer. Distances are between feet positions.
// - Gloomwalker: while sky light at its feet cell >= 12 and daylight > 0.5 -> Fade: no movement,
//   loses 1 health per second (not below 0). Fade wins over everything else.
// - Passive, when ctx.last_attacker_hit_mob is this mob: a Tusker that has not charged yet
//   Charges (toward the player at 6 blocks/s, for at most 3 s; reaching melee reach attacks once
//   and ends it), otherwise Flee. Charge and Flee then run on their timers; Flee moves away from
//   the player at the mob's speed for 4 s, then the mob wanders again.
// - Hostile, player alive, within 24 and in line of sight (eye to eye): Thornspitter Kites
//   (closer than 8: away; 8..12: holds; beyond 12: approaches; shoots every 2 s), others Chase
//   (path to the player's cell, re-planned every 1 s, Gloomwalker avoiding light >= 10; stops
//   within 0.8 x reach and attacks within reach, at most once per second).
// - Otherwise Idle/Wander: a Tusker with another Tusker within 10 (but farther than 3) walks
//   toward the nearest one; else a new random target within 8 every 4-10 s (the first at once),
//   walked to in a straight line (Wander) until within 0.5 (Idle).
AiOutput ai_step(Mob& m, const AiContext& ctx, float dt);
