#include "inventory.hpp"

#include <algorithm>
#include <cstddef>

namespace {

constexpr int HOTBAR = 9;

}  // namespace

int Inventory::add(ItemStack s) {
    if (s.empty()) return 0;
    const int max = item_def(s.id).max_stack;
    int left = s.count;
    if (max == 0) return left;
    for (ItemStack& slot : slots) {
        if (left == 0) break;
        if (slot.empty() || slot.id != s.id || slot.damage != s.damage || slot.count >= max) continue;
        const int moved = std::min(left, max - slot.count);
        slot.count = static_cast<uint8_t>(slot.count + moved);
        left -= moved;
    }
    for (ItemStack& slot : slots) {
        if (left == 0) break;
        if (!slot.empty()) continue;
        const int moved = std::min(left, max);
        slot = ItemStack{s.id, static_cast<uint8_t>(moved), s.damage};
        left -= moved;
    }
    return left;
}

bool Inventory::remove(ItemId id, int n) {
    if (n < 0) return false;
    if (n == 0) return true;
    if (id == 0 || count(id) < n) return false;
    for (std::size_t i = slots.size(); i-- > 0 && n > 0;) {
        ItemStack& slot = slots[i];
        if (slot.empty() || slot.id != id) continue;
        const int taken = std::min(n, static_cast<int>(slot.count));
        slot.count = static_cast<uint8_t>(slot.count - taken);
        n -= taken;
        if (slot.count == 0) slot = ItemStack{};
    }
    return true;
}

int Inventory::count(ItemId id) const {
    if (id == 0) return 0;
    int total = 0;
    for (const ItemStack& slot : slots)
        if (!slot.empty() && slot.id == id) total += slot.count;
    return total;
}

ItemStack& Inventory::held() {
    return slots[static_cast<std::size_t>(std::clamp(selected, 0, HOTBAR - 1))];
}

const ItemStack& Inventory::held() const {
    return slots[static_cast<std::size_t>(std::clamp(selected, 0, HOTBAR - 1))];
}
