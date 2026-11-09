// Where and when creatures appear and disappear. Pure logic on the World (main thread only).
#pragma once

#include "coords.hpp"
#include "mobs.hpp"

#include "raylib.h"  // Vector3 only

#include <cstdint>

class World;

constexpr float SPAWN_MIN_DIST = 24.f, SPAWN_MAX_DIST = 64.f;
constexpr int PASSIVE_MIN_SKY = 9, HOSTILE_MAX_LIGHT = 4, CRAWLER_MAX_Y = 50;
constexpr float NIGHT_DAYLIGHT = 0.5f;  // night for spawning: daylight below this
constexpr float DESPAWN_FAR = 96.f, DESPAWN_LINGER = 48.f, DESPAWN_LINGER_S = 30.f;

// Whether a mob of type t may appear with its feet in cell `feet` (body centered at x + 0.5,
// z + 0.5). Always: the column is loaded, 0 < y and the body fits below y 256, the block below is
// solid, each cell of the body (ceil(height) cells from feet up) is not solid, not WATER, and gives
// no light, and the body center is at least 24 blocks from `player`.
// - Passive: the biome at (x, z) is one of the type's biomes, the block below is GRASS, SNOW, or
//   SAND, and the feet cell's sky light (not scaled by daylight) is >= 9.
// - Gloomwalker: cell_light(feet, day_light) <= 4.
// - Thornspitter: Forest, night (day_light < 0.5), and cell_light <= 4.
// - Cave Crawler: y < 50 and cell_light <= 4.
bool can_spawn(MobType t, const World& w, BlockPos feet, float day_light, Vector3 player);

struct SpawnCaps {
    int passive = 20, hostile = 25;
};

// One spawn attempt for each group (passive, hostile) whose count is under its cap; the game calls
// it once per second. An attempt picks a type of the group and a column 24-64 blocks (horizontal)
// from the player from rng, then a feet cell there: passive on the highest ground that is not a
// tree (log or leaves); hostile on the first floor at or below a random height within 32 of the
// player's, searching down 24 cells. If can_spawn holds, the mob appears there (seeded from rng);
// a passive mob brings up to 1 (Hopper), 2 (Fluffhorn), or 3 (Tusker) more of its kind on cells
// within 2 that also pass can_spawn and the ring, while the cap allows. A non-finite player
// position spawns nothing.
void spawn_tick(MobManager& m, const World& w, Vector3 player, float day_light, uint32_t& rng, SpawnCaps caps);

// Hostiles more than 96 from the player are removed. A hostile more than 48 away counts the time
// (dt) it stays there and is removed at 30 s; within 48 its count restarts. Passive mobs never
// despawn. NaN or negative dt adds no time.
void despawn_tick(MobManager& m, Vector3 player, float dt);
