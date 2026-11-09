#include "recipes.hpp"

#include "block.hpp"
#include "world.hpp"

#include <cmath>
#include <cstddef>
#include <initializer_list>

namespace {

ItemStack s(ItemId id, int count) {
    return ItemStack{id, static_cast<uint8_t>(count), 0};
}

std::vector<Recipe> build() {
    std::vector<Recipe> r;
    auto add = [&r](std::initializer_list<ItemStack> in, ItemStack out, Station st) {
        r.push_back(Recipe{std::vector<ItemStack>(in), out, st});
    };
    using enum Station;
    // By hand.
    add({s(blk::LOG, 1)}, s(blk::PLANKS, 4), None);
    add({s(blk::PINE_LOG, 1)}, s(blk::PLANKS, 4), None);
    add({s(blk::PLANKS, 2)}, s(itm::STICK, 4), None);
    add({s(blk::PLANKS, 4)}, s(blk::WORKBENCH, 1), None);
    add({s(itm::COAL, 1), s(itm::STICK, 1)}, s(blk::TORCH, 4), None);

    // At a workbench.
    add({s(blk::STONE, 8)}, s(blk::KILN, 1), Workbench);
    add({s(blk::PLANKS, 8)}, s(blk::CRATE, 1), Workbench);
    const ItemId heads[] = {blk::PLANKS, blk::STONE, itm::COPPER_INGOT, itm::BRONZE_INGOT, itm::IRON_INGOT};
    for (int t = 0; t < 5; ++t) {
        const ItemId head = heads[t];
        const auto tier = static_cast<ItemId>(t);
        add({s(head, 3), s(itm::STICK, 2)}, s(static_cast<ItemId>(itm::PICK_WOOD + tier), 1), Workbench);
        add({s(head, 3), s(itm::STICK, 2)}, s(static_cast<ItemId>(itm::AXE_WOOD + tier), 1), Workbench);
        add({s(head, 1), s(itm::STICK, 2)}, s(static_cast<ItemId>(itm::SHOVEL_WOOD + tier), 1), Workbench);
        add({s(head, 2), s(itm::STICK, 1)}, s(static_cast<ItemId>(itm::SWORD_WOOD + tier), 1), Workbench);
    }
    add({s(itm::SILVER_INGOT, 2), s(itm::QUARTZ_SHARD, 1), s(itm::STICK, 1)}, s(itm::SWORD_SILVER, 1), Workbench);
    add({s(itm::IRON_INGOT, 1), s(blk::TORCH, 1), s(blk::GLASS, 1)}, s(blk::LANTERN, 1), Workbench);
    const int armor_cost[] = {5, 8, 7, 4};  // helmet, chest, legs, boots
    for (int k = 0; k < 4; ++k) {
        const auto piece = static_cast<ItemId>(k);
        add({s(itm::HIDE, armor_cost[k])}, s(static_cast<ItemId>(itm::HIDE_HELMET + piece), 1), Workbench);
        add({s(itm::IRON_INGOT, armor_cost[k])}, s(static_cast<ItemId>(itm::IRON_HELMET + piece), 1), Workbench);
    }
    return r;
}

bool has_inputs(const Inventory& inv, const Recipe& r) {
    for (const ItemStack& in : r.inputs)
        if (inv.count(in.id) < in.count) return false;
    return true;
}

}  // namespace

const std::vector<Recipe>& recipes() {
    static const std::vector<Recipe> table = build();  // thread-safe init
    return table;
}

StationSet stations_near(const World& w, Vector3 pos, float radius) {
    StationSet found;
    if (!(radius >= 0.f) || !std::isfinite(radius) || !std::isfinite(pos.x) || !std::isfinite(pos.y) ||
        !std::isfinite(pos.z))
        return found;
    const double r = radius, r2 = r * r;
    const int x0 = static_cast<int>(std::floor(pos.x - r)), x1 = static_cast<int>(std::floor(pos.x + r));
    const int y0 = static_cast<int>(std::floor(pos.y - r)), y1 = static_cast<int>(std::floor(pos.y + r));
    const int z0 = static_cast<int>(std::floor(pos.z - r)), z1 = static_cast<int>(std::floor(pos.z + r));
    for (int y = y0; y <= y1; ++y)
        for (int z = z0; z <= z1; ++z)
            for (int x = x0; x <= x1; ++x) {
                const BlockId b = w.get(x, y, z);
                const Station st = b == blk::WORKBENCH ? Station::Workbench
                                                       : (b == blk::KILN ? Station::Kiln : Station::None);
                if (st == Station::None) continue;
                const double dx = x + 0.5 - pos.x, dy = y + 0.5 - pos.y, dz = z + 0.5 - pos.z;
                if (dx * dx + dy * dy + dz * dz <= r2) found.set(static_cast<std::size_t>(st));
            }
    return found;
}

std::vector<const Recipe*> available(const Inventory& inv, StationSet near) {
    std::vector<const Recipe*> out;
    for (const Recipe& r : recipes()) {
        if (r.station != Station::None && !near.test(static_cast<std::size_t>(r.station))) continue;
        if (has_inputs(inv, r)) out.push_back(&r);
    }
    return out;
}

bool craft(Inventory& inv, const Recipe& r) {
    if (!has_inputs(inv, r)) return false;
    Inventory next = inv;
    for (const ItemStack& in : r.inputs) next.remove(in.id, in.count);
    if (next.add(r.output) != 0) return false;
    inv = next;
    return true;
}
