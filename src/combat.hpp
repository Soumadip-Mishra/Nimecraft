// Eating, armor, taking damage with a short invulnerability window, melee damage against
// creatures, knockback, loot, and thorn projectiles. Pure logic (raylib for Vector3).
#pragma once

#include "inventory.hpp"
#include "items.hpp"
#include "mobs.hpp"
#include "vitals.hpp"

#include "raylib.h"  // Vector3 only

#include <cstdint>
#include <vector>

class World;

constexpr float EAT_SECONDS = 1.6f, INVULN_SECONDS = 0.5f;

// hunger += food (max 20); saturation += food * (cooked ? 0.6 : 0.2), then capped at the new
// hunger. An item with food 0 changes nothing.
void eat(Vitals& v, const ItemDef& food);

// Holding "use" with a food item while hunger < 20 fills t; when t reaches EAT_SECONDS the item is
// eaten (returns true; the caller calls eat and removes one item) and t restarts. Letting go,
// switching items, holding a non-food, or a full hunger bar resets t; a NaN or non-positive dt
// leaves it. One call eats at most one item.
struct EatState {
    float t = 0;
    ItemId item = 0;
};
bool eat_tick(EatState& e, bool use_held, ItemId held, const Vitals& v, float dt);

// Sum of armor_points of the items in the 4 armor slots (non-armor items count 0).
int armor_points(const Inventory& inv);
// dmg * (1 - clamp(armor, 0, 20) * 0.04).
float apply_armor(float dmg, int armor);

struct DamageState {
    float invuln = 0;
};
// Ignored (false) while invuln > 0, or when dmg is not positive (or NaN). Otherwise health drops by
// apply_armor(dmg, armor), clamped at 0, invuln becomes INVULN_SECONDS, and it returns true.
bool take_damage(Vitals& v, DamageState& d, float dmg, int armor);
void damage_tick(DamageState& d, float dt);  // invuln -= dt, not below 0; NaN or negative dt: no change

// The held item's attack (an empty hand 1), doubled for SWORD_SILVER against a night creature.
float attack_damage(const ItemStack& held, const MobDef& target);

constexpr float KNOCKBACK_UP = 5.f;
// Pushes something at `target` away from `from`: the horizontal velocity becomes `strength` along
// the horizontal direction from -> target (zero when they share x and z) and vel.y becomes
// KNOCKBACK_UP. Never NaN.
void apply_knockback(Vector3& vel, Vector3 target, Vector3 from, float strength = 6.f);

// Each loot entry, with its chance, gives min..max (uniform) of its item; entries that come out 0
// give nothing. One stack per entry, in table order. rng advances (xorshift32; 0 is replaced).
std::vector<ItemStack> roll_loot(const MobDef& d, uint32_t& rng);

constexpr float THORN_SPEED = 18.f, THORN_GRAVITY = 10.f, THORN_LIFE = 5.f;

struct Projectile {
    Vector3 pos, vel;
    float damage;
    float age;
};
// A thorn at `from` flying straight at `to` at THORN_SPEED (along +X when they coincide).
Projectile make_thorn(Vector3 from, Vector3 to, float damage);

// Zero, negative, or NaN dt does nothing; dt above 1 counts as 1. Every projectile ages by dt and
// flies under THORN_GRAVITY in equal substeps of at most 1/120 s (exact within a substep, so a
// long dt equals many short ones). It is removed when its position enters a solid block or an
// unloaded column, when it reaches THORN_LIFE, or when it enters the player's box (feet center
// player_feet, PLAYER_HALF_W x PLAYER_H): that hit calls take_damage(v, d, damage, armor).
// Returns the number of hits.
int update_projectiles(std::vector<Projectile>& ps, const World& w, Vector3 player_feet, Vitals& v, DamageState& d,
                       int armor, float dt);
