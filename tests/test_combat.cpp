// Tests for src/combat.*: eating, armor points and reduction, damage with invulnerability, melee
// damage against creatures, knockback, loot, and thorn projectiles.
#include "block.hpp"
#include "chunk.hpp"
#include "combat.hpp"
#include "coords.hpp"
#include "inventory.hpp"
#include "items.hpp"
#include "mobs.hpp"
#include "test.hpp"
#include "vitals.hpp"
#include "world.hpp"

#include <cmath>
#include <cstddef>
#include <limits>
#include <map>
#include <memory>
#include <vector>

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

namespace {

// Columns (-3..2, -3..2) with STONE at y = 0..69: the floor top is y = 70, x and z in [-48, 48).
World stone_floor() {
    World w(1);
    for (int cx = -3; cx <= 2; ++cx)
        for (int cz = -3; cz <= 2; ++cz) {
            Column col;
            for (std::size_t i = 0; i < col.size(); ++i) col[i] = std::make_unique<Chunk>();
            for (int y = 0; y < 70; ++y)
                for (int lz = 0; lz < CHUNK; ++lz)
                    for (int lx = 0; lx < CHUNK; ++lx)
                        col[static_cast<std::size_t>(y / CHUNK)]->blocks[static_cast<std::size_t>(
                            Chunk::index(lx, y % CHUNK, lz))] = blk::STONE;
            w.insert_column(ColumnPos{cx, cz}, std::move(col));
        }
    return w;
}

constexpr MobType EVERY_TYPE[] = {MobType::Tusker,      MobType::Fluffhorn,    MobType::Hopper,
                                  MobType::Gloomwalker, MobType::Thornspitter, MobType::CaveCrawler};

}  // namespace

TEST(test_silver_bonus) {
    const MobDef& gloom = mob_def(MobType::Gloomwalker);
    const MobDef& tusker = mob_def(MobType::Tusker);
    CHECK(attack_damage(one(itm::SWORD_SILVER), gloom) == 11.f);
    CHECK(attack_damage(one(itm::SWORD_SILVER), tusker) == 5.5f);
    CHECK(attack_damage(one(itm::SWORD_SILVER), mob_def(MobType::Thornspitter)) == 11.f);  // night creature
    CHECK(attack_damage(one(itm::SWORD_SILVER), mob_def(MobType::CaveCrawler)) == 5.5f);   // not one
    CHECK(attack_damage(one(itm::SWORD_IRON), gloom) == 7.f);
    CHECK(attack_damage(ItemStack{}, gloom) == 1.f);
    CHECK(attack_damage(ItemStack{}, tusker) == 1.f);
    CHECK(attack_damage(one(blk::DIRT), tusker) == 1.f);       // a non-tool hits like a hand
    CHECK(attack_damage(one(itm::PICK_IRON), tusker) < 7.f);  // other tools hit softer than swords
}

TEST(test_knockback_direction) {
    Vector3 v{0.f, 0.f, 0.f};
    apply_knockback(v, Vector3{5.f, 70.f, 0.f}, Vector3{2.f, 70.f, 0.f});  // target at +X of the attacker
    CHECK(v.x > 0.f);
    CHECK(v.y > 0.f);
    CHECK_NEAR(std::sqrt(v.x * v.x + v.z * v.z), 6.0, 1e-4);
    CHECK_NEAR(v.z, 0.0, 1e-6);
    Vector3 diag{1.f, -3.f, 1.f};  // replaces the old horizontal and falling speed
    apply_knockback(diag, Vector3{-10.f, 5.f, -10.f}, Vector3{-7.f, 5.f, -7.f}, 3.f);
    CHECK(diag.x < 0.f && diag.z < 0.f && diag.y > 0.f);
    CHECK_NEAR(std::sqrt(diag.x * diag.x + diag.z * diag.z), 3.0, 1e-4);
    Vector3 same{0.f, 0.f, 0.f};
    apply_knockback(same, Vector3{1.f, 2.f, 3.f}, Vector3{1.f, 2.f, 3.f});
    CHECK(std::isfinite(same.x) && std::isfinite(same.y) && std::isfinite(same.z));
    CHECK(same.y > 0.f);
    Vector3 above{0.f, 0.f, 0.f};  // straight above: only the upward pop
    apply_knockback(above, Vector3{1.f, 9.f, 3.f}, Vector3{1.f, 2.f, 3.f});
    CHECK(std::isfinite(above.x) && above.x == 0.f && above.z == 0.f && above.y > 0.f);
}

TEST(test_loot_ranges) {
    uint32_t rng = 2024;
    for (MobType t : EVERY_TYPE) {
        const MobDef& d = mob_def(t);
        // Per entry: how often each count came out (0 = nothing of it).
        std::vector<std::map<int, int>> seen(d.loot.size());
        for (int roll = 0; roll < 10000; ++roll) {
            const std::vector<ItemStack> got = roll_loot(d, rng);
            std::vector<int> count(d.loot.size(), 0);
            for (const ItemStack& s : got) {
                CHECK(!s.empty());
                CHECK(s.damage == 0);
                bool known = false;
                for (std::size_t i = 0; i < d.loot.size(); ++i)
                    if (d.loot[i].item == s.id) {
                        count[i] += s.count;
                        known = true;
                    }
                CHECK(known);
            }
            for (std::size_t i = 0; i < d.loot.size(); ++i) {
                CHECK(count[i] <= d.loot[i].max);
                ++seen[i][count[i]];
            }
        }
        for (std::size_t i = 0; i < d.loot.size(); ++i) {
            const LootEntry& e = d.loot[i];
            for (int c = e.min; c <= e.max; ++c) CHECK(seen[i][c] > 0);  // every value occurs
            // How often the entry gives something: chance x (share of counts >= 1).
            const int values = e.max - e.min + 1, positive = e.max - std::max<int>(e.min, 1) + 1;
            const double want = static_cast<double>(e.chance) * positive / values;
            const double got = 1.0 - seen[i][0] / 10000.0;
            CHECK(std::fabs(got - want) <= 0.02);
        }
    }
    // Same rng, same loot.
    uint32_t a = 9, b = 9;
    for (int i = 0; i < 20; ++i) {
        const auto x = roll_loot(mob_def(MobType::Tusker), a), y = roll_loot(mob_def(MobType::Tusker), b);
        CHECK(x.size() == y.size());
        for (std::size_t k = 0; k < x.size() && k < y.size(); ++k) CHECK(x[k].id == y[k].id && x[k].count == y[k].count);
    }
    // An empty table gives nothing.
    MobDef none = mob_def(MobType::Hopper);
    none.loot.clear();
    CHECK(roll_loot(none, a).empty());
}

TEST(test_loot_chance) {
    // A half-chance entry of exactly 2: present in 50% +- 2% of 10,000 rolls, always as 2.
    MobDef d = mob_def(MobType::Hopper);
    d.loot = {LootEntry{itm::FIBER, 2, 2, 0.5f}};
    uint32_t rng = 31337;
    int present = 0;
    for (int i = 0; i < 10000; ++i) {
        const std::vector<ItemStack> got = roll_loot(d, rng);
        CHECK(got.size() <= 1);
        if (!got.empty()) {
            CHECK(got[0].id == itm::FIBER && got[0].count == 2);
            ++present;
        }
    }
    CHECK(std::abs(present - 5000) <= 200);
}

TEST(test_projectile_flight) {
    const World w = stone_floor();
    std::vector<Projectile> ps{make_thorn(Vector3{-20.f, 100.f, 0.5f}, Vector3{0.f, 100.f, 0.5f}, 3.f)};
    CHECK_NEAR(ps[0].vel.x, 18.0, 1e-4);  // aimed straight at the target at speed 18
    CHECK_NEAR(ps[0].vel.y, 0.0, 1e-6);
    CHECK(ps[0].damage == 3.f && ps[0].age == 0.f);
    Vitals v;
    DamageState d;
    const Vector3 far_player{0.5f, 70.f, 40.5f};
    for (int i = 0; i < 60; ++i) update_projectiles(ps, w, far_player, v, d, 0, 1.f / 60.f);
    CHECK(ps.size() == 1);
    if (ps.empty()) return;
    CHECK_NEAR(ps[0].pos.x, -2.0, 0.01);  // 18 blocks along its line
    CHECK_NEAR(100.f - ps[0].pos.y, 5.0, 0.05);  // gravity 10: 0.5 x 10 x 1^2 below the line
    CHECK_NEAR(ps[0].vel.y, -10.0, 1e-3);
    CHECK_NEAR(ps[0].age, 1.0, 1e-4);
    // One long step equals many short ones.
    std::vector<Projectile> one_step{make_thorn(Vector3{-20.f, 100.f, 0.5f}, Vector3{0.f, 100.f, 0.5f}, 3.f)};
    update_projectiles(one_step, w, far_player, v, d, 0, 1.f);
    CHECK(one_step.size() == 1 && std::fabs(one_step[0].pos.y - ps[0].pos.y) < 0.01f);
    // A thorn aimed at its own origin flies nowhere useful but stays finite.
    const Projectile odd = make_thorn(Vector3{1.f, 80.f, 1.f}, Vector3{1.f, 80.f, 1.f}, 3.f);
    CHECK(std::isfinite(odd.vel.x) && std::isfinite(odd.vel.y) && std::isfinite(odd.vel.z));
    CHECK_NEAR(std::sqrt(odd.vel.x * odd.vel.x + odd.vel.y * odd.vel.y + odd.vel.z * odd.vel.z), 18.0, 1e-3);
}

TEST(test_projectile_hits_player_aabb) {
    const World w = stone_floor();
    const Vector3 player{0.5f, 70.f, 0.5f};  // box x, z in [0.2, 0.8], y in [70, 71.8]
    {
        std::vector<Projectile> ps{make_thorn(Vector3{-5.f, 71.f, 0.5f}, Vector3{0.5f, 71.f, 0.5f}, 3.f)};
        Vitals v;
        DamageState d;
        int hits = 0;
        for (int i = 0; i < 60 && !ps.empty(); ++i) hits += update_projectiles(ps, w, player, v, d, 10, 1.f / 60.f);
        CHECK(hits == 1);
        CHECK(ps.empty());  // removed on the hit
        CHECK_NEAR(v.health, 20.0 - 3.0 * 0.6, 1e-4);  // armor 10 keeps 60%
        CHECK(d.invuln > 0.f);
    }
    {
        // Passing 0.25 beside the box: nothing.
        std::vector<Projectile> ps{make_thorn(Vector3{-5.f, 71.f, 1.05f}, Vector3{6.f, 71.f, 1.05f}, 3.f)};
        Vitals v;
        DamageState d;
        int hits = 0;
        for (int i = 0; i < 60; ++i) hits += update_projectiles(ps, w, player, v, d, 0, 1.f / 60.f);
        CHECK(hits == 0);
        CHECK(v.health == 20.f);
    }
    {
        // A hit during invulnerability still removes the thorn but takes no health.
        std::vector<Projectile> ps{make_thorn(Vector3{-5.f, 71.f, 0.5f}, Vector3{0.5f, 71.f, 0.5f}, 3.f)};
        Vitals v;
        DamageState d;
        d.invuln = 5.f;
        int hits = 0;
        for (int i = 0; i < 60 && !ps.empty(); ++i) hits += update_projectiles(ps, w, player, v, d, 0, 1.f / 60.f);
        CHECK(hits == 1 && ps.empty() && v.health == 20.f);
    }
}

TEST(test_projectile_stops_on_block) {
    World w = stone_floor();
    for (int y = 70; y < 80; ++y)
        for (int z = -2; z <= 2; ++z) w.chunk(chunk_of(5, y, z))->set(5, y % CHUNK, floor_mod(z, CHUNK), blk::STONE);
    std::vector<Projectile> ps{make_thorn(Vector3{0.f, 72.f, 0.5f}, Vector3{10.f, 72.f, 0.5f}, 3.f)};
    Vitals v;
    DamageState d;
    const Vector3 far_player{0.5f, 70.f, 40.5f};
    for (int i = 0; i < 12; ++i) update_projectiles(ps, w, far_player, v, d, 0, 1.f / 60.f);  // 0.2 s: 3.6 blocks
    CHECK(ps.size() == 1);
    for (int i = 0; i < 12; ++i) update_projectiles(ps, w, far_player, v, d, 0, 1.f / 60.f);  // reaches x = 5
    CHECK(ps.empty());
    // Into the floor.
    std::vector<Projectile> down{make_thorn(Vector3{0.5f, 71.f, 0.5f}, Vector3{0.5f, 60.f, 0.5f}, 3.f)};
    for (int i = 0; i < 30; ++i) update_projectiles(down, w, far_player, v, d, 0, 1.f / 60.f);
    CHECK(down.empty());
    // Into an unloaded column: removed too.
    std::vector<Projectile> out{make_thorn(Vector3{46.f, 90.f, 0.5f}, Vector3{60.f, 90.f, 0.5f}, 3.f)};
    for (int i = 0; i < 30; ++i) update_projectiles(out, w, far_player, v, d, 0, 1.f / 60.f);
    CHECK(out.empty());
    CHECK(v.health == 20.f);
}

TEST(test_projectile_expires) {
    const World w = stone_floor();
    // Straight up from y = 150: at 5 s it is back down at y = 115, still in the air.
    std::vector<Projectile> ps{make_thorn(Vector3{0.5f, 150.f, 0.5f}, Vector3{0.5f, 160.f, 0.5f}, 3.f)};
    Vitals v;
    DamageState d;
    const Vector3 far_player{0.5f, 70.f, 40.5f};
    update_projectiles(ps, w, far_player, v, d, 0, 0.9f);
    for (int i = 0; i < 4; ++i) update_projectiles(ps, w, far_player, v, d, 0, 1.f);  // 4.9 s
    CHECK(ps.size() == 1);
    update_projectiles(ps, w, far_player, v, d, 0, 0.1f);  // 5.0 s
    CHECK(ps.empty());
    // Zero, negative, or NaN dt changes nothing.
    std::vector<Projectile> q{make_thorn(Vector3{0.5f, 150.f, 0.5f}, Vector3{0.5f, 160.f, 0.5f}, 3.f)};
    for (float bad : {0.f, -1.f, std::numeric_limits<float>::quiet_NaN()}) {
        CHECK(update_projectiles(q, w, far_player, v, d, 0, bad) == 0);
        CHECK(q.size() == 1 && q[0].pos.y == 150.f && q[0].age == 0.f);
    }
    std::vector<Projectile> none;
    CHECK(update_projectiles(none, w, far_player, v, d, 0, 1.f) == 0);
}

TEST(test_mob_damage_and_death) {
    const World w = stone_floor();
    MobManager m;
    const uint32_t id = m.spawn(MobType::Hopper, Vector3{0.5f, 70.f, 0.5f}, 4u);  // 3 health
    CHECK(m.damage(id, 2.f, Vector3{-1.5f, 70.f, 0.5f}));
    Mob* h = m.find(id);
    CHECK(h != nullptr && h->health == 1.f);
    CHECK(h->invuln == 0.5f && h->hurt == 0.3f);
    CHECK(h->vel.x > 0.f && h->vel.y > 0.f);  // knocked away from the attacker at -X
    CHECK(!m.damage(id, 2.f, Vector3{-1.5f, 70.f, 0.5f}));  // still invulnerable
    CHECK(h->health == 1.f);
    CHECK(!m.damage(id, 0.f, Vector3{}) && !m.damage(id, std::nanf(""), Vector3{}));
    CHECK(!m.damage(9999u, 5.f, Vector3{}));  // unknown id
    // The knockback carries the mob away before its AI takes over again.
    AiContext ctx{};
    ctx.player_pos = Vector3{0.f, -500.f, 0.f};
    ctx.player_alive = false;
    ctx.daylight = 1.f;
    ctx.world = &w;
    for (int i = 0; i < 18; ++i) m.update(w, ctx, 1.f / 60.f);  // the 0.3 s knockback: 6 x 0.3 = 1.8 blocks
    h = m.find(id);
    CHECK(h != nullptr && h->pos.x > 2.f);
    for (int i = 0; i < 22; ++i) m.update(w, ctx, 1.f / 60.f);
    h = m.find(id);
    CHECK(h != nullptr && h->invuln == 0.f && h->hurt == 0.f && h->knock == 0.f);
    CHECK(m.take_dead().empty());
    CHECK(m.damage(id, 5.f, Vector3{0.5f, 70.f, 0.5f}));
    h = m.find(id);
    CHECK(h != nullptr && h->health == 0.f);  // clamped
    const std::vector<Mob> dead = m.take_dead();
    CHECK(dead.size() == 1 && dead[0].id == id && dead[0].type == MobType::Hopper);
    CHECK(m.find(id) == nullptr);
    CHECK(!m.damage(id, 1.f, Vector3{}));
}

TEST(test_hostiles_within) {
    MobManager m;
    const Vector3 p{0.5f, 70.f, 0.5f};
    CHECK(!hostiles_within(m, p, 8.f));
    m.spawn(MobType::Tusker, Vector3{1.5f, 70.f, 0.5f}, 1u);  // passive: never counts
    CHECK(!hostiles_within(m, p, 8.f));
    const uint32_t g = m.spawn(MobType::Gloomwalker, Vector3{8.5f, 70.f, 0.5f}, 1u);  // exactly 8
    CHECK(hostiles_within(m, p, 8.f));
    m.find(g)->pos.x = 8.6f;  // 8.1
    CHECK(!hostiles_within(m, p, 8.f));
    m.find(g)->pos = Vector3{0.5f, 62.f, 0.5f};  // 8 below
    CHECK(hostiles_within(m, p, 8.f));
}
