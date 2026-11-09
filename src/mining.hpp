// Break times by block hardness and held tool, block drops, and tool wear.
#pragma once

#include "block.hpp"
#include "block_entities.hpp"
#include "items.hpp"
#include "player.hpp"

#include <cstdint>
#include <optional>
#include <vector>

// Seconds to break b with `held`; +infinity when !breakable. base = hardness * 1.5. The held
// tier counts only when the held item's tool kind is b's tool (tier 0 otherwise, the hand). With
// that tier below min_tier: hardness * 5. Otherwise, with the right tool, base / speed[tier],
// speed = {1, 2, 4, 5, 6, 8} for tiers 0..5; any other item mines at hand speed (base).
float break_time(BlockId b, const ItemStack& held);

// What breaking b with `held` drops: nullopt when !breakable, when the tier is below min_tier,
// when b drops nothing, or when the drop_chance roll fails; else drop_min..drop_max of b's drop.
// The roll and the count come from a hash of rng, so the same rng gives the same result.
std::optional<ItemStack> block_drop(BlockId b, const ItemStack& held, uint32_t rng);

// Tools only (tool kind != None, durability > 0): damage += 1, and the stack empties (becomes
// ItemStack{}) when damage reaches durability. Anything else is unchanged.
void wear_tool(ItemStack& held);

// What breaking b (holding entity `be`, or nullptr; its Chunk::meta `meta`) gives. Survival: be's
// contents (block_entity_contents), then block_drop(b, held, rng) if any, then
// growth_drops(b, meta, rng) (M7), and wear_tool(held) when b has hardness > 0. Creative: nothing,
// and held is unchanged. An unbreakable b gives nothing in both.
std::vector<ItemStack> break_drops(BlockId b, const BlockEntity* be, ItemStack& held, GameMode mode, uint32_t rng,
                                   uint8_t meta = 0);
