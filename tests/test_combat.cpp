// Tests for src/combat.*: eating, armor points and reduction, damage with invulnerability.
#include "block.hpp"
#include "combat.hpp"
#include "inventory.hpp"
#include "items.hpp"
#include "test.hpp"
#include "vitals.hpp"

#include <limits>

namespace {

Vitals fed(float hunger, float saturation) {
    Vitals v;
    v.hunger = hunger;
    v.saturation = saturation;
    return v;
}

Inventory wearing(ItemId head, ItemId chest, ItemId legs, ItemId feet) {
    Inventory inv;
    const ItemId ids[4] = {head, chest, legs, feet};
    for (int i = 0; i < 4; ++i)
        if (ids[i] != 0) inv.armor[static_cast<std::size_t>(i)] = ItemStack{ids[i], 1, 0};
    return inv;
}

ItemStack one(ItemId id) {
    return ItemStack{id, 1, 0};
}

}  // namespace

TEST(test_eat_cooked_vs_raw) {
    Vitals c = fed(10.f, 0.f);
    eat(c, item_def(itm::COOKED_MEAT));
    CHECK(c.hunger == 18.f);
    CHECK_NEAR(c.saturation, 4.8, 1e-5);
    Vitals r = fed(10.f, 0.f);
    eat(r, item_def(itm::RAW_MEAT));
    CHECK(r.hunger == 13.f);
    CHECK_NEAR(r.saturation, 0.6, 1e-5);
    // Saturation adds to what is already there.
    Vitals s = fed(10.f, 2.f);
    eat(s, item_def(itm::COOKED_SMALL_MEAT));
    CHECK(s.hunger == 15.f);
    CHECK_NEAR(s.saturation, 5.0, 1e-5);
}

TEST(test_eat_caps) {
    Vitals v = fed(18.f, 0.f);
    eat(v, item_def(itm::COOKED_MEAT));
    CHECK(v.hunger == 20.f);
    CHECK_NEAR(v.saturation, 4.8, 1e-5);
    Vitals full = fed(19.f, 19.f);
    eat(full, item_def(itm::COOKED_MEAT));
    CHECK(full.hunger == 20.f);
    CHECK(full.saturation == 20.f);
    // Saturation is capped at the hunger value after eating.
    Vitals low = fed(0.f, 0.f);
    eat(low, item_def(itm::SMALL_MEAT));
    CHECK(low.hunger == 2.f);
    CHECK_NEAR(low.saturation, 0.4, 1e-5);
    Vitals over = fed(2.f, 3.f);
    eat(over, item_def(itm::SMALL_MEAT));
    CHECK(over.hunger == 4.f);
    CHECK_NEAR(over.saturation, 3.4, 1e-5);
    Vitals capped = fed(1.f, 1.f);
    capped.saturation = 4.f;
    eat(capped, item_def(itm::SMALL_MEAT));
    CHECK(capped.hunger == 3.f);
    CHECK(capped.saturation == 3.f);
}

TEST(test_eat_non_food) {
    for (ItemId id : {itm::STICK, itm::PICK_IRON, static_cast<ItemId>(blk::DIRT), static_cast<ItemId>(0),
                      static_cast<ItemId>(9999)}) {
        Vitals v = fed(10.f, 1.f);
        eat(v, item_def(id));
        CHECK(v.hunger == 10.f);
        CHECK(v.saturation == 1.f);
    }
}

TEST(test_eat_timer) {
    const Vitals hungry = fed(10.f, 0.f);
    EatState e;
    CHECK(!eat_tick(e, true, itm::COOKED_MEAT, hungry, 1.59f));
    CHECK(eat_tick(e, true, itm::COOKED_MEAT, hungry, 0.01f));
    CHECK(e.t == 0.f);  // the next bite starts over
    EatState once;
    CHECK(eat_tick(once, true, itm::COOKED_MEAT, hungry, EAT_SECONDS));
    // One long frame eats at most one item.
    EatState big;
    CHECK(eat_tick(big, true, itm::RAW_MEAT, hungry, 10.f));
    CHECK(big.t == 0.f);
    // Letting go restarts.
    EatState rel;
    eat_tick(rel, true, itm::COOKED_MEAT, hungry, 1.f);
    CHECK(!eat_tick(rel, false, itm::COOKED_MEAT, hungry, 0.1f));
    CHECK(rel.t == 0.f);
    CHECK(!eat_tick(rel, true, itm::COOKED_MEAT, hungry, 1.f));
    // Switching to other food restarts.
    EatState sw;
    eat_tick(sw, true, itm::COOKED_MEAT, hungry, 1.f);
    CHECK(!eat_tick(sw, true, itm::RAW_MEAT, hungry, 1.f));
    CHECK(eat_tick(sw, true, itm::RAW_MEAT, hungry, 0.6f));
    // Non-food and a full hunger bar never eat.
    EatState nf;
    CHECK(!eat_tick(nf, true, itm::STICK, hungry, 5.f));
    CHECK(nf.t == 0.f);
    EatState full;
    CHECK(!eat_tick(full, true, itm::COOKED_MEAT, fed(20.f, 5.f), 5.f));
    CHECK(full.t == 0.f);
    CHECK(eat_tick(full, true, itm::COOKED_MEAT, fed(19.5f, 5.f), 5.f));
    // NaN or negative dt does not advance.
    EatState bad;
    CHECK(!eat_tick(bad, true, itm::COOKED_MEAT, hungry, std::numeric_limits<float>::quiet_NaN()));
    CHECK(!eat_tick(bad, true, itm::COOKED_MEAT, hungry, -5.f));
    CHECK(bad.t == 0.f);
}

TEST(test_armor_points) {
    CHECK(armor_points(wearing(itm::IRON_HELMET, itm::IRON_CHEST, itm::IRON_LEGS, itm::IRON_BOOTS)) == 15);
    CHECK(armor_points(wearing(itm::HIDE_HELMET, itm::HIDE_CHEST, itm::HIDE_LEGS, itm::HIDE_BOOTS)) == 7);
    CHECK(armor_points(wearing(0, itm::IRON_CHEST, 0, itm::HIDE_BOOTS)) == 7);
    CHECK(armor_points(Inventory{}) == 0);
    // Armor carried in the inventory, or a non-armor item in an armor slot, gives nothing.
    Inventory carried;
    carried.slots[0] = one(itm::IRON_CHEST);
    carried.armor[0] = one(itm::STICK);
    CHECK(armor_points(carried) == 0);
}

TEST(test_armor_reduction) {
    CHECK(apply_armor(10.f, 0) == 10.f);
    CHECK_NEAR(apply_armor(10.f, 15), 4.0, 1e-5);
    CHECK_NEAR(apply_armor(10.f, 20), 2.0, 1e-5);
    CHECK_NEAR(apply_armor(10.f, 25), 2.0, 1e-5);
    CHECK_NEAR(apply_armor(10.f, 19), 2.4, 1e-5);
    CHECK(apply_armor(10.f, -5) == 10.f);  // negative armor counts as none
}

TEST(test_invulnerability_window) {
    Vitals v;
    DamageState d;
    CHECK(take_damage(v, d, 3.f, 0));
    CHECK(v.health == 17.f);
    CHECK(d.invuln == 0.5f);
    DamageState early = d;
    Vitals ve = v;
    damage_tick(early, 0.49f);
    CHECK(!take_damage(ve, early, 3.f, 0));
    CHECK(ve.health == 17.f);
    damage_tick(d, 0.5f);
    CHECK(d.invuln == 0.f);
    CHECK(take_damage(v, d, 3.f, 0));
    CHECK(v.health == 14.f);
    // Armor reduces the damage that is applied.
    Vitals a;
    DamageState da;
    CHECK(take_damage(a, da, 10.f, 15));
    CHECK_NEAR(a.health, 16.0, 1e-5);
    // The timer never goes below 0 and ignores NaN.
    DamageState t;
    damage_tick(t, 3.f);
    CHECK(t.invuln == 0.f);
    t.invuln = 0.3f;
    damage_tick(t, std::numeric_limits<float>::quiet_NaN());
    CHECK(t.invuln == 0.3f);
    damage_tick(t, -1.f);
    CHECK(t.invuln == 0.3f);
}

TEST(test_damage_non_positive) {
    for (float dmg : {0.f, -3.f, std::numeric_limits<float>::quiet_NaN()}) {
        Vitals v;
        DamageState d;
        CHECK(!take_damage(v, d, dmg, 0));
        CHECK(v.health == 20.f);
        CHECK(d.invuln == 0.f);
    }
}

TEST(test_damage_clamps_zero) {
    Vitals v;
    v.health = 5.f;
    DamageState d;
    CHECK(take_damage(v, d, 50.f, 0));
    CHECK(v.health == 0.f);
}
