#include "mobs.hpp"

#include "ai.hpp"
#include "combat.hpp"
#include "coords.hpp"
#include "physics.hpp"
#include "world.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace {

// clang-format off
const std::array<MobDef, static_cast<std::size_t>(MobType::COUNT)> DEFS = {{
    {"Tusker",       0.45f, 0.9f, 10.f, 3.f,  3.f, 1.2f, false, false, {Biome::Forest, Biome::Meadow},
                     {{itm::RAW_MEAT, 1, 3, 1.f}, {itm::HIDE, 0, 2, 1.f}}},
    {"Fluffhorn",    0.45f, 1.3f, 8.f,  2.5f, 0.f, 1.2f, false, false, {Biome::Highlands, Biome::Snowfield},
                     {{itm::WOOL, 1, 2, 1.f}, {itm::RAW_MEAT, 1, 2, 1.f}}},
    {"Hopper",       0.2f,  0.5f, 3.f,  5.f,  0.f, 1.2f, false, false, {Biome::Meadow, Biome::Desert},
                     {{itm::SMALL_MEAT, 1, 1, 1.f}}},
    {"Gloomwalker",  0.3f,  1.9f, 20.f, 3.5f, 4.f, 1.2f, true,  true,  {},
                     {{itm::GLOOM_DUST, 0, 2, 1.f}}},
    {"Thornspitter", 0.35f, 1.2f, 14.f, 2.5f, 3.f, 1.2f, true,  true,  {Biome::Forest},
                     {{itm::FIBER, 1, 3, 1.f}}},
    {"Cave Crawler", 0.3f,  0.5f, 6.f,  5.f,  2.f, 1.2f, true,  false, {},
                     {{itm::SILK, 0, 2, 1.f}}},
}};
// clang-format on

constexpr uint32_t ZERO_SEED = 0x6D2B79F5u;

ColumnPos column_at(Vector3 p) {
    return column_of(chunk_of(static_cast<int>(std::floor(p.x)), 0, static_cast<int>(std::floor(p.z))));
}

bool loaded(const World& w, Vector3 p) {
    return w.column_loaded(column_at(p));
}

// One step of physics for a mob in a loaded column.
void step_mob(Mob& m, const World& w, float h) {
    const MobDef& d = mob_def(m.type);
    const Vector3 before = m.pos;
    m.vel.y = std::max(m.vel.y - MOB_GRAVITY * h, -MOB_MAX_FALL);
    const MoveResult r = move_aabb(w, m.pos, m.vel, Vector3{d.half_w, 0.f, d.half_w}, d.height, h);
    m.pos = r.pos;
    m.vel = r.vel;
    if (!loaded(w, m.pos)) {
        // World::get reads an unloaded column as AIR: never step into one (it would freeze there,
        // out of reach of the column saves). Keep the fall, undo the sideways move.
        m.pos.x = before.x;
        m.pos.z = before.z;
        m.vel.x = 0.f;
        m.vel.z = 0.f;  // not counted as blocked: jumping cannot get past an unloaded edge
    }
    const float dx = m.pos.x - before.x, dz = m.pos.z - before.z;
    m.walk += std::sqrt(dx * dx + dz * dz);
    m.on_ground = r.on_ground;
    m.blocked = r.hit_x || r.hit_z;
    m.invuln = std::max(0.f, m.invuln - h);
    m.hurt = std::max(0.f, m.hurt - h);
    m.knock = std::max(0.f, m.knock - h);
}

// Ray entry distance into [mn, mx] along one axis, narrowing [t0, t1]; false when it misses.
bool slab(float o, float d, float mn, float mx, float& t0, float& t1) {
    if (d == 0.f) return o >= mn && o <= mx;
    float a = (mn - o) / d, b = (mx - o) / d;
    if (a > b) std::swap(a, b);
    t0 = std::max(t0, a);
    t1 = std::min(t1, b);
    return t0 <= t1;
}

}  // namespace

const MobDef& mob_def(MobType t) {
    const auto i = static_cast<std::size_t>(t);
    return i < DEFS.size() ? DEFS[i] : DEFS[0];
}

uint32_t MobManager::spawn(MobType t, Vector3 pos, uint32_t seed) {
    Mob m{};
    m.id = next_id_++;
    m.type = t;
    m.pos = pos;
    m.vel = Vector3{0.f, 0.f, 0.f};
    m.health = mob_def(t).health;
    m.rng = seed != 0u ? seed : ZERO_SEED;
    mobs_.push_back(std::move(m));
    return mobs_.back().id;
}

void MobManager::update(const World& w, const AiContext& ctx, float dt) {
    if (!(dt > 0.f) || !std::isfinite(dt)) return;
    dt = std::min(dt, MOB_MAX_DT);
    const int n = std::max(1, static_cast<int>(std::ceil(dt / MOB_STEP - 1e-3f)));
    const float h = dt / static_cast<float>(n);
    AiContext c = ctx;
    c.mobs = &mobs_;
    for (int s = 0; s < n; ++s) {
        if (s == 1) c.last_attacker_hit_mob.reset();  // a hit counts once
        for (Mob& m : mobs_) {
            if (!loaded(w, m.pos)) {
                m.vel.y = 0.f;  // frozen until its column streams in
                continue;
            }
            const AiOutput o = ai_step(m, c, h);
            m.state = o.next;
            if (m.knock <= 0.f) {
                m.vel.x = o.move_dir.x;
                m.vel.z = o.move_dir.z;
            }
            const bool walking = o.move_dir.x != 0.f || o.move_dir.z != 0.f;
            if (m.on_ground && (o.jump || (walking && m.blocked))) m.vel.y = MOB_JUMP_VEL;
            if (o.attack) events_.push_back(MobEvent{MobEvent::Attack, m.id, mob_eye(m)});
            if (o.shoot) events_.push_back(MobEvent{MobEvent::Shoot, m.id, mob_eye(m)});
            step_mob(m, w, h);
        }
    }
}

std::vector<MobEvent> MobManager::take_events() {
    std::vector<MobEvent> out;
    out.swap(events_);
    return out;
}

void MobManager::remove(uint32_t id) {
    std::erase_if(mobs_, [id](const Mob& m) { return m.id == id; });
}

std::optional<uint32_t> MobManager::raycast(Vector3 origin, Vector3 dir, float max_dist) const {
    const float len = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
    if (!std::isfinite(max_dist) || !(len > 0.f)) return std::nullopt;
    const Vector3 u{dir.x / len, dir.y / len, dir.z / len};
    std::optional<uint32_t> best;
    float best_t = max_dist;
    for (const Mob& m : mobs_) {
        const MobDef& d = mob_def(m.type);
        float t0 = 0.f, t1 = max_dist;
        if (!slab(origin.x, u.x, m.pos.x - d.half_w, m.pos.x + d.half_w, t0, t1)) continue;
        if (!slab(origin.y, u.y, m.pos.y, m.pos.y + d.height, t0, t1)) continue;
        if (!slab(origin.z, u.z, m.pos.z - d.half_w, m.pos.z + d.half_w, t0, t1)) continue;
        if (t0 > best_t || (best && t0 == best_t)) continue;
        best_t = t0;
        best = m.id;
    }
    return best;
}

std::vector<Mob>& MobManager::all() {
    return mobs_;
}

const std::vector<Mob>& MobManager::all() const {
    return mobs_;
}

bool MobManager::damage(uint32_t id, float dmg, Vector3 from) {
    Mob* m = find(id);
    if (m == nullptr || m->invuln > 0.f || !(dmg > 0.f)) return false;
    m->health = std::max(0.f, m->health - dmg);
    m->invuln = MOB_INVULN_S;
    m->hurt = MOB_HURT_S;
    m->knock = MOB_KNOCK_S;
    apply_knockback(m->vel, m->pos, from);
    return true;
}

std::vector<Mob> MobManager::take_dead() {
    std::vector<Mob> dead;
    for (Mob& m : mobs_)
        if (m.health <= 0.f) dead.push_back(std::move(m));
    std::erase_if(mobs_, [](const Mob& m) { return m.health <= 0.f; });
    return dead;
}

std::vector<SavedMob> MobManager::saved_in(ColumnPos c) const {
    std::vector<SavedMob> out;
    for (const Mob& m : mobs_)
        if (!mob_def(m.type).hostile && column_at(m.pos) == c)
            out.push_back(SavedMob{static_cast<uint8_t>(m.type), m.pos.x, m.pos.y, m.pos.z, m.yaw, m.health});
    return out;
}

std::vector<SavedMob> MobManager::take_column(ColumnPos c) {
    std::vector<SavedMob> out = saved_in(c);
    std::erase_if(mobs_, [c](const Mob& m) { return column_at(m.pos) == c; });
    return out;
}

void MobManager::restore(const std::vector<SavedMob>& saved, uint32_t& rng) {
    for (const SavedMob& s : saved) {
        if (s.type >= static_cast<uint8_t>(MobType::COUNT) || !(s.health > 0.f)) continue;
        const auto t = static_cast<MobType>(s.type);
        if (mob_def(t).hostile) continue;
        if (rng == 0u) rng = ZERO_SEED;
        rng ^= rng << 13;
        rng ^= rng >> 17;
        rng ^= rng << 5;
        spawn(t, Vector3{s.x, s.y, s.z}, rng);
        mobs_.back().yaw = s.yaw;
        mobs_.back().health = std::min(s.health, mob_def(t).health);
    }
}

void stash_mobs(Column& col, const std::vector<SavedMob>& mobs) {
    for (const SavedMob& m : mobs) {
        const float y = std::isfinite(m.y) ? std::clamp(m.y, 0.f, static_cast<float>(WORLD_HEIGHT - 1)) : 0.f;
        Chunk& ch = *col[static_cast<std::size_t>(static_cast<int>(y) / CHUNK)];
        ch.mobs.push_back(m);
        ch.modified = true;
    }
}

bool hostiles_within(const MobManager& m, Vector3 p, float r) {
    for (const Mob& mob : m.all()) {
        if (!mob_def(mob.type).hostile) continue;
        const float dx = mob.pos.x - p.x, dy = mob.pos.y - p.y, dz = mob.pos.z - p.z;
        if (dx * dx + dy * dy + dz * dz <= r * r) return true;
    }
    return false;
}

Mob* MobManager::find(uint32_t id) {
    for (Mob& m : mobs_)
        if (m.id == id) return &m;
    return nullptr;
}
