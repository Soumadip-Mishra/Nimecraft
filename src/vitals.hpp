// Survival vitals: health, hunger, saturation, air, exhaustion, regeneration, starving, drowning,
// fall damage, and sprinting. Pure logic.
#pragma once

#include "player.hpp"  // GameMode

constexpr float MAX_HEALTH = 20.f, MAX_HUNGER = 20.f, MAX_AIR = 10.f, SPRINT_MIN_HUNGER = 6.f;

struct Vitals {
    float health = 20, hunger = 20, saturation = 5, air = 10, exhaustion = 0, regen_timer = 0, starve_timer = 0;
};

// What the player did since the last tick.
struct VitalsInput {
    float walked_m = 0, sprinted_m = 0;
    int jumps = 0, blocks_mined = 0, attacks = 0;
    bool eye_in_water = false;
};

// Creative: no change. Survival:
// - The input adds exhaustion once per call: walk 0.01/m, sprint 0.1/m, jump 0.05, mined block
//   0.005, attack 0.1. While exhaustion >= 4: subtract 4, then saturation -1 if > 0 (not below 0),
//   else hunger -1 (not below 0). This drain runs before the timers, so regeneration's own
//   exhaustion drains on a later tick.
// - Then dt (NaN, infinite, or <= 0 counts as 0; above 60 counts as 60) runs in steps of at most
//   1 s, so a long dt equals the same time in 1 s ticks. Each step:
//   regen: while hunger >= 18 and health < 20, regen_timer counts up and every 4 s gives +1 health
//   (capped at 20) and +6 exhaustion; otherwise regen_timer is 0.
//   starving: while hunger is 0, starve_timer counts up and every 4 s takes 1 health; otherwise
//   starve_timer is 0.
//   air: -1/s with the eye in water, +5/s otherwise (max 10); time spent at 0 air takes 2 health/s.
//   Health never goes below 0.
void vitals_tick(Vitals& v, const VitalsInput& in, float dt, GameMode mode);

// Landing in water: 0. Otherwise max(0, ceil(d - 3)); NaN gives 0.
float fall_damage(float fall_distance, bool landed_in_water);

// Sprinting is allowed while hunger > 6.
bool can_sprint(const Vitals& v);

// Measures falls for fall damage; call once per frame after player_update.
struct FallTracker {
    float peak_y = 0;
    bool airborne = false;
};
// Returns how far the player fell (highest y while airborne minus the landing y) in the frame they
// land (on_ground after being airborne), else 0. While `reset` (in water, flying, or Creative) the
// fall restarts at y and nothing is reported.
float track_fall(FallTracker& f, float y, bool on_ground, bool reset);
