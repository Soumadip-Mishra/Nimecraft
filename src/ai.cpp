#include "ai.hpp"

#include "block.hpp"
#include "light.hpp"
#include "pathfind.hpp"
#include "player.hpp"  // PLAYER_EYE
#include "world.hpp"

#include <algorithm>
#include <cmath>

namespace {

constexpr float SIGHT_STEP = 0.25f, PATH_ARRIVE = 0.35f, CHASE_STOP = 0.8f, REACH_EPS = 1e-4f;

float next01(uint32_t& s) {
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return static_cast<float>(s >> 8) / 16777216.f;
}

float hdist2(Vector3 a, Vector3 b) {
    const float dx = a.x - b.x, dz = a.z - b.z;
    return dx * dx + dz * dz;
}

float dist2(Vector3 a, Vector3 b) {
    const float dy = a.y - b.y;
    return hdist2(a, b) + dy * dy;
}

int cell(float v) {
    return static_cast<int>(std::floor(v + 1e-3f));
}

BlockPos cell_of(Vector3 p) {
    return BlockPos{cell(p.x), cell(p.y), cell(p.z)};
}

// Horizontal velocity of length `speed` along (dx, dz); zero when the direction is (near) zero.
Vector3 along(float dx, float dz, float speed) {
    const float len = std::sqrt(dx * dx + dz * dz);
    if (!(len > 1e-4f)) return Vector3{0.f, 0.f, 0.f};
    return Vector3{dx / len * speed, 0.f, dz / len * speed};
}

Vector3 away_from(const Mob& m, Vector3 from, float speed) {
    Vector3 v = along(m.pos.x - from.x, m.pos.z - from.z, speed);
    if (v.x == 0.f && v.z == 0.f) v = Vector3{std::cos(m.yaw) * speed, 0.f, std::sin(m.yaw) * speed};
    return v;
}

AiOutput output(Vector3 move, AiState next) {
    return AiOutput{move, false, false, false, next};
}

AiOutput start_flee(Mob& m, const AiContext& ctx, float speed) {
    m.timer = FLEE_S;
    m.path.clear();
    return output(away_from(m, ctx.player_pos, speed), AiState::Flee);
}

void plan(Mob& m, const AiContext& ctx, const MobDef& d) {
    PathParams p;
    p.height = std::max(1, static_cast<int>(std::ceil(d.height)));
    p.daylight = ctx.daylight;
    if (m.type == MobType::Gloomwalker) p.avoid_light_at_or_above = GLOOM_AVOID_LIGHT;
    const auto path = find_path(*ctx.world, cell_of(m.pos), cell_of(ctx.player_pos), p);
    m.path = path ? *path : std::vector<BlockPos>{};
    m.path_i = m.path.size() > 1 ? 1 : m.path.size();
    m.repath = REPATH_S;
}

// Toward the next path cell (jumping when it is higher), or straight at the player past the end.
AiOutput follow(Mob& m, const AiContext& ctx, float speed, AiState next) {
    while (m.path_i < m.path.size()) {
        const BlockPos c = m.path[m.path_i];
        if (hdist2(m.pos, Vector3{c.x + 0.5f, 0.f, c.z + 0.5f}) >= PATH_ARRIVE * PATH_ARRIVE) break;
        ++m.path_i;
    }
    AiOutput out = output(Vector3{}, next);
    if (m.path_i < m.path.size()) {
        const BlockPos c = m.path[m.path_i];
        out.move_dir = along(c.x + 0.5f - m.pos.x, c.z + 0.5f - m.pos.z, speed);
        out.jump = c.y > cell(m.pos.y);
    } else {
        out.move_dir = along(ctx.player_pos.x - m.pos.x, ctx.player_pos.z - m.pos.z, speed);
    }
    return out;
}

AiOutput chase(Mob& m, const AiContext& ctx, const MobDef& d, float d2) {
    if (m.state != AiState::Chase || m.repath <= 0.f) plan(m, ctx, d);
    AiOutput out = follow(m, ctx, d.speed, AiState::Chase);
    if (d2 <= CHASE_STOP * CHASE_STOP * d.reach * d.reach) out.move_dir = Vector3{0.f, 0.f, 0.f};
    if (d2 <= d.reach * d.reach + REACH_EPS && m.attack_cd <= 0.f) {
        out.attack = true;
        m.attack_cd = ATTACK_S;
    }
    return out;
}

AiOutput kite(Mob& m, const AiContext& ctx, const MobDef& d, float d2) {
    m.path.clear();
    AiOutput out = output(Vector3{}, AiState::Kite);
    if (d2 < KITE_MIN * KITE_MIN)
        out.move_dir = away_from(m, ctx.player_pos, d.speed);
    else if (d2 > KITE_MAX * KITE_MAX)
        out.move_dir = along(ctx.player_pos.x - m.pos.x, ctx.player_pos.z - m.pos.z, d.speed);
    if (m.shoot_cd <= 0.f) {
        out.shoot = true;
        m.shoot_cd = SHOOT_S;
    }
    return out;
}

AiOutput wander(Mob& m, const AiContext& ctx, const MobDef& d, float h) {
    m.path.clear();
    if (m.type == MobType::Tusker && ctx.mobs != nullptr) {
        const Mob* nearest = nullptr;
        float best = HERD_RANGE * HERD_RANGE;
        for (const Mob& o : *ctx.mobs) {
            if (o.id == m.id || o.type != MobType::Tusker) continue;
            const float d2 = dist2(m.pos, o.pos);
            if (d2 <= best && (nearest == nullptr || d2 < best)) {
                best = d2;
                nearest = &o;
            }
        }
        if (nearest != nullptr && best > HERD_CLOSE * HERD_CLOSE)
            return output(along(nearest->pos.x - m.pos.x, nearest->pos.z - m.pos.z, d.speed), AiState::Wander);
    }
    m.timer -= h;
    if (m.timer <= 0.f) {
        float dx = 0.f, dz = 0.f;
        do {
            dx = (next01(m.rng) * 2.f - 1.f) * WANDER_RADIUS;
            dz = (next01(m.rng) * 2.f - 1.f) * WANDER_RADIUS;
        } while (dx * dx + dz * dz > WANDER_RADIUS * WANDER_RADIUS);
        m.target = Vector3{m.pos.x + dx, m.pos.y, m.pos.z + dz};
        m.timer = WANDER_MIN_S + next01(m.rng) * (WANDER_MAX_S - WANDER_MIN_S);
    }
    if (hdist2(m.pos, m.target) <= WANDER_ARRIVE * WANDER_ARRIVE) return output(Vector3{}, AiState::Idle);
    return output(along(m.target.x - m.pos.x, m.target.z - m.pos.z, d.speed), AiState::Wander);
}

AiOutput decide(Mob& m, const AiContext& ctx, float h) {
    const MobDef& d = mob_def(m.type);
    if (m.type == MobType::Gloomwalker && ctx.world != nullptr && ctx.daylight > FADE_DAYLIGHT) {
        const BlockPos f = cell_of(m.pos);
        if (sky_light_at(*ctx.world, f.x, f.y, f.z) >= FADE_SKY_LIGHT) {
            m.health = std::max(0.f, m.health - FADE_DPS * h);
            m.path.clear();
            return output(Vector3{}, AiState::Fade);
        }
    }
    const float d2 = dist2(m.pos, ctx.player_pos);
    if (!d.hostile) {
        const bool hit = ctx.last_attacker_hit_mob && *ctx.last_attacker_hit_mob == m.id;
        if (hit && m.type == MobType::Tusker && !m.charged) {
            m.charged = true;
            m.timer = CHARGE_MAX_S;
            return output(along(ctx.player_pos.x - m.pos.x, ctx.player_pos.z - m.pos.z, CHARGE_SPEED),
                          AiState::Charge);
        }
        if (hit) return start_flee(m, ctx, d.speed);
        if (m.state == AiState::Charge) {
            m.timer -= h;
            if (d2 <= d.reach * d.reach + REACH_EPS) {
                AiOutput out = start_flee(m, ctx, d.speed);
                out.attack = true;
                return out;
            }
            if (m.timer <= 0.f) return start_flee(m, ctx, d.speed);
            return output(along(ctx.player_pos.x - m.pos.x, ctx.player_pos.z - m.pos.z, CHARGE_SPEED),
                          AiState::Charge);
        }
        if (m.state == AiState::Flee) {
            m.timer -= h;
            if (m.timer > 0.f) return output(away_from(m, ctx.player_pos, d.speed), AiState::Flee);
        }
        return wander(m, ctx, d, h);
    }
    if (ctx.player_alive && ctx.world != nullptr && d2 <= CHASE_RANGE * CHASE_RANGE &&
        line_of_sight(*ctx.world, mob_eye(m), Vector3{ctx.player_pos.x, ctx.player_pos.y + PLAYER_EYE, ctx.player_pos.z}))
        return m.type == MobType::Thornspitter ? kite(m, ctx, d, d2) : chase(m, ctx, d, d2);
    return wander(m, ctx, d, h);
}

}  // namespace

Vector3 mob_eye(const Mob& m) {
    return Vector3{m.pos.x, m.pos.y + mob_def(m.type).height * MOB_EYE, m.pos.z};
}

bool line_of_sight(const World& w, Vector3 a, Vector3 b) {
    const float len = std::sqrt(dist2(a, b));
    const int n = static_cast<int>(std::ceil(len / SIGHT_STEP));
    for (int i = 1; i < n; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(n);
        const int x = static_cast<int>(std::floor(a.x + (b.x - a.x) * t));
        const int y = static_cast<int>(std::floor(a.y + (b.y - a.y) * t));
        const int z = static_cast<int>(std::floor(a.z + (b.z - a.z) * t));
        if (block_def(w.get(x, y, z)).opaque) return false;
    }
    return true;
}

AiOutput ai_step(Mob& m, const AiContext& ctx, float dt) {
    const float h = (dt > 0.f && std::isfinite(dt)) ? dt : 0.f;
    m.attack_cd = std::max(0.f, m.attack_cd - h);
    m.shoot_cd = std::max(0.f, m.shoot_cd - h);
    m.repath = std::max(0.f, m.repath - h);
    const AiOutput out = decide(m, ctx, h);
    if (out.move_dir.x != 0.f || out.move_dir.z != 0.f) m.yaw = std::atan2(out.move_dir.z, out.move_dir.x);
    return out;
}
