// Block entities: the state of kilns, crates, and grave cairns (M5), and kiln smelting.
// Stored per chunk in the World (Chunk::entities) and saved in region format v2.
// Main thread only (Global Constraints), like the World that holds them.
#pragma once

#include "items.hpp"

#include <array>
#include <cstdint>
#include <variant>
#include <vector>

struct KilnState {
    std::array<ItemStack, 2> in;
    ItemStack fuel, out;
    float burn_left = 0, burn_total = 0, progress = 0;  // seconds
};
struct CrateState {
    std::array<ItemStack, 27> slots;
};
struct CairnState {
    std::vector<ItemStack> items;  // used in M5
};
// The variant index is the region v2 `kind` byte: 0 kiln, 1 crate, 2 cairn. Append only.
using BlockEntity = std::variant<KilnState, CrateState, CairnState>;

constexpr float SMELT_SECONDS = 10.f;

struct SmeltRecipe {
    std::array<ItemStack, 2> inputs;  // inputs[1] empty for single-input recipes
    ItemStack output;
};

// The bronze alloy (3 COPPER_INGOT + 1 TIN_INGOT -> 4 BRONZE_INGOT) first, then ore -> ingot
// (COPPER, TIN, IRON, SILVER), SAND -> GLASS, RAW_MEAT -> COOKED_MEAT, SMALL_MEAT ->
// COOKED_SMALL_MEAT, each 1 -> 1.
const std::vector<SmeltRecipe>& smelt_recipes();

float fuel_seconds(ItemId id);  // COAL 80, LOG and PINE_LOG 15, PLANKS 7.5, STICK 2.5, else 0

// Zero, negative, or NaN dt does nothing. The first smelt recipe that the two input slots hold
// (a two-input recipe in either slot order; a single-input one in either slot) progresses while
// its output fits in `out` (empty, or the same item with room for the whole output). Each smelt
// takes SMELT_SECONDS of burning; when burn_left runs out, one fuel item is lit (burn_left =
// burn_total = fuel_seconds) only if a recipe can progress. With no recipe that can progress,
// progress resets to 0 and burning fuel pauses. Large dt runs every smelt and refuel in order.
void kiln_tick(KilnState& k, float dt);

// The non-empty stacks the entity holds, dropped when its block is broken: kiln in, in, fuel, out;
// crate slots in order; cairn items in order.
std::vector<ItemStack> block_entity_contents(const BlockEntity& be);
