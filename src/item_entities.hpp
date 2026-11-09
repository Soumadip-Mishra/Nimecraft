// Dropped items lying in the world: falling, merging, pickup, and despawn.
// Main thread only (Global Constraints): entities are never touched by worker jobs.
#pragma once

#include "inventory.hpp"
#include "items.hpp"

#include "raylib.h"  // Vector3 only

#include <cstdint>
#include <vector>

class World;

constexpr float ITEM_HALF = 0.125f, ITEM_SIZE = 0.25f;  // a 0.25 box, feet at pos
constexpr float ITEM_PICKUP_DELAY = 0.5f, ITEM_PICKUP_RANGE = 1.5f, ITEM_MERGE_RANGE = 0.5f,
                ITEM_DESPAWN_AGE = 300.f;

struct ItemEntity {
    Vector3 pos, vel;
    ItemStack stack;
    float age = 0;
};

class ItemEntities {
public:
    // Adds an entity at pos (feet) with a small upward pop and sideways drift from a seeded rng,
    // so the same sequence of spawns gives the same velocities. Empty stacks are not spawned.
    void spawn(Vector3 pos, ItemStack s);
    // Zero, negative, or NaN dt does nothing. Each entity whose feet column is loaded ages by dt,
    // falls (gravity 20, at most 40 down) through move_aabb, and loses sideways speed on the
    // ground; an entity in an unloaded column is frozen (no motion, no aging). Then entities
    // aged >= 300 s are removed, and stackable stacks of the same id and damage whose positions
    // are within 0.5 merge into the earlier one up to max_stack (the rest stays in the later).
    void update(const World& w, float dt);
    // Every entity aged >= 0.5 s within 1.5 of player_pos goes into inv.add; what fits is
    // returned (one stack per entity that gave something), leftovers stay on the ground.
    std::vector<ItemStack> collect(Vector3 player_pos, Inventory& inv);
    const std::vector<ItemEntity>& all() const;
    std::vector<ItemEntity>& all();  // tests and the game's renderer only

private:
    std::vector<ItemEntity> items_;
    uint32_t rng_ = 0x9E3779B9u;
};
