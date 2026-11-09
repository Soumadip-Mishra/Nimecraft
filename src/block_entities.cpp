#include "block_entities.hpp"

#include "block.hpp"

#include <algorithm>
#include <cstddef>

namespace {

constexpr float DONE_EPS = 1e-4f;  // progress this close to SMELT_SECONDS counts as done

ItemStack s(ItemId id, int count) {
    return ItemStack{id, static_cast<uint8_t>(count), 0};
}

std::vector<SmeltRecipe> build() {
    return {
        {{s(itm::COPPER_INGOT, 3), s(itm::TIN_INGOT, 1)}, s(itm::BRONZE_INGOT, 4)},
        {{s(blk::COPPER_ORE, 1), {}}, s(itm::COPPER_INGOT, 1)},
        {{s(blk::TIN_ORE, 1), {}}, s(itm::TIN_INGOT, 1)},
        {{s(blk::IRON_ORE, 1), {}}, s(itm::IRON_INGOT, 1)},
        {{s(blk::SILVER_ORE, 1), {}}, s(itm::SILVER_INGOT, 1)},
        {{s(blk::SAND, 1), {}}, s(blk::GLASS, 1)},
        {{s(itm::RAW_MEAT, 1), {}}, s(itm::COOKED_MEAT, 1)},
        {{s(itm::SMALL_MEAT, 1), {}}, s(itm::COOKED_SMALL_MEAT, 1)},
    };
}

bool holds(const ItemStack& slot, const ItemStack& need) {
    return !slot.empty() && slot.id == need.id && slot.count >= need.count;
}

// A recipe the slots hold, and which slot feeds each recipe input (-1 for none).
struct Match {
    const SmeltRecipe* recipe = nullptr;
    int slot_for[2] = {-1, -1};
};

Match find_match(const KilnState& k) {
    for (const SmeltRecipe& r : smelt_recipes()) {
        if (r.inputs[1].empty()) {
            for (int i = 0; i < 2; ++i)
                if (holds(k.in[static_cast<std::size_t>(i)], r.inputs[0])) return Match{&r, {i, -1}};
            continue;
        }
        if (holds(k.in[0], r.inputs[0]) && holds(k.in[1], r.inputs[1])) return Match{&r, {0, 1}};
        if (holds(k.in[1], r.inputs[0]) && holds(k.in[0], r.inputs[1])) return Match{&r, {1, 0}};
    }
    return Match{};
}

bool output_fits(const ItemStack& out, const ItemStack& add) {
    if (out.empty()) return true;
    return out.id == add.id && out.damage == add.damage && out.count + add.count <= item_def(out.id).max_stack;
}

void take(ItemStack& slot, int count) {
    slot.count = static_cast<uint8_t>(slot.count - count);
    if (slot.count == 0) slot = ItemStack{};
}

}  // namespace

const std::vector<SmeltRecipe>& smelt_recipes() {
    static const std::vector<SmeltRecipe> table = build();  // thread-safe init
    return table;
}

float fuel_seconds(ItemId id) {
    switch (id) {
    case itm::COAL: return 80.f;
    case blk::LOG:
    case blk::PINE_LOG: return 15.f;
    case blk::PLANKS: return 7.5f;
    case itm::STICK: return 2.5f;
    default: return 0.f;
    }
}

void kiln_tick(KilnState& k, float dt) {
    if (!(dt > 0.f)) return;
    while (dt > 0.f) {
        const Match m = find_match(k);
        if (m.recipe == nullptr || !output_fits(k.out, m.recipe->output)) {
            k.progress = 0.f;
            return;
        }
        if (k.burn_left <= 0.f) {
            const float f = k.fuel.empty() ? 0.f : fuel_seconds(k.fuel.id);
            if (f <= 0.f) return;  // no fuel: the smelt waits where it is
            take(k.fuel, 1);
            k.burn_left = k.burn_total = f;
        }
        const float step = std::min({dt, k.burn_left, SMELT_SECONDS - k.progress});
        k.burn_left -= step;
        k.progress += step;
        dt -= step;
        if (k.burn_left < DONE_EPS) k.burn_left = 0.f;
        if (k.progress >= SMELT_SECONDS - DONE_EPS) {
            k.progress = 0.f;
            for (int i = 0; i < 2; ++i)
                if (m.slot_for[i] >= 0)
                    take(k.in[static_cast<std::size_t>(m.slot_for[i])], m.recipe->inputs[static_cast<std::size_t>(i)].count);
            if (k.out.empty()) k.out = m.recipe->output;
            else k.out.count = static_cast<uint8_t>(k.out.count + m.recipe->output.count);
        }
    }
}

std::vector<ItemStack> block_entity_contents(const BlockEntity& be) {
    std::vector<ItemStack> out;
    auto keep = [&out](const ItemStack& st) {
        if (!st.empty()) out.push_back(st);
    };
    if (const auto* k = std::get_if<KilnState>(&be)) {
        keep(k->in[0]);
        keep(k->in[1]);
        keep(k->fuel);
        keep(k->out);
    } else if (const auto* c = std::get_if<CrateState>(&be)) {
        for (const ItemStack& st : c->slots) keep(st);
    } else {
        for (const ItemStack& st : std::get<CairnState>(be).items) keep(st);
    }
    return out;
}
