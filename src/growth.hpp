// Plant growth on random ticks (grass, saplings, the Sunroot crop), falling sand and gravel on
// scheduled ticks, the extra drops of plants and crops, and planting. Main thread only, like World.
#pragma once

#include "block.hpp"
#include "coords.hpp"
#include "items.hpp"

#include <cstdint>
#include <optional>
#include <vector>

class World;
class TickScheduler;

constexpr int GROW_LIGHT = 9;  // max(sky, block) light a plant needs
constexpr uint32_t GRASS_SPREAD_ONE_IN = 4, SAPLING_ONE_IN = 7, CROP_ONE_IN = 3;
constexpr int CROP_WATER_REACH = 4;  // WATER within this many cells on x and z, on the crop's y
constexpr uint8_t CROP_RIPE = 3;     // SUNROOT_CROP meta: stage 0..3
constexpr int OAK_TRUNK = 5, PINE_TRUNK_MIN = 6, PINE_TRUNK_SPAN = 3;  // pine 6..8
constexpr uint32_t FALL_DELAY = 2;  // ticks before a block over a new gap falls

// One random tick of the block at p (rng is xorshift32 state, 0 is replaced); nothing in unloaded
// columns, outside y 0..255, or for blocks without random ticks.
// - GRASS: becomes DIRT when the block above has opacity >= 2. Otherwise, with 1/4 chance, picks one
//   of the 26 cells around it (x, y, z each -1..1); DIRT there turns to GRASS when the block above
//   it has opacity < 2 and that cell's light is >= 9.
// - SAPLING / PINE_SAPLING: with light >= 9 at p and 1/7 chance, grows an oak (trunk 5) or a pine
//   (trunk 6..8) from feature_cells, when every trunk cell and every canopy core cell (leaves within
//   1 of the trunk axis on x and z) is inside the world, loaded, and AIR (p itself excepted). Logs
//   overwrite; leaves go only into AIR.
// - SUNROOT_CROP: below stage 3, with light >= 9 at p and WATER within 4 on x and z on p's y,
//   advances one stage with 1/3 chance.
void random_tick_block(World& w, BlockPos p, uint32_t& rng);

// SAND or GRAVEL at p with a non-solid block below moves to the lowest non-solid cell under it in
// its column (y 0 at the lowest; whatever was there is replaced), then schedules p and the landing
// cell with their 6 neighbors after FALL_DELAY. Anything else, or an unloaded p, is left alone.
void falling_block_update(World& w, TickScheduler& t, BlockPos p);

// A scheduled tick: SAND and GRAVEL fall (falling_block_update), every other cell goes to
// fluid_update.
void block_tick(World& w, TickScheduler& t, BlockPos p);

// Drops on top of block_drop when b is broken in Survival, from a hash of rng independent of
// block_drop's roll: LEAVES a SAPLING 1/20, PINE_LEAVES a PINE_SAPLING 1/20, TALL_GRASS
// SUNROOT_SEEDS 1/8; SUNROOT_CROP at stage 3 (or any meta above) 1-2 SUNROOT and 1-3 SUNROOT_SEEDS,
// below it exactly 1 SUNROOT_SEEDS. Nothing for other blocks.
std::vector<ItemStack> growth_drops(BlockId b, uint8_t meta, uint32_t rng);

bool is_plant_item(ItemId id);  // SUNROOT_SEEDS, SAPLING, PINE_SAPLING
// The block that planting `held` on top of `ground` places: SUNROOT_CROP for seeds, the sapling
// itself for saplings, each only on GRASS or DIRT; nullopt otherwise.
std::optional<BlockId> plant_from(ItemId held, BlockId ground);
