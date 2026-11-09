#include "combat.hpp"

#include "block.hpp"
#include "coords.hpp"
#include "physics.hpp"
#include "player.hpp"
#include "world.hpp"

#include <algorithm>
#include <cmath>

namespace {

constexpr float COOKED_SAT = 0.6f, RAW_SAT = 0.2f, ARMOR_STEP = 0.04f;
constexpr int MAX_ARMOR = 20;
constexpr float SILVER_FACTOR = 2.f;
constexpr float THORN_SUBSTEP = 1.f / 120.f, THORN_MAX_DT = 1.f;

uint32_t next(uint32_t& s) {
    if (s == 0u) s = 0x6D2B79F5u;
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s;
}

bool inside(const AABB& b, Vector3 p) {
    return p.x >= b.min.x && p.x <= b.max.x && p.y >= b.min.y && p.y <= b.max.y && p.z >= b.min.z && p.z <= b.max.z;
}

// Moves p one substep; false when it must be removed (block or unloaded column), hit set when it
// entered the player's box.
bool fly(Projectile& p, const World& w, const AABB& player, float h, bool& hit) {
    p.pos.x += p.vel.x * h;
    p.pos.y += p.vel.y * h - 0.5f * THORN_GRAVITY * h * h;
    p.pos.z += p.vel.z * h;
    p.vel.y -= THORN_GRAVITY * h;
    const int x = static_cast<int>(std::floor(p.pos.x)), y = static_cast<int>(std::floor(p.pos.y)),
              z = static_cast<int>(std::floor(p.pos.z));
    if (!w.column_loaded(column_of(chunk_of(x, 0, z))) || block_def(w.get(x, y, z)).solid) return false;
    hit = inside(player, p.pos);
    return !hit;
}

}  // namespace

void eat(Vitals& v, const ItemDef& food) {
    if (food.food == 0) return;
    const float f = static_cast<float>(food.food);
    v.hunger = std::min(MAX_HUNGER, v.hunger + f);
    v.saturation = std::min(v.hunger, v.saturation + f * (food.cooked ? COOKED_SAT : RAW_SAT));
}

bool eat_tick(EatState& e, bool use_held, ItemId held, const Vitals& v, float dt) {
    const bool can_eat = use_held && item_def(held).food != 0 && v.hunger < MAX_HUNGER;
    if (!can_eat || held != e.item) {
        e.t = 0.f;
        e.item = held;
    }
    if (!can_eat || !(dt > 0.f)) return false;
    e.t += dt;
    if (e.t < EAT_SECONDS) return false;
    e.t = 0.f;
    return true;
}

int armor_points(const Inventory& inv) {
    int sum = 0;
    for (const ItemStack& s : inv.armor)
        if (!s.empty()) sum += item_def(s.id).armor_points;
    return sum;
}

float apply_armor(float dmg, int armor) {
    return dmg * (1.f - static_cast<float>(std::clamp(armor, 0, MAX_ARMOR)) * ARMOR_STEP);
}

bool take_damage(Vitals& v, DamageState& d, float dmg, int armor) {
    if (d.invuln > 0.f || !(dmg > 0.f)) return false;
    v.health = std::max(0.f, v.health - apply_armor(dmg, armor));
    d.invuln = INVULN_SECONDS;
    return true;
}

void damage_tick(DamageState& d, float dt) {
    if (!(dt > 0.f)) return;
    d.invuln = std::max(0.f, d.invuln - dt);
}

float attack_damage(const ItemStack& held, const MobDef& target) {
    const float base = held.empty() ? 1.f : item_def(held.id).attack;
    return held.id == itm::SWORD_SILVER && !held.empty() && target.night_creature ? base * SILVER_FACTOR : base;
}

void apply_knockback(Vector3& vel, Vector3 target, Vector3 from, float strength) {
    const float dx = target.x - from.x, dz = target.z - from.z;
    const float len = std::sqrt(dx * dx + dz * dz);
    if (len > 1e-6f) {
        vel.x = dx / len * strength;
        vel.z = dz / len * strength;
    } else {
        vel.x = 0.f;
        vel.z = 0.f;
    }
    vel.y = KNOCKBACK_UP;
}

std::vector<ItemStack> roll_loot(const MobDef& d, uint32_t& rng) {
    std::vector<ItemStack> out;
    for (const LootEntry& e : d.loot) {
        const float roll = static_cast<float>(next(rng) >> 8) / 16777216.f;
        if (!(roll < e.chance)) continue;
        const uint32_t span = static_cast<uint32_t>(e.max - e.min) + 1u;
        const int count = e.min + static_cast<int>(next(rng) % span);
        if (count > 0) out.push_back(ItemStack{e.item, static_cast<uint8_t>(count), 0});
    }
    return out;
}

Projectile make_thorn(Vector3 from, Vector3 to, float damage) {
    const float dx = to.x - from.x, dy = to.y - from.y, dz = to.z - from.z;
    const float len = std::sqrt(dx * dx + dy * dy + dz * dz);
    const Vector3 vel = len > 1e-6f ? Vector3{dx / len * THORN_SPEED, dy / len * THORN_SPEED, dz / len * THORN_SPEED}
                                    : Vector3{THORN_SPEED, 0.f, 0.f};
    return Projectile{from, vel, damage, 0.f};
}

int update_projectiles(std::vector<Projectile>& ps, const World& w, Vector3 player_feet, Vitals& v, DamageState& d,
                       int armor, float dt) {
    if (!(dt > 0.f) || !std::isfinite(dt)) return 0;
    dt = std::min(dt, THORN_MAX_DT);
    const int n = std::max(1, static_cast<int>(std::ceil(dt / THORN_SUBSTEP - 1e-3f)));
    const float h = dt / static_cast<float>(n);
    const AABB player = aabb_at(player_feet, Vector3{PLAYER_HALF_W, 0.f, PLAYER_HALF_W}, PLAYER_H);
    int hits = 0;
    std::erase_if(ps, [&](Projectile& p) {
        p.age += dt;
        for (int s = 0; s < n; ++s) {
            bool hit = false;
            if (fly(p, w, player, h, hit)) continue;
            if (hit) {
                take_damage(v, d, p.damage, armor);
                ++hits;
            }
            return true;
        }
        return p.age >= THORN_LIFE;
    });
    return hits;
}
