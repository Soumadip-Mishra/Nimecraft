// Item ids, tool and armor kinds, the ItemDef table, and item stacks.
// Item ids are persisted in saves (world.meta inventories, block entities): append only, never
// renumber or reuse. Ids 0..255 are block items (ItemId == BlockId); 256 and up are other items.
#pragma once

#include <cstdint>
#include <string_view>

using ItemId = uint16_t;

namespace itm {
// clang-format off
constexpr ItemId STICK = 256, COAL = 257, COPPER_INGOT = 258, TIN_INGOT = 259, BRONZE_INGOT = 260,
    IRON_INGOT = 261, SILVER_INGOT = 262, QUARTZ_SHARD = 263, RAW_MEAT = 264, COOKED_MEAT = 265,
    SMALL_MEAT = 266, COOKED_SMALL_MEAT = 267, HIDE = 268, WOOL = 269, FIBER = 270, SILK = 271,
    GLOOM_DUST = 272,
    PICK_WOOD = 273, PICK_STONE = 274, PICK_COPPER = 275, PICK_BRONZE = 276, PICK_IRON = 277,
    AXE_WOOD = 278, AXE_STONE = 279, AXE_COPPER = 280, AXE_BRONZE = 281, AXE_IRON = 282,
    SHOVEL_WOOD = 283, SHOVEL_STONE = 284, SHOVEL_COPPER = 285, SHOVEL_BRONZE = 286, SHOVEL_IRON = 287,
    SWORD_WOOD = 288, SWORD_STONE = 289, SWORD_COPPER = 290, SWORD_BRONZE = 291, SWORD_IRON = 292,
    SWORD_SILVER = 293,
    HIDE_HELMET = 294, HIDE_CHEST = 295, HIDE_LEGS = 296, HIDE_BOOTS = 297,
    IRON_HELMET = 298, IRON_CHEST = 299, IRON_LEGS = 300, IRON_BOOTS = 301,
    END = 302;
// clang-format on
}  // namespace itm

enum class ToolKind : uint8_t { None, Pick, Axe, Shovel, Sword };
enum class ArmorSlot : uint8_t { None, Head, Chest, Legs, Feet };

// Tiers: 0 hand, 1 wood, 2 stone, 3 copper, 4 bronze, 5 iron; the silver sword is tier 3.
struct ItemDef {
    std::string_view name;
    uint8_t max_stack;  // 0 for "no item" (AIR, unused and unknown ids); tools and armor 1; else 64
    ToolKind tool;
    uint8_t tier;
    uint16_t durability;  // uses before a tool breaks; 0 = never wears
    float attack;         // hand and non-tools 1
    uint8_t food;         // hunger restored when eaten; 0 = not food
    bool cooked;
    ArmorSlot armor;
    uint8_t armor_points;
    // Non-block items: their cell in the 8x8 item atlas (id - STICK). Block items: the block's
    // +X face tile in the block atlas.
    uint8_t icon;
};

// Block ids below blk::COUNT (except AIR) and item ids STICK..END-1 are items; every other id
// returns the AIR item (name "Air", max_stack 0).
const ItemDef& item_def(ItemId id);

struct ItemStack {
    ItemId id = 0;
    uint8_t count = 0;
    uint16_t damage = 0;  // tool wear, 0..durability-1
    bool empty() const;   // count == 0 or id == 0
};
