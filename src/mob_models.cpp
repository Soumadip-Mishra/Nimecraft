#include "mob_models.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace {

constexpr float SWING_RATE = 6.f, SWING_AMP = 0.6f, HURT_KEEP = 0.4f;
constexpr float PI_F = 3.14159265f;

// clang-format off
const std::array<std::vector<BoxPart>, static_cast<std::size_t>(MobType::COUNT)> MODELS = {{
    // Tusker: a low, heavy body, a head with tusks, four legs.
    {{{0.f, 0.56f, 0.f}, {0.85f, 0.46f, 0.6f}, MT_TUSKER_HIDE, false, 0.f},
     {{0.52f, 0.6f, 0.f}, {0.34f, 0.34f, 0.4f}, MT_TUSKER_FACE, false, 0.f},
     {{0.7f, 0.46f, 0.f}, {0.08f, 0.08f, 0.34f}, MT_BONE, false, 0.f},
     {{0.28f, 0.165f, 0.18f}, {0.18f, 0.33f, 0.18f}, MT_TUSKER_HIDE, true, 0.f},
     {{0.28f, 0.165f, -0.18f}, {0.18f, 0.33f, 0.18f}, MT_TUSKER_HIDE, true, PI_F},
     {{-0.28f, 0.165f, 0.18f}, {0.18f, 0.33f, 0.18f}, MT_TUSKER_HIDE, true, PI_F},
     {{-0.28f, 0.165f, -0.18f}, {0.18f, 0.33f, 0.18f}, MT_TUSKER_HIDE, true, 0.f}},
    // Fluffhorn: a woolly body on long legs, a head with curled horns.
    {{{0.f, 0.82f, 0.f}, {0.85f, 0.55f, 0.65f}, MT_WOOL, false, 0.f},
     {{0.5f, 1.05f, 0.f}, {0.3f, 0.34f, 0.3f}, MT_FLUFF_FACE, false, 0.f},
     {{0.48f, 1.27f, 0.f}, {0.14f, 0.1f, 0.46f}, MT_HORN, false, 0.f},
     {{0.28f, 0.275f, 0.2f}, {0.16f, 0.55f, 0.16f}, MT_FLUFF_FACE, true, 0.f},
     {{0.28f, 0.275f, -0.2f}, {0.16f, 0.55f, 0.16f}, MT_FLUFF_FACE, true, PI_F},
     {{-0.28f, 0.275f, 0.2f}, {0.16f, 0.55f, 0.16f}, MT_FLUFF_FACE, true, PI_F},
     {{-0.28f, 0.275f, -0.2f}, {0.16f, 0.55f, 0.16f}, MT_FLUFF_FACE, true, 0.f}},
    // Hopper: a small round body, a head with tall ears, strong hind legs.
    {{{0.f, 0.2f, 0.f}, {0.36f, 0.26f, 0.3f}, MT_HOPPER_FUR, false, 0.f},
     {{0.16f, 0.34f, 0.f}, {0.2f, 0.2f, 0.2f}, MT_HOPPER_FACE, false, 0.f},
     {{0.12f, 0.5f, 0.05f}, {0.05f, 0.14f, 0.04f}, MT_HOPPER_FUR, false, 0.f},
     {{0.12f, 0.5f, -0.05f}, {0.05f, 0.14f, 0.04f}, MT_HOPPER_FUR, false, 0.f},
     {{-0.1f, 0.05f, 0.1f}, {0.14f, 0.1f, 0.08f}, MT_HOPPER_FUR, true, 0.f},
     {{-0.1f, 0.05f, -0.1f}, {0.14f, 0.1f, 0.08f}, MT_HOPPER_FUR, true, 0.f}},
    // Gloomwalker: a tall, thin walker with long arms.
    {{{0.f, 0.4f, 0.1f}, {0.18f, 0.8f, 0.18f}, MT_GLOOM_BODY, true, 0.f},
     {{0.f, 0.4f, -0.1f}, {0.18f, 0.8f, 0.18f}, MT_GLOOM_BODY, true, PI_F},
     {{0.f, 1.15f, 0.f}, {0.25f, 0.7f, 0.5f}, MT_GLOOM_BODY, false, 0.f},
     {{0.f, 1.7f, 0.f}, {0.36f, 0.4f, 0.36f}, MT_GLOOM_FACE, false, 0.f},
     {{0.f, 1.15f, 0.33f}, {0.14f, 0.72f, 0.14f}, MT_GLOOM_BODY, true, PI_F},
     {{0.f, 1.15f, -0.33f}, {0.14f, 0.72f, 0.14f}, MT_GLOOM_BODY, true, 0.f}},
    // Thornspitter: a stem on root feet, a bulb head with a spout.
    {{{0.f, 0.52f, 0.f}, {0.36f, 0.72f, 0.36f}, MT_THORN_STEM, false, 0.f},
     {{0.f, 1.0f, 0.f}, {0.56f, 0.4f, 0.56f}, MT_THORN_BULB, false, 0.f},
     {{0.33f, 1.0f, 0.f}, {0.2f, 0.14f, 0.14f}, MT_THORN_STEM, false, 0.f},
     {{0.15f, 0.08f, 0.15f}, {0.12f, 0.16f, 0.12f}, MT_THORN_STEM, true, 0.f},
     {{0.15f, 0.08f, -0.15f}, {0.12f, 0.16f, 0.12f}, MT_THORN_STEM, true, PI_F},
     {{-0.15f, 0.08f, 0.15f}, {0.12f, 0.16f, 0.12f}, MT_THORN_STEM, true, PI_F},
     {{-0.15f, 0.08f, -0.15f}, {0.12f, 0.16f, 0.12f}, MT_THORN_STEM, true, 0.f}},
    // Cave Crawler: a flat shell, a head, four splayed legs.
    {{{0.f, 0.26f, 0.f}, {0.5f, 0.22f, 0.4f}, MT_CRAWLER_SHELL, false, 0.f},
     {{0.32f, 0.24f, 0.f}, {0.18f, 0.18f, 0.24f}, MT_CRAWLER_FACE, false, 0.f},
     {{0.14f, 0.1f, 0.28f}, {0.08f, 0.2f, 0.26f}, MT_CRAWLER_SHELL, true, 0.f},
     {{0.14f, 0.1f, -0.28f}, {0.08f, 0.2f, 0.26f}, MT_CRAWLER_SHELL, true, PI_F},
     {{-0.14f, 0.1f, 0.28f}, {0.08f, 0.2f, 0.26f}, MT_CRAWLER_SHELL, true, PI_F},
     {{-0.14f, 0.1f, -0.28f}, {0.08f, 0.2f, 0.26f}, MT_CRAWLER_SHELL, true, 0.f}},
}};

// Base color per tile, and whether it is a face (eyes drawn on it).
struct TileLook {
    Color base;
    bool face;
};
constexpr std::array<TileLook, MOB_ATLAS_TILES * MOB_ATLAS_TILES> LOOKS = {{
    {{122, 84, 58, 255}, false},  {{140, 98, 70, 255}, true},  {{232, 224, 200, 255}, false},
    {{236, 232, 222, 255}, false}, {{96, 82, 74, 255}, true},  {{170, 150, 110, 255}, false},
    {{176, 150, 112, 255}, false}, {{190, 164, 126, 255}, true}, {{38, 34, 54, 255}, false},
    {{30, 26, 44, 255}, true},    {{70, 120, 50, 255}, false}, {{120, 60, 90, 255}, true},
    {{64, 58, 52, 255}, false},   {{50, 44, 40, 255}, true},   {{150, 170, 60, 255}, false},
    {{255, 0, 255, 255}, false},
}};
// clang-format on

uint32_t speckle(int x, int y) {
    uint32_t h = static_cast<uint32_t>(x) * 374761393u + static_cast<uint32_t>(y) * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

unsigned char shade(unsigned char c, int delta) {
    return static_cast<unsigned char>(std::clamp(static_cast<int>(c) + delta, 0, 255));
}

}  // namespace

const std::vector<BoxPart>& mob_model(MobType t) {
    const auto i = static_cast<std::size_t>(t);
    return i < MODELS.size() ? MODELS[i] : MODELS[0];
}

std::vector<Color> mob_atlas_pixels() {
    std::vector<Color> px(static_cast<std::size_t>(MOB_ATLAS_PX * MOB_ATLAS_PX));
    for (int y = 0; y < MOB_ATLAS_PX; ++y)
        for (int x = 0; x < MOB_ATLAS_PX; ++x) {
            const int tile = y / MOB_TILE_PX * MOB_ATLAS_TILES + x / MOB_TILE_PX;
            const TileLook& look = LOOKS[static_cast<std::size_t>(tile)];
            const int d = static_cast<int>(speckle(x, y) % 25u) - 12;
            Color c{shade(look.base.r, d), shade(look.base.g, d), shade(look.base.b, d), 255};
            const int lx = x % MOB_TILE_PX, ly = y % MOB_TILE_PX;
            const bool eye = look.face && ly >= 5 && ly <= 7 && ((lx >= 3 && lx <= 5) || (lx >= 10 && lx <= 12));
            if (eye) c = tile == MT_GLOOM_FACE ? Color{150, 230, 255, 255} : Color{20, 20, 20, 255};
            px[static_cast<std::size_t>(y * MOB_ATLAS_PX + x)] = c;
        }
    return px;
}

float part_swing(const BoxPart& p, float walk_distance) {
    return p.swings ? std::sin(walk_distance * SWING_RATE + p.phase) * SWING_AMP : 0.f;
}

Color mob_tint(const Mob& m, Color light) {
    Color c = light;
    if (m.hurt > 0.f) {
        c.g = static_cast<unsigned char>(static_cast<float>(c.g) * HURT_KEEP);
        c.b = static_cast<unsigned char>(static_cast<float>(c.b) * HURT_KEEP);
    }
    if (m.state == AiState::Fade) {
        const float f = std::clamp(m.health / mob_def(m.type).health, 0.f, 1.f);
        c.a = static_cast<unsigned char>(std::lround(255.f * f));
    }
    return c;
}
