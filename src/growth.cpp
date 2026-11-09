#include "growth.hpp"

#include "fluids.hpp"
#include "light.hpp"
#include "terrain.hpp"
#include "ticks.hpp"
#include "world.hpp"

#include <algorithm>
#include <cstdlib>

namespace {

constexpr uint8_t COVER_OPACITY = 2;     // a block this opaque above kills grass
constexpr uint32_t AROUND = 26;          // cells in the 3x3x3 cube around a block, itself excluded
constexpr uint32_t SAPLING_DROP_ONE_IN = 20, SEED_DROP_ONE_IN = 8;
constexpr uint32_t DROP_SALT = 0x5EED5A17u;

uint32_t next(uint32_t& s) {
    if (s == 0) s = 0x9E3779B9u;
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s;
}

// A well-mixed 32-bit hash (murmur3 finalizer).
uint32_t mix(uint32_t h) {
    h ^= h >> 16;
    h *= 0x85EBCA6Bu;
    h ^= h >> 13;
    h *= 0xC2B2AE35u;
    h ^= h >> 16;
    return h;
}

bool loaded(const World& w, int x, int y, int z) {
    return y >= 0 && y < WORLD_HEIGHT && w.column_loaded(ColumnPos{floor_div(x, CHUNK), floor_div(z, CHUNK)});
}

int light_at(const World& w, int x, int y, int z) {
    return std::max(sky_light_at(w, x, y, z), block_light_at(w, x, y, z));
}

void grass_tick(World& w, BlockPos p, uint32_t& rng) {
    if (block_def(w.get(p.x, p.y + 1, p.z)).opacity >= COVER_OPACITY) {
        w.set(p.x, p.y, p.z, blk::DIRT);
        return;
    }
    const uint32_t r = next(rng);
    if (r % GRASS_SPREAD_ONE_IN != 0) return;
    uint32_t k = (r / GRASS_SPREAD_ONE_IN) % AROUND;
    if (k >= AROUND / 2) ++k;  // skip the center of the 27 cells
    const int x = p.x + static_cast<int>(k % 3) - 1, y = p.y + static_cast<int>(k / 3 % 3) - 1,
              z = p.z + static_cast<int>(k / 9) - 1;
    if (w.get(x, y, z) != blk::DIRT) return;  // unloaded cells read AIR
    if (block_def(w.get(x, y + 1, z)).opacity >= COVER_OPACITY) return;
    if (light_at(w, x, y + 1, z) < GROW_LIGHT) return;
    w.set(x, y, z, blk::GRASS);
}

void sapling_tick(World& w, BlockPos p, uint32_t& rng, bool pine) {
    if (light_at(w, p.x, p.y, p.z) < GROW_LIGHT) return;
    const uint32_t r = next(rng);
    if (r % SAPLING_ONE_IN != 0) return;
    const int trunk = pine ? PINE_TRUNK_MIN + static_cast<int>((r / SAPLING_ONE_IN) % PINE_TRUNK_SPAN) : OAK_TRUNK;
    const Feature f{p.x, p.y, p.z, pine ? FeatureType::PineTree : FeatureType::OakTree, static_cast<uint8_t>(trunk)};
    const std::vector<FeatureCell> cells = feature_cells(f);
    for (const FeatureCell& c : cells) {
        const bool core = std::abs(c.dx) <= 1 && std::abs(c.dz) <= 1;
        if (!c.overwrite && !core) continue;
        const int x = p.x + c.dx, z = p.z + c.dz;
        if (!loaded(w, x, c.y, z)) return;  // also outside the world
        if (w.get(x, c.y, z) != blk::AIR && !(x == p.x && c.y == p.y && z == p.z)) return;
    }
    for (const FeatureCell& c : cells) {
        const int x = p.x + c.dx, z = p.z + c.dz;
        if (c.overwrite || w.get(x, c.y, z) == blk::AIR) w.set(x, c.y, z, c.block);
    }
}

bool water_near(const World& w, BlockPos p) {
    for (int dz = -CROP_WATER_REACH; dz <= CROP_WATER_REACH; ++dz)
        for (int dx = -CROP_WATER_REACH; dx <= CROP_WATER_REACH; ++dx)
            if (w.get(p.x + dx, p.y, p.z + dz) == blk::WATER) return true;
    return false;
}

void crop_tick(World& w, BlockPos p, uint32_t& rng) {
    const uint8_t stage = w.meta(p.x, p.y, p.z);
    if (stage >= CROP_RIPE || light_at(w, p.x, p.y, p.z) < GROW_LIGHT || !water_near(w, p)) return;
    if (next(rng) % CROP_ONE_IN != 0) return;
    w.set(p.x, p.y, p.z, blk::SUNROOT_CROP, static_cast<uint8_t>(stage + 1));
}

}  // namespace

void random_tick_block(World& w, BlockPos p, uint32_t& rng) {
    if (!loaded(w, p.x, p.y, p.z)) return;
    switch (w.get(p.x, p.y, p.z)) {
        case blk::GRASS: grass_tick(w, p, rng); break;
        case blk::SAPLING: sapling_tick(w, p, rng, false); break;
        case blk::PINE_SAPLING: sapling_tick(w, p, rng, true); break;
        case blk::SUNROOT_CROP: crop_tick(w, p, rng); break;
        default: break;
    }
}

void falling_block_update(World& w, TickScheduler& t, BlockPos p) {
    if (!loaded(w, p.x, p.y, p.z)) return;
    const BlockId b = w.get(p.x, p.y, p.z);
    if (b != blk::SAND && b != blk::GRAVEL) return;
    int y = p.y;
    while (y > 0 && !block_def(w.get(p.x, y - 1, p.z)).solid) --y;
    if (y == p.y) return;
    w.set(p.x, p.y, p.z, blk::AIR);
    w.set(p.x, y, p.z, b);
    schedule_around(t, p, FALL_DELAY);
    schedule_around(t, BlockPos{p.x, y, p.z}, FALL_DELAY);
}

void block_tick(World& w, TickScheduler& t, BlockPos p) {
    const BlockId b = w.get(p.x, p.y, p.z);
    if (b == blk::SAND || b == blk::GRAVEL) {
        falling_block_update(w, t, p);
    } else {
        fluid_update(w, t, p);
    }
}

std::vector<ItemStack> growth_drops(BlockId b, uint8_t meta, uint32_t rng) {
    const uint32_t h = mix(rng ^ DROP_SALT);
    std::vector<ItemStack> out;
    switch (b) {
        case blk::LEAVES:
            if (h % SAPLING_DROP_ONE_IN == 0) out.push_back(ItemStack{blk::SAPLING, 1, 0});
            break;
        case blk::PINE_LEAVES:
            if (h % SAPLING_DROP_ONE_IN == 0) out.push_back(ItemStack{blk::PINE_SAPLING, 1, 0});
            break;
        case blk::TALL_GRASS:
            if (h % SEED_DROP_ONE_IN == 0) out.push_back(ItemStack{itm::SUNROOT_SEEDS, 1, 0});
            break;
        case blk::SUNROOT_CROP:
            if (meta >= CROP_RIPE) {
                out.push_back(ItemStack{itm::SUNROOT, static_cast<uint8_t>(1 + (h >> 8) % 2), 0});
                out.push_back(ItemStack{itm::SUNROOT_SEEDS, static_cast<uint8_t>(1 + (h >> 16) % 3), 0});
            } else {
                out.push_back(ItemStack{itm::SUNROOT_SEEDS, 1, 0});
            }
            break;
        default: break;
    }
    return out;
}

bool is_plant_item(ItemId id) {
    return id == itm::SUNROOT_SEEDS || id == blk::SAPLING || id == blk::PINE_SAPLING;
}

std::optional<BlockId> plant_from(ItemId held, BlockId ground) {
    if (!is_plant_item(held) || (ground != blk::GRASS && ground != blk::DIRT)) return std::nullopt;
    return held == itm::SUNROOT_SEEDS ? blk::SUNROOT_CROP : static_cast<BlockId>(held);
}
