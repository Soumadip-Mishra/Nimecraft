#include "combat.hpp"

#include <algorithm>

namespace {

constexpr float COOKED_SAT = 0.6f, RAW_SAT = 0.2f, ARMOR_STEP = 0.04f;
constexpr int MAX_ARMOR = 20;

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
