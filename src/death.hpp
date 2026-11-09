// Death and respawn: where a grave cairn goes, filling it, the bedroll respawn point, and sleeping.
// Pure logic on the World (main thread only).
#pragma once

#include "coords.hpp"
#include "inventory.hpp"
#include "world_time.hpp"

#include "raylib.h"  // Vector3 only

#include <optional>

class World;

constexpr int CAIRN_UP = 8, CAIRN_SIDE = 3;

// The cell for a grave cairn: a free cell is in a loaded column and neither solid nor liquid
// (AIR, plants, torches). The feet cell's y is clamped to 0..255 first. Then the first free cell
// from the feet cell up to CAIRN_UP above it (never y >= 256); else the free cell at the feet y
// with |dx|, |dz| <= CAIRN_SIDE nearest by dx^2 + dz^2 (ties: lowest dz, then lowest dx); else
// nullopt (the game then drops the items at the feet).
std::optional<BlockPos> find_cairn_spot(const World& w, Vector3 feet);

// Moves every non-empty stack (slots 0..35, then armor Head..Feet, damage kept) into a CairnState
// at p and sets p to GRAVE_CAIRN. Nothing to store, or a cell the World cannot set (unloaded column,
// y outside 0..255): no block is placed and inv is unchanged.
void place_cairn(World& w, BlockPos p, Inventory& inv);

// The cell still holds BEDROLL and the 2 cells above are neither solid nor liquid. An unloaded
// bedroll reads as AIR: not usable.
bool bedroll_usable(const World& w, BlockPos bedroll);
// On top of the bedroll (x + 0.5, y + 1, z + 0.5) if it is set and usable; else spawn_player's
// position. The caller waits until the bedroll's column, and column (0, 0) when the bedroll is not
// usable, are loaded.
Vector3 respawn_point(const World& w, std::optional<BlockPos> bedroll);

// Night only (day tick >= DAY_END), and no hostile creature within 8 blocks (M6 supplies it).
bool can_sleep(const WorldTime& t, bool hostiles_within_8);
// Moves the clock forward to the next sunrise (the next multiple of DAY_TICKS, day tick 0).
void sleep_until_dawn(WorldTime& t);
