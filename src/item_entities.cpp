#include "item_entities.hpp"

#include "coords.hpp"
#include "physics.hpp"
#include "world.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace {

constexpr float GRAVITY = 20.f, MAX_FALL = 40.f, GROUND_FRICTION = 8.f;  // friction: 1/s
constexpr float POP_UP_MIN = 3.f, POP_UP_SPAN = 1.f, DRIFT = 1.5f;
constexpr Vector3 HALF{ITEM_HALF, 0.f, ITEM_HALF};

float dist2(Vector3 a, Vector3 b) {
    const float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return dx * dx + dy * dy + dz * dz;
}

bool loaded(const World& w, Vector3 p) {
    return w.column_loaded(column_of(chunk_of(static_cast<int>(std::floor(p.x)), 0,
                                              static_cast<int>(std::floor(p.z)))));
}

}  // namespace

void ItemEntities::spawn(Vector3 pos, ItemStack s) {
    if (s.empty()) return;
    // xorshift32: three values in [0, 1) per spawn.
    auto next = [this] {
        rng_ ^= rng_ << 13;
        rng_ ^= rng_ >> 17;
        rng_ ^= rng_ << 5;
        return static_cast<float>(rng_ >> 8) / 16777216.f;
    };
    const float vx = (next() * 2.f - 1.f) * DRIFT;
    const float vy = POP_UP_MIN + next() * POP_UP_SPAN;
    const float vz = (next() * 2.f - 1.f) * DRIFT;
    items_.push_back(ItemEntity{pos, Vector3{vx, vy, vz}, s, 0.f});
}

void ItemEntities::update(const World& w, float dt) {
    if (!(dt > 0.f)) return;
    std::vector<bool> active(items_.size());
    for (std::size_t i = 0; i < items_.size(); ++i) {
        ItemEntity& e = items_[i];
        if (!loaded(w, e.pos)) continue;  // frozen until its column streams in
        active[i] = true;
        e.age += dt;
        e.vel.y = std::max(e.vel.y - GRAVITY * dt, -MAX_FALL);
        const MoveResult r = move_aabb(w, e.pos, e.vel, HALF, ITEM_SIZE, dt);
        e.pos = r.pos;
        e.vel = r.vel;
        if (r.on_ground) {
            const float keep = std::max(0.f, 1.f - GROUND_FRICTION * dt);
            e.vel.x *= keep;
            e.vel.z *= keep;
        }
    }
    for (std::size_t i = 0; i < items_.size(); ++i) {
        if (!active[i]) continue;
        ItemEntity& a = items_[i];
        const int max = item_def(a.stack.id).max_stack;
        if (max <= 1 || a.stack.empty()) continue;
        for (std::size_t j = i + 1; j < items_.size() && a.stack.count < max; ++j) {
            ItemEntity& b = items_[j];
            if (!active[j] || b.stack.empty() || b.stack.id != a.stack.id || b.stack.damage != a.stack.damage)
                continue;
            if (dist2(a.pos, b.pos) > ITEM_MERGE_RANGE * ITEM_MERGE_RANGE) continue;
            const int moved = std::min(static_cast<int>(b.stack.count), max - a.stack.count);
            a.stack.count = static_cast<uint8_t>(a.stack.count + moved);
            b.stack.count = static_cast<uint8_t>(b.stack.count - moved);
            a.age = std::max(a.age, b.age);  // merging never extends a stack's life
        }
    }
    std::erase_if(items_, [](const ItemEntity& e) { return e.stack.empty() || e.age >= ITEM_DESPAWN_AGE; });
}

std::vector<ItemStack> ItemEntities::collect(Vector3 player_pos, Inventory& inv) {
    std::vector<ItemStack> got;
    for (ItemEntity& e : items_) {
        if (e.age < ITEM_PICKUP_DELAY || dist2(e.pos, player_pos) > ITEM_PICKUP_RANGE * ITEM_PICKUP_RANGE) continue;
        const int left = inv.add(e.stack);
        const int taken = e.stack.count - left;
        if (taken == 0) continue;
        got.push_back(ItemStack{e.stack.id, static_cast<uint8_t>(taken), e.stack.damage});
        e.stack.count = static_cast<uint8_t>(left);
    }
    std::erase_if(items_, [](const ItemEntity& e) { return e.stack.empty(); });
    return got;
}

const std::vector<ItemEntity>& ItemEntities::all() const {
    return items_;
}

std::vector<ItemEntity>& ItemEntities::all() {
    return items_;
}
