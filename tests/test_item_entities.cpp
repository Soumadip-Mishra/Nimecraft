// Tests for src/item_entities.*: dropped items falling, merging, pickup, despawn, freezing.
#include "block.hpp"
#include "chunk.hpp"
#include "coords.hpp"
#include "inventory.hpp"
#include "item_entities.hpp"
#include "items.hpp"
#include "test.hpp"
#include "world.hpp"

#include <cmath>
#include <cstddef>
#include <limits>
#include <memory>
#include <vector>

namespace {

// Columns (-2..2, -2..2) with STONE at y = 0..69: the floor top is y = 70.
World flat() {
    World w(1);
    for (int cx = -2; cx <= 2; ++cx)
        for (int cz = -2; cz <= 2; ++cz) {
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

ItemStack stack(ItemId id, int count) {
    return ItemStack{id, static_cast<uint8_t>(count), 0};
}

// An entity resting on the floor at (x, 70, z), still, with the given age.
void place(ItemEntities& items, float x, float z, ItemStack s, float age = 0.f) {
    items.spawn(Vector3{x, 70.f, z}, s);
    ItemEntity& e = items.all().back();
    e.vel = Vector3{0, 0, 0};
    e.age = age;
}

int total(const ItemEntities& items, ItemId id) {
    int n = 0;
    for (const ItemEntity& e : items.all())
        if (e.stack.id == id) n += e.stack.count;
    return n;
}

}  // namespace

TEST(test_item_spawn) {
    ItemEntities items;
    items.spawn(Vector3{1.5f, 71.f, 2.5f}, stack(blk::DIRT, 3));
    CHECK(items.all().size() == 1);
    const ItemEntity& e = items.all()[0];
    CHECK(e.stack.id == blk::DIRT && e.stack.count == 3);
    CHECK(e.age == 0.f);
    CHECK(e.vel.y > 0.f);  // a small upward pop
    CHECK(std::fabs(e.vel.x) <= 2.f && std::fabs(e.vel.z) <= 2.f);
    items.spawn(Vector3{0, 71.f, 0}, ItemStack{});  // empty stacks are not spawned
    CHECK(items.all().size() == 1);
    // Seeded: two fresh collections give the same velocities.
    ItemEntities a, b;
    for (int i = 0; i < 5; ++i) {
        a.spawn(Vector3{0, 80, 0}, stack(blk::DIRT, 1));
        b.spawn(Vector3{0, 80, 0}, stack(blk::DIRT, 1));
    }
    for (std::size_t i = 0; i < 5; ++i) {
        CHECK(a.all()[i].vel.x == b.all()[i].vel.x);
        CHECK(a.all()[i].vel.y == b.all()[i].vel.y);
        CHECK(a.all()[i].vel.z == b.all()[i].vel.z);
    }
    CHECK(a.all()[0].vel.x != a.all()[1].vel.x);
}

TEST(test_item_merge) {
    World w = flat();
    ItemEntities near;
    place(near, 8.5f, 8.5f, stack(blk::DIRT, 10));
    place(near, 8.8f, 8.5f, stack(blk::DIRT, 10));
    near.update(w, 1.f / 60.f);
    CHECK(near.all().size() == 1);
    CHECK(total(near, blk::DIRT) == 20);

    ItemEntities far;
    place(far, 8.5f, 8.5f, stack(blk::DIRT, 10));
    place(far, 9.1f, 8.5f, stack(blk::DIRT, 10));
    far.update(w, 1.f / 60.f);
    CHECK(far.all().size() == 2);

    ItemEntities mixed;
    place(mixed, 8.5f, 8.5f, stack(blk::DIRT, 10));
    place(mixed, 8.6f, 8.5f, stack(blk::STONE, 10));
    mixed.update(w, 1.f / 60.f);
    CHECK(mixed.all().size() == 2);

    ItemEntities full;
    place(full, 8.5f, 8.5f, stack(blk::DIRT, 60));
    place(full, 8.7f, 8.5f, stack(blk::DIRT, 10));
    full.update(w, 1.f / 60.f);
    CHECK(full.all().size() == 2);
    CHECK(full.all()[0].stack.count == 64);
    CHECK(full.all()[1].stack.count == 6);

    // Exactly 0.5 apart merges; tools never merge.
    ItemEntities edge;
    place(edge, 8.5f, 8.5f, stack(blk::SAND, 1));
    place(edge, 8.5f, 9.f, stack(blk::SAND, 1));
    edge.update(w, 1.f / 60.f);
    CHECK(edge.all().size() == 1);
    ItemEntities tools;
    place(tools, 8.5f, 8.5f, stack(itm::PICK_WOOD, 1));
    place(tools, 8.6f, 8.5f, stack(itm::PICK_WOOD, 1));
    tools.update(w, 1.f / 60.f);
    CHECK(tools.all().size() == 2);
}

TEST(test_item_pickup_delay) {
    ItemEntities items;
    Inventory inv;
    place(items, 8.5f, 8.5f, stack(blk::DIRT, 5), 0.49f);
    CHECK(items.collect(Vector3{8.5f, 70.f, 8.5f}, inv).empty());
    CHECK(inv.count(blk::DIRT) == 0);
    items.all()[0].age = 0.5f;
    const std::vector<ItemStack> got = items.collect(Vector3{8.5f, 70.f, 8.5f}, inv);
    CHECK(got.size() == 1);
    CHECK(got[0].id == blk::DIRT && got[0].count == 5);
    CHECK(inv.count(blk::DIRT) == 5);
    CHECK(items.all().empty());
}

TEST(test_item_collect_range) {
    ItemEntities items;
    Inventory inv;
    place(items, 8.5f, 8.5f, stack(blk::DIRT, 1), 1.f);
    place(items, 0.5f, 8.5f, stack(blk::SAND, 1), 1.f);
    items.collect(Vector3{10.f, 70.f, 8.5f}, inv);  // DIRT 1.5 away, SAND 9.5 away
    CHECK(inv.count(blk::DIRT) == 1);
    CHECK(inv.count(blk::SAND) == 0);
    ItemEntities far;
    place(far, 8.5f, 8.5f, stack(blk::DIRT, 1), 1.f);
    far.collect(Vector3{10.01f, 70.f, 8.5f}, inv);  // 1.51 away
    CHECK(inv.count(blk::DIRT) == 1);
    CHECK(far.all().size() == 1);
    // Vertical distance counts too: 1.5 straight up still collects.
    far.collect(Vector3{8.5f, 71.5f, 8.5f}, inv);
    CHECK(inv.count(blk::DIRT) == 2);
}

TEST(test_item_collect_leftover) {
    ItemEntities items;
    Inventory inv;
    for (auto& s : inv.slots) s = stack(blk::STONE, 64);
    place(items, 8.5f, 8.5f, stack(blk::DIRT, 10), 1.f);
    CHECK(items.collect(Vector3{8.5f, 70.f, 8.5f}, inv).empty());
    CHECK(items.all().size() == 1);
    CHECK(items.all()[0].stack.count == 10);
    inv.slots[5] = stack(blk::DIRT, 60);  // room for 4
    const std::vector<ItemStack> got = items.collect(Vector3{8.5f, 70.f, 8.5f}, inv);
    CHECK(got.size() == 1 && got[0].count == 4);
    CHECK(inv.slots[5].count == 64);
    CHECK(items.all().size() == 1);
    CHECK(items.all()[0].stack.count == 6);
}

TEST(test_item_despawn) {
    World w = flat();
    ItemEntities items;
    place(items, 8.5f, 8.5f, stack(blk::DIRT, 1), 299.9f);
    place(items, 2.5f, 2.5f, stack(blk::SAND, 1), 300.f);
    items.update(w, 1e-6f);  // below float spacing at 300: the ages stay 299.9 and exactly 300
    CHECK(items.all().size() == 1);
    CHECK(items.all()[0].stack.id == blk::DIRT);
    items.update(w, 0.2f);
    CHECK(items.all().empty());
}

TEST(test_item_falls_to_floor) {
    World w = flat();
    ItemEntities items;
    items.spawn(Vector3{8.5f, 80.f, 8.5f}, stack(blk::DIRT, 1));
    for (int i = 0; i < 180; ++i) items.update(w, 1.f / 60.f);
    CHECK(items.all().size() == 1);
    CHECK_NEAR(items.all()[0].pos.y, 70.0, 1e-3);
    CHECK_NEAR(items.all()[0].age, 3.0, 1e-3);
    // Ground friction stops the sideways pop.
    CHECK(std::fabs(items.all()[0].vel.x) < 0.05f && std::fabs(items.all()[0].vel.z) < 0.05f);
    // One large step falls the same way (dt = 1.0 three times).
    ItemEntities big;
    big.spawn(Vector3{8.5f, 80.f, 8.5f}, stack(blk::DIRT, 1));
    for (int i = 0; i < 3; ++i) big.update(w, 1.f);
    CHECK_NEAR(big.all()[0].pos.y, 70.0, 1e-3);
}

TEST(test_item_frozen_when_unloaded) {
    World w = flat();  // column 5 is not loaded
    ItemEntities items;
    items.spawn(Vector3{5 * 16 + 8.5f, 90.f, 8.5f}, stack(blk::DIRT, 1));
    const ItemEntity before = items.all()[0];
    for (int i = 0; i < 120; ++i) items.update(w, 1.f / 60.f);
    const ItemEntity& after = items.all()[0];
    CHECK(after.pos.x == before.pos.x && after.pos.y == before.pos.y && after.pos.z == before.pos.z);
    CHECK(after.age == 0.f);  // frozen items do not age toward despawn
    // A frozen item does not merge with one next to it either.
    place(items, 5 * 16 + 8.6f, 8.5f, stack(blk::DIRT, 1));
    items.update(w, 1.f / 60.f);
    CHECK(items.all().size() == 2);
}

// Coverage row 5: zero, negative, and NaN dt change nothing.
TEST(test_item_bad_dt) {
    World w = flat();
    ItemEntities items;
    items.spawn(Vector3{8.5f, 80.f, 8.5f}, stack(blk::DIRT, 1));
    const ItemEntity before = items.all()[0];
    for (float dt : {0.f, -1.f, std::numeric_limits<float>::quiet_NaN()}) items.update(w, dt);
    CHECK(items.all()[0].pos.y == before.pos.y);
    CHECK(items.all()[0].age == 0.f);
}

// Merging keeps the older age, so feeding a stack fresh drops never keeps it alive past 300 s.
TEST(test_item_merge_keeps_older_age) {
    World w = flat();
    ItemEntities items;
    place(items, 8.5f, 8.5f, stack(blk::DIRT, 1), 1.f);
    place(items, 8.6f, 8.5f, stack(blk::DIRT, 1), 299.f);
    items.update(w, 0.5f);
    CHECK(items.all().size() == 1);
    CHECK_NEAR(items.all()[0].age, 299.5, 1e-3);
    items.update(w, 0.5f);
    CHECK(items.all().empty());
}
