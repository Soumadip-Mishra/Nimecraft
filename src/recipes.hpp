// Recipe-list crafting: the recipe table, stations near the player, and atomic crafting.
#pragma once

#include "inventory.hpp"
#include "items.hpp"

#include "raylib.h"  // Vector3 only

#include <bitset>
#include <cstdint>
#include <vector>

class World;

enum class Station : uint8_t { None, Workbench, Kiln };

struct Recipe {
    std::vector<ItemStack> inputs;  // distinct ids, each count >= 1
    ItemStack output;
    Station station;
};

// Every recipe: hand recipes (Station::None) first, then workbench ones, the last being BEDROLL
// (3 WOOL + 3 PLANKS, M5).
const std::vector<Recipe>& recipes();

using StationSet = std::bitset<3>;  // indexed by Station; the None bit is never set

// The stations whose block center (x + 0.5, y + 0.5, z + 0.5) lies within radius of pos
// (inclusive). Unloaded columns read as AIR; a non-finite or negative radius finds nothing.
StationSet stations_near(const World& w, Vector3 pos, float radius = 3.f);

// Recipes in table order whose station is None or in `near` and whose inputs inv holds.
std::vector<const Recipe*> available(const Inventory& inv, StationSet near);

// Removes the inputs and adds the output, or returns false and leaves inv unchanged when an input
// is missing or the output does not fit after the inputs are taken.
bool craft(Inventory& inv, const Recipe& r);
