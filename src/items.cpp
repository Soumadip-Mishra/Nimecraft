#include "items.hpp"

#include "block.hpp"

#include <array>
#include <cstddef>

namespace {

constexpr std::size_t BLOCK_ITEMS = 256;
constexpr std::size_t OTHER_ITEMS = itm::END - itm::STICK;
constexpr uint8_t STACK = 64;

constexpr ItemDef AIR_ITEM{"Air", 0, ToolKind::None, 0, 0, 1.f, 0, false, ArmorSlot::None, 0, 0};

constexpr ItemDef material(std::string_view name, uint8_t food = 0, bool cooked = false) {
    return ItemDef{name, STACK, ToolKind::None, 0, 0, 1.f, food, cooked, ArmorSlot::None, 0, 0};
}

constexpr uint16_t DURABILITY[6] = {0, 60, 130, 200, 350, 500};  // by tier, 1 wood .. 5 iron
constexpr float SWORD_ATTACK[6] = {1.f, 4.f, 5.f, 5.5f, 6.f, 7.f};
constexpr uint16_t SILVER_DURABILITY = 250;
constexpr float SILVER_ATTACK = 5.5f, OTHER_TOOL_ATTACK_DROP = 2.f;

constexpr ItemDef tool(std::string_view name, ToolKind kind, uint8_t tier) {
    const float attack = kind == ToolKind::Sword ? SWORD_ATTACK[tier] : SWORD_ATTACK[tier] - OTHER_TOOL_ATTACK_DROP;
    return ItemDef{name, 1, kind, tier, DURABILITY[tier], attack, 0, false, ArmorSlot::None, 0, 0};
}

constexpr ItemDef armor(std::string_view name, ArmorSlot slot, uint8_t points) {
    return ItemDef{name, 1, ToolKind::None, 0, 0, 1.f, 0, false, slot, points, 0};
}

// Indexed by id - STICK; entries follow the itm:: order exactly.
constexpr std::array<ItemDef, OTHER_ITEMS> make_other_items() {
    std::array<ItemDef, OTHER_ITEMS> d{{
        material("Stick"),
        material("Coal"),
        material("Copper Ingot"),
        material("Tin Ingot"),
        material("Bronze Ingot"),
        material("Iron Ingot"),
        material("Silver Ingot"),
        material("Quartz Shard"),
        material("Raw Meat", 3),
        material("Cooked Meat", 8, true),
        material("Small Meat", 2),
        material("Cooked Small Meat", 5, true),
        material("Hide"),
        material("Wool"),
        material("Fiber"),
        material("Silk"),
        material("Gloom Dust"),
        tool("Wooden Pick", ToolKind::Pick, 1),
        tool("Stone Pick", ToolKind::Pick, 2),
        tool("Copper Pick", ToolKind::Pick, 3),
        tool("Bronze Pick", ToolKind::Pick, 4),
        tool("Iron Pick", ToolKind::Pick, 5),
        tool("Wooden Axe", ToolKind::Axe, 1),
        tool("Stone Axe", ToolKind::Axe, 2),
        tool("Copper Axe", ToolKind::Axe, 3),
        tool("Bronze Axe", ToolKind::Axe, 4),
        tool("Iron Axe", ToolKind::Axe, 5),
        tool("Wooden Shovel", ToolKind::Shovel, 1),
        tool("Stone Shovel", ToolKind::Shovel, 2),
        tool("Copper Shovel", ToolKind::Shovel, 3),
        tool("Bronze Shovel", ToolKind::Shovel, 4),
        tool("Iron Shovel", ToolKind::Shovel, 5),
        tool("Wooden Sword", ToolKind::Sword, 1),
        tool("Stone Sword", ToolKind::Sword, 2),
        tool("Copper Sword", ToolKind::Sword, 3),
        tool("Bronze Sword", ToolKind::Sword, 4),
        tool("Iron Sword", ToolKind::Sword, 5),
        ItemDef{"Silver Sword", 1, ToolKind::Sword, 3, SILVER_DURABILITY, SILVER_ATTACK, 0, false, ArmorSlot::None, 0, 0},
        armor("Hide Cap", ArmorSlot::Head, 1),
        armor("Hide Vest", ArmorSlot::Chest, 3),
        armor("Hide Leggings", ArmorSlot::Legs, 2),
        armor("Hide Boots", ArmorSlot::Feet, 1),
        armor("Iron Helmet", ArmorSlot::Head, 2),
        armor("Iron Chestplate", ArmorSlot::Chest, 6),
        armor("Iron Leggings", ArmorSlot::Legs, 5),
        armor("Iron Boots", ArmorSlot::Feet, 2),
    }};
    for (std::size_t i = 0; i < d.size(); ++i) d[i].icon = static_cast<uint8_t>(i);
    return d;
}

constexpr std::array<ItemDef, OTHER_ITEMS> OTHER = make_other_items();

std::array<ItemDef, BLOCK_ITEMS> make_block_items() {
    std::array<ItemDef, BLOCK_ITEMS> d;
    d.fill(AIR_ITEM);
    for (BlockId b = 1; b < blk::COUNT; ++b) {
        const BlockDef& def = block_def(b);
        d[b] = material(def.name);
        d[b].icon = static_cast<uint8_t>(def.tiles[PX]);
    }
    return d;
}

}  // namespace

const ItemDef& item_def(ItemId id) {
    static const std::array<ItemDef, BLOCK_ITEMS> blocks = make_block_items();  // thread-safe init
    if (id < BLOCK_ITEMS) return id < blk::COUNT ? blocks[id] : blocks[0];
    if (id >= itm::STICK && id < itm::END) return OTHER[id - itm::STICK];
    return blocks[0];
}

bool ItemStack::empty() const {
    return count == 0 || id == 0;
}
