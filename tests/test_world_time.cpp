// Tests for src/world_time.* (M3 Task 4) and the world.meta `time` key in src/storage.*.
#include "storage.hpp"
#include "test.hpp"
#include "world_time.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>

namespace {

void write_text(const std::filesystem::path& f, const std::string& s) {
    std::ofstream out(f, std::ios::binary);
    out << s;
}

const std::string M1_META = "version=1\nseed=7\npx=1\npy=70\npz=-2\nyaw=0\npitch=0\n";

bool same_color(Color a, Color b) {
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

}  // namespace

TEST(test_time_constants) {
    CHECK(TICKS_PER_SECOND == 20);
    CHECK(DAY_TICKS == 24000);
    CHECK(DAY_END == 15600);
    CHECK(WorldTime{}.ticks == 6000.0);
}

TEST(test_daylight_plateaus) {
    for (int t : {1200, 6000, 14400}) CHECK(daylight_at(t) == 1.0f);
    for (int t : {16800, 20000, 22800}) CHECK(daylight_at(t) == 0.15f);
}

TEST(test_daylight_monotonic_ramps) {
    float prev = daylight_at(14400);
    for (int t = 14400; t <= 16800; ++t) {
        CHECK(daylight_at(t) <= prev);
        prev = daylight_at(t);
    }
    prev = daylight_at(22800);
    for (int t = 22800; t < 24000; ++t) {
        CHECK(daylight_at(t) >= prev);
        prev = daylight_at(t);
    }
    for (int t = 0; t <= 1200; ++t) {
        CHECK(daylight_at(t) >= prev);
        prev = daylight_at(t);
    }
    for (int t = 0; t < 24000; ++t) CHECK(daylight_at(t) >= 0.15f && daylight_at(t) <= 1.0f);
}

TEST(test_daylight_ramp_points) {
    // Just inside each ramp the value has left its plateau; smoothstep's midpoint is halfway
    // between 1.0 and 0.15 (0.575) at 15600 (dusk) and at 0 (dawn, the ramp wraps over midnight).
    CHECK(daylight_at(14401) < 1.0f);
    CHECK(daylight_at(1199) < 1.0f);
    CHECK(daylight_at(16799) > 0.15f);
    CHECK(daylight_at(22801) > 0.15f);
    CHECK_NEAR(daylight_at(15600), 0.575, 1e-6);
    CHECK_NEAR(daylight_at(0), 0.575, 1e-6);
    // Smoothstep, not linear: a quarter into the dusk ramp, 3t^2 - 2t^3 = 0.15625 of the drop.
    CHECK_NEAR(daylight_at(15000), 1.0 - 0.85 * 0.15625, 1e-6);
    // Ticks outside [0, 24000) wrap to a day tick.
    CHECK(daylight_at(30000) == daylight_at(6000));
    CHECK(daylight_at(-4000) == daylight_at(20000));
}

TEST(test_is_night_boundary) {
    auto night = [](double ticks) {
        WorldTime t;
        t.ticks = ticks;
        return t.is_night();
    };
    CHECK(!night(15599));
    CHECK(night(15600));
    CHECK(night(23999));
    CHECK(!night(0));
    CHECK(!night(24000 + 15599.5));  // a later day: day_tick 15599
    CHECK(night(48000 + 15600));
}

TEST(test_time_advance) {
    WorldTime t;
    t.advance(1.0f);
    CHECK(t.ticks == 6020.0);
    t.advance(0.f);
    CHECK(t.ticks == 6020.0);
    t.advance(-1.f);
    CHECK(t.ticks == 6020.0);
    t.advance(std::numeric_limits<float>::quiet_NaN());
    CHECK(t.ticks == 6020.0);
    t.advance(std::numeric_limits<float>::infinity());
    CHECK(t.ticks == 6020.0);
    t.advance(0.5f);
    CHECK(t.ticks == 6030.0);
    CHECK(t.day_tick() == 6030);
}

TEST(test_time_wraps) {
    WorldTime t;
    t.ticks = 23990;
    t.advance(1.0f);
    CHECK(t.day_tick() == 10);
    t.ticks = 24000;
    CHECK(t.day_tick() == 0);
    t.ticks = 23999.9;
    CHECK(t.day_tick() == 23999);
    // Far past INT_MAX ticks (a very old world): 1e12 - 41666666 * 24000 = 16000, night.
    t.ticks = 1e12;
    CHECK(t.day_tick() == 16000);
    CHECK(t.is_night());
}

TEST(test_time_no_drift) {
    WorldTime t;
    t.ticks = 0;
    for (int i = 0; i < 72000; ++i) t.advance(1 / 60.f);
    CHECK(t.ticks >= 23999.0 && t.ticks <= 24001.0);
    // dt = 1.0 equals 60 steps of 1/60 within the same tolerance.
    WorldTime a, b;
    a.advance(1.0f);
    for (int i = 0; i < 60; ++i) b.advance(1 / 60.f);
    CHECK_NEAR(a.ticks, b.ticks, 1e-3);
    // A long step (a frame hitch of 1000 s) is not clamped: 20000 ticks.
    WorldTime c;
    c.advance(1000.f);
    CHECK(c.ticks == 26000.0);
}

TEST(test_sky_color_endpoints) {
    CHECK(same_color(sky_color(6000), Color{135, 206, 235, 255}));
    CHECK(same_color(sky_color(20000), Color{10, 12, 30, 255}));
    CHECK(same_color(sky_color(1200), Color{135, 206, 235, 255}));   // day plateau edges
    CHECK(same_color(sky_color(14400), Color{135, 206, 235, 255}));
    CHECK(same_color(sky_color(16800), Color{10, 12, 30, 255}));     // night plateau edges
    CHECK(same_color(sky_color(22800), Color{10, 12, 30, 255}));
    // Dusk sits in the middle of both ramps: 15600 and 0.
    CHECK(same_color(sky_color(15600), Color{240, 140, 90, 255}));
    CHECK(same_color(sky_color(0), Color{240, 140, 90, 255}));
    // Halfway from day to dusk: (135 + 240) / 2 = 187.5 -> 188, (206 + 140) / 2 = 173,
    // (235 + 90) / 2 = 162.5 -> 163.
    CHECK(same_color(sky_color(15000), Color{188, 173, 163, 255}));
}

TEST(test_sun_angle) {
    CHECK(sun_angle(0) == 0.0f);
    float prev = sun_angle(0);
    for (int t = 1; t < 24000; ++t) {
        CHECK(sun_angle(t) > prev);
        prev = sun_angle(t);
    }
    CHECK(prev < 6.2832f);
    // The sun sets (pi) as night begins, at DAY_END.
    CHECK_NEAR(sun_angle(15600), 3.14159265, 1e-5);
}

// ---- world.meta time ----

TEST(test_meta_time_default) {
    test::TempDir tmp;
    const auto f = tmp.path() / "world.meta";
    write_text(f, M1_META);
    const auto m = load_meta(f);
    CHECK(m.has_value());
    if (m) CHECK(m->time == 6000.0);
}

TEST(test_meta_time_roundtrip_and_invalid) {
    test::TempDir tmp;
    const auto f = tmp.path() / "world.meta";
    WorldMeta w{7u, 1.f, 70.f, -2.f, 0.f, 0.f};
    w.time = 12345.5;
    CHECK(save_meta(f, w));
    auto m = load_meta(f);
    CHECK(m.has_value());
    if (m) CHECK(m->time == 12345.5);
    w.time = 0;  // the lowest valid time
    CHECK(save_meta(f, w));
    m = load_meta(f);
    CHECK(m.has_value());
    if (m) CHECK(m->time == 0.0);
    w.time = 1e12;  // a very old world
    CHECK(save_meta(f, w));
    m = load_meta(f);
    CHECK(m.has_value());
    if (m) CHECK(m->time == 1e12);
    // Invalid values fall back to 6000 and keep the rest of the file.
    for (const char* bad : {"nan", "-5", "inf", "-inf", "abc", "", "12x", "1e400"}) {
        write_text(f, M1_META + "time=" + bad + "\n");
        m = load_meta(f);
        CHECK(m.has_value());
        if (m) {
            CHECK(m->time == 6000.0);
            CHECK(m->seed == 7u);
        }
    }
}
