// Tests for src/vitals.*: exhaustion, hunger, saturation, regeneration, starving, air, drowning,
// fall damage and fall tracking, sprinting.
#include "player.hpp"
#include "test.hpp"
#include "vitals.hpp"

#include <limits>

namespace {

VitalsInput walk(float m) {
    VitalsInput in;
    in.walked_m = m;
    return in;
}

VitalsInput underwater() {
    VitalsInput in;
    in.eye_in_water = true;
    return in;
}

bool same(const Vitals& a, const Vitals& b) {
    return a.health == b.health && a.hunger == b.hunger && a.saturation == b.saturation && a.air == b.air &&
           a.exhaustion == b.exhaustion && a.regen_timer == b.regen_timer && a.starve_timer == b.starve_timer;
}

}  // namespace

TEST(test_exhaustion_drains_saturation_first) {
    Vitals v;
    vitals_tick(v, walk(400.f), 0.f, GameMode::Survival);
    CHECK(v.saturation == 4.f);
    CHECK(v.hunger == 20.f);
    CHECK_NEAR(v.exhaustion, 0.0, 1e-5);
    // Fractional saturation never goes below 0.
    Vitals f;
    f.saturation = 0.6f;
    vitals_tick(f, walk(400.f), 0.f, GameMode::Survival);
    CHECK(f.saturation == 0.f);
    CHECK(f.hunger == 20.f);
}

TEST(test_exhaustion_then_hunger) {
    Vitals v;
    v.saturation = 0.f;
    vitals_tick(v, walk(400.f), 0.f, GameMode::Survival);
    CHECK(v.hunger == 19.f);
    CHECK(v.saturation == 0.f);
    // Several drains in one tick: 1200 m = 12 exhaustion = 3 hunger.
    Vitals m;
    m.saturation = 0.f;
    vitals_tick(m, walk(1200.f), 0.f, GameMode::Survival);
    CHECK(m.hunger == 17.f);
    // Hunger never goes below 0.
    Vitals z;
    z.saturation = 0.f;
    z.hunger = 1.f;
    vitals_tick(z, walk(1200.f), 0.f, GameMode::Survival);
    CHECK(z.hunger == 0.f);
}

TEST(test_exhaustion_rates) {
    VitalsInput sprint, jumps, mined, attacks;
    sprint.sprinted_m = 40.f;
    jumps.jumps = 80;
    mined.blocks_mined = 800;
    attacks.attacks = 40;
    for (const VitalsInput& in : {sprint, jumps, mined, attacks}) {
        Vitals v;
        vitals_tick(v, in, 0.f, GameMode::Survival);
        CHECK(v.saturation == 4.f);
        CHECK_NEAR(v.exhaustion, 0.0, 1e-5);
    }
    // Just below 4: nothing drains yet.
    VitalsInput below;
    below.sprinted_m = 39.f;
    Vitals v;
    vitals_tick(v, below, 0.f, GameMode::Survival);
    CHECK(v.saturation == 5.f);
    CHECK_NEAR(v.exhaustion, 3.9, 1e-5);
    // Exhaustion accumulates across ticks.
    vitals_tick(v, walk(10.f), 0.f, GameMode::Survival);
    CHECK(v.saturation == 4.f);
    CHECK_NEAR(v.exhaustion, 0.0, 1e-5);
}

TEST(test_regen_when_fed) {
    Vitals a;
    a.hunger = 18.f;
    a.health = 10.f;
    vitals_tick(a, VitalsInput{}, 3.99f, GameMode::Survival);
    CHECK(a.health == 10.f);
    CHECK(a.exhaustion == 0.f);
    Vitals b;
    b.hunger = 18.f;
    b.health = 10.f;
    vitals_tick(b, VitalsInput{}, 4.f, GameMode::Survival);
    CHECK(b.health == 11.f);
    CHECK(b.exhaustion == 6.f);
    CHECK(b.saturation == 5.f);  // the regen exhaustion drains on a later tick
    vitals_tick(b, VitalsInput{}, 0.f, GameMode::Survival);
    CHECK(b.saturation == 4.f);
    CHECK_NEAR(b.exhaustion, 2.0, 1e-5);
    // Regeneration caps at 20.
    Vitals c;
    c.health = 19.5f;
    vitals_tick(c, VitalsInput{}, 4.f, GameMode::Survival);
    CHECK(c.health == 20.f);
}

TEST(test_no_regen_boundaries) {
    Vitals a;
    a.hunger = 17.f;
    a.health = 10.f;
    vitals_tick(a, VitalsInput{}, 8.f, GameMode::Survival);
    CHECK(a.health == 10.f);
    CHECK(a.exhaustion == 0.f);
    Vitals b;
    vitals_tick(b, VitalsInput{}, 8.f, GameMode::Survival);
    CHECK(b.health == 20.f);
    CHECK(b.exhaustion == 0.f);
    // The regen timer restarts when regeneration stops being possible.
    Vitals c;
    c.hunger = 18.f;
    c.health = 10.f;
    vitals_tick(c, VitalsInput{}, 3.f, GameMode::Survival);
    c.hunger = 17.f;
    vitals_tick(c, VitalsInput{}, 1.f, GameMode::Survival);
    c.hunger = 18.f;
    vitals_tick(c, VitalsInput{}, 3.f, GameMode::Survival);
    CHECK(c.health == 10.f);
    vitals_tick(c, VitalsInput{}, 1.f, GameMode::Survival);
    CHECK(c.health == 11.f);
}

TEST(test_starve_damage) {
    Vitals v;
    v.hunger = 0.f;
    vitals_tick(v, VitalsInput{}, 3.99f, GameMode::Survival);
    CHECK(v.health == 20.f);
    vitals_tick(v, VitalsInput{}, 0.01f, GameMode::Survival);
    CHECK(v.health == 19.f);
    vitals_tick(v, VitalsInput{}, 4.f, GameMode::Survival);
    CHECK(v.health == 18.f);
    Vitals low;
    low.hunger = 0.f;
    low.health = 0.5f;
    vitals_tick(low, VitalsInput{}, 8.f, GameMode::Survival);
    CHECK(low.health == 0.f);
    // The starve timer restarts once the player has eaten.
    Vitals fed;
    fed.hunger = 0.f;
    vitals_tick(fed, VitalsInput{}, 3.f, GameMode::Survival);
    fed.hunger = 1.f;
    vitals_tick(fed, VitalsInput{}, 1.f, GameMode::Survival);
    fed.hunger = 0.f;
    vitals_tick(fed, VitalsInput{}, 3.f, GameMode::Survival);
    CHECK(fed.health == 20.f);
}

TEST(test_drowning) {
    Vitals v;
    vitals_tick(v, underwater(), 10.f, GameMode::Survival);
    CHECK(v.air == 0.f);
    CHECK(v.health == 20.f);
    vitals_tick(v, underwater(), 1.f, GameMode::Survival);
    CHECK(v.health == 18.f);
    CHECK(v.air == 0.f);
    // Half a second of air left, then a 1 s tick: 0.5 s at zero air.
    Vitals h;
    h.air = 0.5f;
    vitals_tick(h, underwater(), 1.f, GameMode::Survival);
    CHECK(h.air == 0.f);
    CHECK(h.health == 19.f);
    // Health never goes below 0.
    Vitals d;
    d.air = 0.f;
    d.health = 1.f;
    vitals_tick(d, underwater(), 5.f, GameMode::Survival);
    CHECK(d.health == 0.f);
}

TEST(test_air_refills) {
    Vitals v;
    v.air = 0.f;
    vitals_tick(v, VitalsInput{}, 2.f, GameMode::Survival);
    CHECK(v.air == 10.f);
    Vitals p;
    p.air = 3.f;
    vitals_tick(p, VitalsInput{}, 0.4f, GameMode::Survival);
    CHECK_NEAR(p.air, 5.0, 1e-5);
    Vitals full;
    vitals_tick(full, VitalsInput{}, 1.f, GameMode::Survival);
    CHECK(full.air == 10.f);
}

TEST(test_vitals_large_dt) {
    Vitals one, many;
    one.hunger = many.hunger = 0.f;
    vitals_tick(one, VitalsInput{}, 8.f, GameMode::Survival);
    for (int i = 0; i < 8; ++i) vitals_tick(many, VitalsInput{}, 1.f, GameMode::Survival);
    CHECK(same(one, many));
    CHECK(one.health == 18.f);
    // dt = 1.0 matches 60 frames of 1/60 under water.
    Vitals a, b;
    vitals_tick(a, underwater(), 1.f, GameMode::Survival);
    for (int i = 0; i < 60; ++i) vitals_tick(b, underwater(), 1.f / 60.f, GameMode::Survival);
    CHECK_NEAR(a.air, 9.0, 1e-5);
    CHECK_NEAR(b.air, 9.0, 1e-4);
    // Input exhaustion counts once, not once per internal step.
    Vitals c;
    vitals_tick(c, walk(400.f), 8.f, GameMode::Survival);
    CHECK(c.saturation == 4.f);
}

TEST(test_vitals_bad_dt) {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    for (float dt : {nan, 0.f, -1.f, -inf, inf}) {
        Vitals v;
        v.hunger = 0.f;
        v.saturation = 1.f;
        v.air = 5.f;
        vitals_tick(v, walk(400.f), dt, GameMode::Survival);
        CHECK(v.saturation == 0.f);  // input still counts
        CHECK(v.health == 20.f);
        CHECK(v.air == 5.f);
        CHECK(v.starve_timer == 0.f);
    }
    // A huge finite dt counts as 60 s: 15 starve hits, not a hang.
    Vitals big;
    big.hunger = 0.f;
    vitals_tick(big, VitalsInput{}, 1e30f, GameMode::Survival);
    CHECK(big.health == 5.f);
    Vitals minute;
    minute.hunger = 0.f;
    vitals_tick(minute, VitalsInput{}, 60.f, GameMode::Survival);
    CHECK(minute.health == 5.f);
    Vitals more;
    more.hunger = 0.f;
    vitals_tick(more, VitalsInput{}, 64.f, GameMode::Survival);
    CHECK(more.health == 5.f);
}

TEST(test_fall_damage_table) {
    CHECK(fall_damage(0.f, false) == 0.f);
    CHECK(fall_damage(3.f, false) == 0.f);
    CHECK(fall_damage(3.01f, false) == 1.f);
    CHECK(fall_damage(4.f, false) == 1.f);
    CHECK(fall_damage(10.2f, false) == 8.f);
    CHECK(fall_damage(-1.f, false) == 0.f);
    CHECK(fall_damage(50.f, true) == 0.f);
    CHECK(fall_damage(std::numeric_limits<float>::quiet_NaN(), false) == 0.f);
}

TEST(test_fall_tracking) {
    // Standing: nothing.
    FallTracker f;
    CHECK(track_fall(f, 70.f, true, false) == 0.f);
    // Jump up to 71.2, fall to a floor at 60: 11.2 blocks from the peak.
    CHECK(track_fall(f, 70.5f, false, false) == 0.f);
    CHECK(track_fall(f, 71.2f, false, false) == 0.f);
    CHECK(track_fall(f, 65.f, false, false) == 0.f);
    CHECK_NEAR(track_fall(f, 60.f, true, false), 11.2, 1e-4);
    CHECK(track_fall(f, 60.f, true, false) == 0.f);  // only the landing frame reports it
    // Falling into water restarts the fall at the water.
    FallTracker w;
    track_fall(w, 100.f, true, false);
    track_fall(w, 90.f, false, false);
    CHECK(track_fall(w, 70.f, false, true) == 0.f);
    CHECK(track_fall(w, 66.f, true, true) == 0.f);
    // Swimming up out of water, then dropping 2 blocks back to its floor.
    FallTracker s;
    track_fall(s, 64.f, false, true);
    CHECK(track_fall(s, 64.f, false, false) == 0.f);
    CHECK_NEAR(track_fall(s, 62.f, true, false), 2.0, 1e-5);
    // Landing in the same frame the player leaves a reset (flying stops right at the floor).
    FallTracker g;
    track_fall(g, 100.f, false, true);
    CHECK(track_fall(g, 100.f, true, false) == 0.f);
}

TEST(test_can_sprint) {
    Vitals v;
    v.hunger = 6.f;
    CHECK(!can_sprint(v));
    v.hunger = 6.5f;
    CHECK(can_sprint(v));
    v.hunger = 0.f;
    CHECK(!can_sprint(v));
    v.hunger = 20.f;
    CHECK(can_sprint(v));
}

TEST(test_creative_unchanged) {
    VitalsInput all;
    all.walked_m = 400.f;
    all.sprinted_m = 40.f;
    all.jumps = 80;
    all.blocks_mined = 800;
    all.attacks = 40;
    all.eye_in_water = true;
    for (float hunger : {0.f, 18.f, 20.f}) {
        Vitals v;
        v.hunger = hunger;
        v.health = 10.f;
        v.air = 0.f;
        const Vitals before = v;
        vitals_tick(v, all, 8.f, GameMode::Creative);
        CHECK(same(v, before));
        vitals_tick(v, VitalsInput{}, 8.f, GameMode::Creative);
        CHECK(same(v, before));
    }
}
