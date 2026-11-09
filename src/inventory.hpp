// The player's 36 slots (0-8 = hotbar), 4 armor slots, and the selected hotbar slot.
#pragma once

#include "items.hpp"

#include <array>

class Inventory {
public:
    std::array<ItemStack, 36> slots;
    std::array<ItemStack, 4> armor;  // ArmorSlot Head, Chest, Legs, Feet in that order
    int selected = 0;                // hotbar slot 0..8

    // Merges s into existing stacks of the same id and damage (slot order), then fills empty
    // slots, hotbar first, up to the item's max_stack each. Returns the count that did not fit:
    // 0 for an empty stack, the full count for an id that is no item (max_stack 0).
    int add(ItemStack s);
    // Takes count of id from the slots (inventory slots 35..9 first, then the hotbar), or nothing
    // and false when fewer are present. count 0 is true and changes nothing; negative is false.
    bool remove(ItemId id, int count);
    int count(ItemId id) const;  // summed over the 36 slots (armor not counted)
    ItemStack& held();           // slots[selected], selected clamped to 0..8
    const ItemStack& held() const;
};
