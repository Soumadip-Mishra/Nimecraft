#include "death.hpp"

#include "block.hpp"
#include "block_entities.hpp"
#include "player.hpp"
#include "world.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace {

bool loaded(const World& w, int x, int z) {
    return w.column_loaded(column_of(chunk_of(x, 0, z)));
}

// Neither solid nor liquid, inside the world height, in a loaded column.
bool free_cell(const World& w, int x, int y, int z) {
    if (y < 0 || y >= WORLD_HEIGHT || !loaded(w, x, z)) return false;
    const BlockDef& d = block_def(w.get(x, y, z));
    return !d.solid && d.shape != Shape::Liquid;
}

int cell_of(float v) {
    // Clamped before the cast so far-off or NaN positions stay defined.
    const double f = std::floor(static_cast<double>(v));
    if (!(f > -2.0e9)) return -2000000000;
    return f < 2.0e9 ? static_cast<int>(f) : 2000000000;
}

}  // namespace

std::optional<BlockPos> find_cairn_spot(const World& w, Vector3 feet) {
    const int x = cell_of(feet.x), z = cell_of(feet.z);
    const int y = std::clamp(cell_of(feet.y), 0, WORLD_HEIGHT - 1);
    for (int dy = 0; dy <= CAIRN_UP && y + dy < WORLD_HEIGHT; ++dy)
        if (free_cell(w, x, y + dy, z)) return BlockPos{x, y + dy, z};
    std::optional<BlockPos> best;
    int best_d2 = 0;
    for (int dz = -CAIRN_SIDE; dz <= CAIRN_SIDE; ++dz)
        for (int dx = -CAIRN_SIDE; dx <= CAIRN_SIDE; ++dx) {
            const int d2 = dx * dx + dz * dz;
            if ((best && d2 >= best_d2) || !free_cell(w, x + dx, y, z + dz)) continue;
            best = BlockPos{x + dx, y, z + dz};
            best_d2 = d2;
        }
    return best;
}

void place_cairn(World& w, BlockPos p, Inventory& inv) {
    CairnState cairn;
    for (const ItemStack& s : inv.slots)
        if (!s.empty()) cairn.items.push_back(s);
    for (const ItemStack& s : inv.armor)
        if (!s.empty()) cairn.items.push_back(s);
    if (cairn.items.empty() || !w.set(p.x, p.y, p.z, blk::GRAVE_CAIRN)) return;
    w.set_block_entity(p, std::move(cairn));
    for (ItemStack& s : inv.slots) s = ItemStack{};
    for (ItemStack& s : inv.armor) s = ItemStack{};
}

bool bedroll_usable(const World& w, BlockPos b) {
    if (w.get(b.x, b.y, b.z) != blk::BEDROLL) return false;
    for (int up = 1; up <= 2; ++up) {
        const BlockDef& d = block_def(w.get(b.x, b.y + up, b.z));
        if (d.solid || d.shape == Shape::Liquid) return false;
    }
    return true;
}

Vector3 respawn_point(const World& w, std::optional<BlockPos> bedroll) {
    if (bedroll && bedroll_usable(w, *bedroll))
        return Vector3{static_cast<float>(bedroll->x) + 0.5f, static_cast<float>(bedroll->y + 1),
                       static_cast<float>(bedroll->z) + 0.5f};
    return spawn_player(w).pos;
}

bool can_sleep(const WorldTime& t, bool hostiles_within_8) {
    return t.is_night() && !hostiles_within_8;
}

void sleep_until_dawn(WorldTime& t) {
    t.ticks = (std::floor(t.ticks / DAY_TICKS) + 1.0) * DAY_TICKS;
}
