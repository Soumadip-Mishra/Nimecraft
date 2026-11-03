// World clock: ticks, the daylight curve, sky colors, and the sun's angle. Pure logic; raylib is
// used only for the Color type.
//
// One day is DAY_TICKS = 24000 ticks at 20 ticks per second (20 minutes). Day tick 0 is sunrise;
// the day runs to DAY_END = 15600 (13 minutes), the night from there to 24000 (7 minutes).
#pragma once

#include "raylib.h"

constexpr int TICKS_PER_SECOND = 20, DAY_TICKS = 24000, DAY_END = 15600;

struct WorldTime {
    double ticks = 6000;  // total ticks since the world began (saved as world.meta `time`)
    // Adds dt * TICKS_PER_SECOND ticks; dt <= 0 or non-finite adds nothing. No clamp on long steps.
    void advance(float dt);
    int day_tick() const;  // floor(ticks) mod DAY_TICKS, in [0, DAY_TICKS); ticks >= 0
    bool is_night() const;  // day_tick() >= DAY_END
};

// Sky light multiplier: 1.0 over [1200, 14400], 0.15 over [16800, 22800], smoothstep ramps between
// (dusk 14400 -> 16800, dawn 22800 -> 1200 across midnight). Any int wraps to a day tick first.
// The plan calls it `daylight`; glibc's <time.h> already declares a global `int daylight`.
float daylight_at(int day_tick);

// Sky (and clear) color: day (135,206,235) on the day plateau, night (10,12,30) on the night
// plateau. Each ramp passes through dusk (240,140,90) at its middle: dusk ramp day -> dusk ->
// night, dawn ramp night -> dusk -> day, each half a linear blend, channels rounded. Alpha 255.
Color sky_color(int day_tick);

// Radians, 0 at sunrise (day tick 0), pi as night begins (DAY_END), rising linearly within the day
// [0, DAY_END) and within the night [DAY_END, DAY_TICKS) toward 2 pi. Wraps like daylight.
float sun_angle(int day_tick);
