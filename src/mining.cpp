#include "mining.hpp"

#include "growth.hpp"

#include <limits>

namespace {

constexpr float HAND_FACTOR = 1.5f, LOW_TIER_FACTOR = 5.f;
constexpr float SPEED[6] = {1.f, 2.f, 4.f, 5.f, 6.f, 8.f};  // by tier 0..5
constexpr uint32_t PERCENT = 100;

// The held tier as it applies to b: the item's tier when its tool kind is b's tool, else 0.
int effective_tier(const BlockDef& d, const ItemStack& held) {
    if (held.empty() || d.tool == ToolKind::None) return 0;
    const ItemDef& item = item_def(held.id);
    return item.tool == d.tool ? item.tier : 0;
}

uint32_t mix(uint32_t h) {
    h ^= h >> 16;
    h *= 0x7FEB352Du;
    h ^= h >> 15;
    h *= 0x846CA68Bu;
    h ^= h >> 16;
    return h;
}

}  // namespace

float break_time(BlockId b, const ItemStack& held) {
    const BlockDef& d = block_def(b);
    if (!d.breakable) return std::numeric_limits<float>::infinity();
    const int tier = effective_tier(d, held);
    if (tier < d.min_tier) return d.hardness * LOW_TIER_FACTOR;
    const float base = d.hardness * HAND_FACTOR;
    return base / SPEED[tier];
}

std::optional<ItemStack> block_drop(BlockId b, const ItemStack& held, uint32_t rng) {
    const BlockDef& d = block_def(b);
    if (!d.breakable || d.drop == 0 || d.drop_max == 0) return std::nullopt;
    if (effective_tier(d, held) < d.min_tier) return std::nullopt;
    const uint32_t h = mix(rng);
    if (h % PERCENT >= d.drop_chance) return std::nullopt;
    const uint32_t span = static_cast<uint32_t>(d.drop_max - d.drop_min) + 1;
    const uint32_t count = d.drop_min + (h / PERCENT) % span;
    if (count == 0) return std::nullopt;
    return ItemStack{d.drop, static_cast<uint8_t>(count), 0};
}

void wear_tool(ItemStack& held) {
    if (held.empty()) return;
    const ItemDef& d = item_def(held.id);
    if (d.tool == ToolKind::None || d.durability == 0) return;
    ++held.damage;
    if (held.damage >= d.durability) held = ItemStack{};
}

std::vector<ItemStack> break_drops(BlockId b, const BlockEntity* be, ItemStack& held, GameMode mode, uint32_t rng,
                                   uint8_t meta) {
    std::vector<ItemStack> out;
    const BlockDef& d = block_def(b);
    if (mode == GameMode::Creative || !d.breakable) return out;
    if (be != nullptr) out = block_entity_contents(*be);
    if (const std::optional<ItemStack> drop = block_drop(b, held, rng)) out.push_back(*drop);
    for (const ItemStack& s : growth_drops(b, meta, rng)) out.push_back(s);
    if (d.hardness > 0.f) wear_tool(held);
    return out;
}
