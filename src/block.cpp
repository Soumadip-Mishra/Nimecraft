#include "block.hpp"

namespace {

constexpr std::array<Tile, 6> all(Tile t) {
    return {t, t, t, t, t, t};
}

// Face order: PX, NX, PY, NY, PZ, NZ.
constexpr std::array<Tile, 6> top_bottom_side(Tile top, Tile bottom, Tile side) {
    return {side, side, top, bottom, side, side};
}

// Indexed by BlockId; entries follow the blk:: order exactly. Mining columns are set by with_mining.
constexpr std::array<BlockDef, blk::COUNT> BASE{{
    {"Air", false, false, true, all(T_STONE), Layer::Opaque, Shape::Cube, 0, 0},  // tiles unused: never meshed
    {"Grass", true, true, true, top_bottom_side(T_GRASS_TOP, T_DIRT, T_GRASS_SIDE)},
    {"Dirt", true, true, true, all(T_DIRT)},
    {"Stone", true, true, true, all(T_STONE)},
    {"Sand", true, true, true, all(T_SAND)},
    {"Log", true, true, true, top_bottom_side(T_LOG_TOP, T_LOG_TOP, T_LOG_SIDE)},
    {"Leaves", true, false, true, all(T_LEAVES), Layer::Cutout, Shape::Cube, 0, 1},
    {"Planks", true, true, true, all(T_PLANKS)},
    {"Deepstone", true, true, false, all(T_DEEPSTONE)},
    {"Water", false, false, false, all(T_WATER), Layer::Translucent, Shape::Liquid, 0, 2},
    {"Gravel", true, true, true, all(T_GRAVEL)},
    {"Clay", true, true, true, all(T_CLAY)},
    {"Snow", true, true, true, top_bottom_side(T_SNOW, T_DIRT, T_SNOW_SIDE)},
    {"Ice", true, false, true, all(T_ICE), Layer::Translucent, Shape::Cube, 0, 2},
    {"Sandstone", true, true, true, top_bottom_side(T_SANDSTONE_TOP, T_SANDSTONE_TOP, T_SANDSTONE_SIDE)},
    {"Cactus", true, true, true, top_bottom_side(T_CACTUS_TOP, T_CACTUS_TOP, T_CACTUS_SIDE)},
    {"Pine Log", true, true, true, top_bottom_side(T_PINE_LOG_TOP, T_PINE_LOG_TOP, T_PINE_LOG_SIDE)},
    {"Pine Leaves", true, false, true, all(T_PINE_LEAVES), Layer::Cutout, Shape::Cube, 0, 1},
    {"Tall Grass", false, false, true, all(T_TALL_GRASS), Layer::Cutout, Shape::Cross, 0, 0},
    {"Flower", false, false, true, all(T_FLOWER), Layer::Cutout, Shape::Cross, 0, 0},
    {"Coal Ore", true, true, true, all(T_COAL_ORE)},
    {"Copper Ore", true, true, true, all(T_COPPER_ORE)},
    {"Tin Ore", true, true, true, all(T_TIN_ORE)},
    {"Iron Ore", true, true, true, all(T_IRON_ORE)},
    {"Silver Ore", true, true, true, all(T_SILVER_ORE)},
    {"Quartz", true, true, true, all(T_QUARTZ)},
    {"Torch", false, false, true, all(T_TORCH), Layer::Cutout, Shape::Cross, 14, 0},
    {"Lantern", true, false, true, all(T_LANTERN), Layer::Cutout, Shape::Cube, 15, 0},
    {"Glowcap", false, false, true, all(T_GLOWCAP), Layer::Cutout, Shape::Cross, 7, 0},
    {"Workbench", true, true, true, top_bottom_side(T_WORKBENCH_TOP, T_PLANKS, T_WORKBENCH_SIDE)},
    {"Kiln", true, true, true, top_bottom_side(T_KILN_TOP, T_KILN_TOP, T_KILN_SIDE)},
    {"Crate", true, true, true, top_bottom_side(T_CRATE_TOP, T_CRATE_TOP, T_CRATE_SIDE)},
    {"Glass", true, false, true, all(T_GLASS), Layer::Translucent, Shape::Cube, 0, 0},
    {"Bedroll", true, true, true, top_bottom_side(T_BEDROLL_TOP, T_PLANKS, T_BEDROLL_SIDE)},
    {"Grave Cairn", true, true, true, top_bottom_side(T_CAIRN_TOP, T_CAIRN_TOP, T_CAIRN_SIDE)},
    {"Sapling", false, false, true, all(T_SAPLING), Layer::Cutout, Shape::Cross, 0, 0},
    {"Pine Sapling", false, false, true, all(T_PINE_SAPLING), Layer::Cutout, Shape::Cross, 0, 0},
    // Its tiles show stage 0; the mesher picks T_SUNROOT_0 + stage (see mesher.hpp).
    {"Sunroot Crop", false, false, true, all(T_SUNROOT_0), Layer::Cutout, Shape::Cross, 0, 0},
}};

// Hardness, tool, min tier, and drops (M4 Task 3). Blocks the task's table leaves out use the
// values the user approved on 2026-11-08 (PINE_LEAVES as LEAVES, plants and torches instant,
// CACTUS 0.4, LANTERN 1.5 with a tier-1 pick).
constexpr std::array<BlockDef, blk::COUNT> with_mining(std::array<BlockDef, blk::COUNT> d) {
    for (BlockId id = 0; id < blk::COUNT; ++id) d[id].drop = id;  // a block drops itself by default
    d[blk::AIR].drop = 0;
    d[blk::AIR].drop_min = d[blk::AIR].drop_max = 0;
    auto mine = [&d](BlockId id, float hardness, ToolKind tool, uint8_t min_tier) {
        d[id].hardness = hardness;
        d[id].tool = tool;
        d[id].min_tier = min_tier;
    };
    auto drop = [&d](BlockId id, ItemId item, uint8_t lo, uint8_t hi, uint8_t chance = 100) {
        d[id].drop = item;
        d[id].drop_min = lo;
        d[id].drop_max = hi;
        d[id].drop_chance = chance;
    };
    using enum ToolKind;
    for (BlockId id : {blk::DIRT, blk::SAND, blk::GRAVEL, blk::SNOW}) mine(id, 0.5f, Shovel, 0);
    mine(blk::GRASS, 0.6f, Shovel, 0);
    mine(blk::CLAY, 0.6f, Shovel, 0);
    for (BlockId id : {blk::LOG, blk::PINE_LOG, blk::PLANKS, blk::WORKBENCH, blk::CRATE}) mine(id, 2.f, Axe, 0);
    mine(blk::LEAVES, 0.2f, None, 0);
    mine(blk::PINE_LEAVES, 0.2f, None, 0);
    mine(blk::STONE, 1.5f, Pick, 1);
    mine(blk::SANDSTONE, 1.5f, Pick, 1);
    mine(blk::KILN, 3.5f, Pick, 1);
    mine(blk::COAL_ORE, 3.f, Pick, 1);
    mine(blk::COPPER_ORE, 3.f, Pick, 2);
    mine(blk::TIN_ORE, 3.f, Pick, 2);
    mine(blk::IRON_ORE, 3.f, Pick, 4);
    mine(blk::SILVER_ORE, 3.f, Pick, 5);
    mine(blk::QUARTZ, 3.f, Pick, 3);
    mine(blk::GLASS, 0.3f, None, 0);
    mine(blk::ICE, 0.5f, Pick, 0);
    mine(blk::CACTUS, 0.4f, None, 0);
    mine(blk::LANTERN, 1.5f, Pick, 1);
    for (BlockId id : {blk::TALL_GRASS, blk::FLOWER, blk::TORCH, blk::GLOWCAP}) mine(id, 0.f, None, 0);
    mine(blk::BEDROLL, 0.4f, None, 0);      // M5
    mine(blk::GRAVE_CAIRN, 0.5f, None, 0);  // M5: by hand, so its items always drop
    for (BlockId id : {blk::SAPLING, blk::PINE_SAPLING, blk::SUNROOT_CROP}) mine(id, 0.f, None, 0);  // M7: plants

    drop(blk::GRASS, blk::DIRT, 1, 1);
    drop(blk::COAL_ORE, itm::COAL, 1, 1);
    drop(blk::QUARTZ, itm::QUARTZ_SHARD, 1, 3);
    drop(blk::LEAVES, itm::STICK, 1, 1, 10);
    drop(blk::PINE_LEAVES, itm::STICK, 1, 1, 10);
    drop(blk::TALL_GRASS, itm::FIBER, 1, 1, 10);
    drop(blk::GLASS, 0, 0, 0);
    drop(blk::ICE, 0, 0, 0);
    drop(blk::GRAVE_CAIRN, 0, 0, 0);  // its stored items drop instead (block_entity_contents)
    drop(blk::SUNROOT_CROP, 0, 0, 0);  // seeds and roots by stage instead (growth_drops)
    return d;
}

constexpr std::array<BlockDef, blk::COUNT> DEFS = with_mining(BASE);

}  // namespace

const BlockDef& block_def(BlockId id) {
    return DEFS[id < blk::COUNT ? id : blk::AIR];
}
