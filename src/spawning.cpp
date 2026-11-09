#include "spawning.hpp"

#include "block.hpp"
#include "light.hpp"
#include "pathfind.hpp"  // cell_light
#include "terrain.hpp"
#include "world.hpp"

#include <algorithm>
#include <cmath>

namespace {

constexpr MobType PASSIVE[] = {MobType::Tusker, MobType::Fluffhorn, MobType::Hopper};
constexpr MobType HOSTILE[] = {MobType::Gloomwalker, MobType::Thornspitter, MobType::CaveCrawler};
constexpr int HOSTILE_Y_RANGE = 32, FLOOR_SEARCH = 24, GROUP_SPREAD = 2;

uint32_t next(uint32_t& s) {
    if (s == 0u) s = 0x6D2B79F5u;
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s;
}

int pick(uint32_t& s, int n) {
    return static_cast<int>(next(s) % static_cast<uint32_t>(n));
}

bool solid(const World& w, int x, int y, int z) {
    return block_def(w.get(x, y, z)).solid;
}

bool tree(BlockId b) {
    return b == blk::LOG || b == blk::LEAVES || b == blk::PINE_LOG || b == blk::PINE_LEAVES;
}

Vector3 center(BlockPos p) {
    return Vector3{static_cast<float>(p.x) + 0.5f, static_cast<float>(p.y), static_cast<float>(p.z) + 0.5f};
}

float hdist2(Vector3 a, Vector3 b) {
    const float dx = a.x - b.x, dz = a.z - b.z;
    return dx * dx + dz * dz;
}

bool in_ring(Vector3 p, Vector3 player) {
    return hdist2(p, player) <= SPAWN_MAX_DIST * SPAWN_MAX_DIST;  // the near edge is can_spawn's
}

int count(const MobManager& m, bool hostile) {
    int n = 0;
    for (const Mob& mob : m.all()) n += mob_def(mob.type).hostile == hostile ? 1 : 0;
    return n;
}

// A column 24-64 blocks from the player, horizontally.
void ring_column(uint32_t& rng, Vector3 player, int& x, int& z) {
    for (;;) {
        const float dx = static_cast<float>(pick(rng, 129) - 64), dz = static_cast<float>(pick(rng, 129) - 64);
        const float d2 = dx * dx + dz * dz;
        if (d2 < SPAWN_MIN_DIST * SPAWN_MIN_DIST || d2 > SPAWN_MAX_DIST * SPAWN_MAX_DIST) continue;
        x = static_cast<int>(std::floor(player.x + dx));
        z = static_cast<int>(std::floor(player.z + dz));
        if (in_ring(center(BlockPos{x, 0, z}), player)) return;
    }
}

// Feet on the highest ground at (x, z) that is not part of a tree; -1 when there is none.
int surface_feet(const World& w, int x, int z) {
    for (int y = WORLD_HEIGHT - 1; y >= 0; --y) {
        const BlockId b = w.get(x, y, z);
        if (block_def(b).solid && !tree(b)) return y + 1;
    }
    return -1;
}

// The first feet cell (not solid, solid below) from y0 down FLOOR_SEARCH cells; -1 when none.
int floor_feet(const World& w, int x, int y0, int z) {
    for (int y = y0; y > y0 - FLOOR_SEARCH && y > 0; --y)
        if (!solid(w, x, y, z) && solid(w, x, y - 1, z)) return y;
    return -1;
}

void try_passive(MobManager& m, const World& w, Vector3 player, float day_light, uint32_t& rng, int room) {
    const MobType t = PASSIVE[pick(rng, 3)];
    int x = 0, z = 0;
    ring_column(rng, player, x, z);
    if (!w.column_loaded(column_of(chunk_of(x, 0, z)))) return;
    const BlockPos f{x, surface_feet(w, x, z), z};
    if (!can_spawn(t, w, f, day_light, player)) return;
    m.spawn(t, center(f), next(rng));
    const int extra = t == MobType::Tusker ? 3 : (t == MobType::Fluffhorn ? 2 : 1);
    for (int i = 0, made = 1; i < extra && made < room; ++i) {
        const int gx = x + pick(rng, 2 * GROUP_SPREAD + 1) - GROUP_SPREAD;
        const int gz = z + pick(rng, 2 * GROUP_SPREAD + 1) - GROUP_SPREAD;
        if (!w.column_loaded(column_of(chunk_of(gx, 0, gz)))) continue;
        const BlockPos g{gx, surface_feet(w, gx, gz), gz};
        if (!in_ring(center(g), player) || !can_spawn(t, w, g, day_light, player)) continue;
        m.spawn(t, center(g), next(rng));
        ++made;
    }
}

void try_hostile(MobManager& m, const World& w, Vector3 player, float day_light, uint32_t& rng) {
    const MobType t = HOSTILE[pick(rng, 3)];
    int x = 0, z = 0;
    ring_column(rng, player, x, z);
    const int py = static_cast<int>(std::floor(player.y));
    const int y0 = std::clamp(py - HOSTILE_Y_RANGE + pick(rng, 2 * HOSTILE_Y_RANGE + 1), 1, WORLD_HEIGHT - 1);
    if (!w.column_loaded(column_of(chunk_of(x, 0, z)))) return;
    const BlockPos f{x, floor_feet(w, x, y0, z), z};
    if (!can_spawn(t, w, f, day_light, player)) return;
    m.spawn(t, center(f), next(rng));
}

}  // namespace

bool can_spawn(MobType t, const World& w, BlockPos feet, float day_light, Vector3 player) {
    const MobDef& d = mob_def(t);
    const int body = std::max(1, static_cast<int>(std::ceil(d.height)));
    if (feet.y < 1 || feet.y + body > WORLD_HEIGHT) return false;
    if (!w.column_loaded(column_of(chunk_of(feet.x, 0, feet.z)))) return false;
    const BlockId ground = w.get(feet.x, feet.y - 1, feet.z);
    if (!block_def(ground).solid) return false;
    for (int k = 0; k < body; ++k) {
        const BlockId b = w.get(feet.x, feet.y + k, feet.z);
        const BlockDef& bd = block_def(b);
        if (bd.solid || b == blk::WATER || bd.emission > 0) return false;
    }
    const Vector3 c = center(feet);
    const float dy = c.y - player.y;
    if (hdist2(c, player) + dy * dy < SPAWN_MIN_DIST * SPAWN_MIN_DIST) return false;
    if (!d.hostile) {
        const Biome b = biome_at(w.seed(), feet.x, feet.z);
        if (std::find(d.biomes.begin(), d.biomes.end(), b) == d.biomes.end()) return false;
        if (ground != blk::GRASS && ground != blk::SNOW && ground != blk::SAND) return false;
        return sky_light_at(w, feet.x, feet.y, feet.z) >= PASSIVE_MIN_SKY;
    }
    if (cell_light(w, feet, day_light) > HOSTILE_MAX_LIGHT) return false;
    if (t == MobType::Thornspitter)
        return day_light < NIGHT_DAYLIGHT && biome_at(w.seed(), feet.x, feet.z) == Biome::Forest;
    if (t == MobType::CaveCrawler) return feet.y < CRAWLER_MAX_Y;
    return true;
}

void spawn_tick(MobManager& m, const World& w, Vector3 player, float day_light, uint32_t& rng, SpawnCaps caps) {
    if (!std::isfinite(player.x) || !std::isfinite(player.y) || !std::isfinite(player.z)) return;
    const int passive = count(m, false), hostile = count(m, true);
    if (passive < caps.passive) try_passive(m, w, player, day_light, rng, caps.passive - passive);
    if (hostile < caps.hostile) try_hostile(m, w, player, day_light, rng);
}

void despawn_tick(MobManager& m, Vector3 player, float dt) {
    const float h = dt > 0.f ? dt : 0.f;
    std::vector<uint32_t> gone;
    for (Mob& mob : m.all()) {
        if (!mob_def(mob.type).hostile) continue;
        const float dy = mob.pos.y - player.y;
        const float d2 = hdist2(mob.pos, player) + dy * dy;
        if (d2 > DESPAWN_FAR * DESPAWN_FAR) {
            gone.push_back(mob.id);
        } else if (d2 > DESPAWN_LINGER * DESPAWN_LINGER) {
            mob.far_t += h;
            if (mob.far_t >= DESPAWN_LINGER_S) gone.push_back(mob.id);
        } else {
            mob.far_t = 0.f;
        }
    }
    for (uint32_t id : gone) m.remove(id);
}
