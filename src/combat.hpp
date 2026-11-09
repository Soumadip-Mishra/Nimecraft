// Eating, armor, and taking damage with a short invulnerability window. Pure logic.
#pragma once

#include "inventory.hpp"
#include "items.hpp"
#include "vitals.hpp"

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
