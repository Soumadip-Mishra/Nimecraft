#include "world_time.hpp"

#include <cmath>

namespace {

constexpr int DAY_FULL = 1200, DUSK_START = 14400, NIGHT_FULL = 16800, DAWN_START = 22800;
constexpr int RAMP = 2400;  // ticks per ramp
constexpr float NIGHT_LIGHT = 0.15f;
constexpr double HALF_TURN = 3.14159265358979323846;  // pi; raylib.h defines PI as a float macro

int wrap(int t) {
    const int m = t % DAY_TICKS;
    return m < 0 ? m + DAY_TICKS : m;
}

float smoothstep(float t) {
    return t * t * (3.f - 2.f * t);
}

// Where t lies inside a ramp: dusk (0, 1) over (DUSK_START, NIGHT_FULL), dawn (0, 1) over
// (DAWN_START, DAY_FULL) across midnight; -1 on a plateau, endpoints included, so plateaus give
// their exact values.
float dusk_fraction(int t) {
    if (t <= DUSK_START || t >= NIGHT_FULL) return -1.f;
    return static_cast<float>(t - DUSK_START) / RAMP;
}

float dawn_fraction(int t) {
    if (t >= DAY_FULL && t <= DAWN_START) return -1.f;
    return static_cast<float>(wrap(t - DAWN_START)) / RAMP;
}

bool day_plateau(int t) {
    return t >= DAY_FULL && t <= DUSK_START;
}

unsigned char mix(unsigned char a, unsigned char b, float t) {
    return static_cast<unsigned char>(std::lround(a + (b - a) * t));
}

Color blend(Color a, Color b, float t) {
    return Color{mix(a.r, b.r, t), mix(a.g, b.g, t), mix(a.b, b.b, t), 255};
}

constexpr Color DAY_SKY{135, 206, 235, 255}, DUSK_SKY{240, 140, 90, 255}, NIGHT_SKY{10, 12, 30, 255};

}  // namespace

void WorldTime::advance(float dt) {
    if (!(dt > 0.f) || !std::isfinite(dt)) return;
    ticks += static_cast<double>(dt) * TICKS_PER_SECOND;
}

int WorldTime::day_tick() const {
    return static_cast<int>(std::fmod(std::floor(ticks), static_cast<double>(DAY_TICKS)));
}

bool WorldTime::is_night() const {
    return day_tick() >= DAY_END;
}

float daylight_at(int day_tick) {
    const int t = wrap(day_tick);
    const float dusk = dusk_fraction(t);
    if (dusk >= 0.f) return 1.f - (1.f - NIGHT_LIGHT) * smoothstep(dusk);
    const float dawn = dawn_fraction(t);
    if (dawn >= 0.f) return NIGHT_LIGHT + (1.f - NIGHT_LIGHT) * smoothstep(dawn);
    return day_plateau(t) ? 1.f : NIGHT_LIGHT;
}

Color sky_color(int day_tick) {
    const int t = wrap(day_tick);
    const float dusk = dusk_fraction(t);
    if (dusk >= 0.f) return dusk < 0.5f ? blend(DAY_SKY, DUSK_SKY, dusk * 2) : blend(DUSK_SKY, NIGHT_SKY, dusk * 2 - 1);
    const float dawn = dawn_fraction(t);
    if (dawn >= 0.f) return dawn < 0.5f ? blend(NIGHT_SKY, DUSK_SKY, dawn * 2) : blend(DUSK_SKY, DAY_SKY, dawn * 2 - 1);
    return day_plateau(t) ? DAY_SKY : NIGHT_SKY;
}

float sun_angle(int day_tick) {
    const int t = wrap(day_tick);
    if (t < DAY_END) return static_cast<float>(HALF_TURN * t / DAY_END);
    return static_cast<float>(HALF_TURN + HALF_TURN * (t - DAY_END) / (DAY_TICKS - DAY_END));
}
