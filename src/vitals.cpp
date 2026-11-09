#include "vitals.hpp"

#include <algorithm>
#include <cmath>

namespace {

constexpr float WALK_EX = 0.01f, SPRINT_EX = 0.1f, JUMP_EX = 0.05f, MINE_EX = 0.005f, ATTACK_EX = 0.1f;
constexpr float EX_PER_DRAIN = 4.f, REGEN_EX = 6.f, REGEN_MIN_HUNGER = 18.f, TIMER_SECONDS = 4.f;
constexpr float AIR_LOSS = 1.f, AIR_GAIN = 5.f, DROWN_DAMAGE = 2.f, MAX_STEP = 1.f, SAFE_FALL = 3.f;
constexpr float MAX_DT = 60.f;  // a longer call (a stalled frame) counts as one minute

void hurt(Vitals& v, float amount) {
    v.health = std::max(0.f, v.health - amount);
}

void step(Vitals& v, bool eye_in_water, float dt) {
    if (v.hunger >= REGEN_MIN_HUNGER && v.health < MAX_HEALTH) {
        v.regen_timer += dt;
        while (v.regen_timer >= TIMER_SECONDS && v.health < MAX_HEALTH) {
            v.regen_timer -= TIMER_SECONDS;
            v.health = std::min(MAX_HEALTH, v.health + 1.f);
            v.exhaustion += REGEN_EX;
        }
    } else {
        v.regen_timer = 0.f;
    }

    if (v.hunger <= 0.f) {
        v.starve_timer += dt;
        while (v.starve_timer >= TIMER_SECONDS) {
            v.starve_timer -= TIMER_SECONDS;
            hurt(v, 1.f);
        }
    } else {
        v.starve_timer = 0.f;
    }

    if (eye_in_water) {
        v.air -= AIR_LOSS * dt;
        if (v.air < 0.f) {
            hurt(v, DROWN_DAMAGE * -v.air / AIR_LOSS);
            v.air = 0.f;
        }
    } else {
        v.air = std::min(MAX_AIR, v.air + AIR_GAIN * dt);
    }
}

}  // namespace

void vitals_tick(Vitals& v, const VitalsInput& in, float dt, GameMode mode) {
    if (mode == GameMode::Creative) return;

    v.exhaustion += in.walked_m * WALK_EX + in.sprinted_m * SPRINT_EX + static_cast<float>(in.jumps) * JUMP_EX +
                    static_cast<float>(in.blocks_mined) * MINE_EX + static_cast<float>(in.attacks) * ATTACK_EX;
    while (v.exhaustion >= EX_PER_DRAIN) {
        v.exhaustion -= EX_PER_DRAIN;
        if (v.saturation > 0.f)
            v.saturation = std::max(0.f, v.saturation - 1.f);
        else
            v.hunger = std::max(0.f, v.hunger - 1.f);
    }

    if (!(dt > 0.f) || !std::isfinite(dt)) return;  // NaN, infinite, zero, or negative
    dt = std::min(dt, MAX_DT);
    while (dt > 0.f) {
        const float h = std::min(dt, MAX_STEP);
        step(v, in.eye_in_water, h);
        dt -= h;
    }
}

float fall_damage(float fall_distance, bool landed_in_water) {
    if (landed_in_water || !(fall_distance > SAFE_FALL)) return 0.f;
    return std::ceil(fall_distance - SAFE_FALL);
}

bool can_sprint(const Vitals& v) {
    return v.hunger > SPRINT_MIN_HUNGER;
}

float track_fall(FallTracker& f, float y, bool on_ground, bool reset) {
    if (reset) {
        f.airborne = !on_ground;
        f.peak_y = y;
        return 0.f;
    }
    if (!on_ground) {
        f.peak_y = f.airborne ? std::max(f.peak_y, y) : y;
        f.airborne = true;
        return 0.f;
    }
    if (!f.airborne) return 0.f;
    f.airborne = false;
    return std::max(0.f, f.peak_y - y);
}
