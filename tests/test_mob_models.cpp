// Tests for src/mob_models.*: box models, the mob atlas, the leg swing, and the hurt and fade tints.
#include "mob_models.hpp"
#include "mobs.hpp"
#include "test.hpp"

#include <cmath>
#include <cstddef>
#include <set>
#include <vector>

TEST(test_mob_models_valid) {
    const MobType types[] = {MobType::Tusker,      MobType::Fluffhorn,    MobType::Hopper,
                             MobType::Gloomwalker, MobType::Thornspitter, MobType::CaveCrawler};
    std::set<const std::vector<BoxPart>*> distinct;
    for (MobType t : types) {
        const std::vector<BoxPart>& parts = mob_model(t);
        distinct.insert(&parts);
        CHECK(parts.size() >= 4 && parts.size() <= 8);
        int swinging = 0;
        for (const BoxPart& p : parts) {
            CHECK(p.size.x > 0.f && p.size.y > 0.f && p.size.z > 0.f);
            CHECK(p.tile < MOB_ATLAS_TILES * MOB_ATLAS_TILES);
            CHECK(std::isfinite(p.offset.x) && std::isfinite(p.offset.y) && std::isfinite(p.offset.z));
            CHECK(p.offset.y - p.size.y / 2 >= -1e-4f);  // nothing below the feet
            swinging += p.swings ? 1 : 0;
        }
        CHECK(swinging >= 2);  // legs (or roots) that swing
    }
    CHECK(distinct.size() == 6);
    // An unknown type falls back to the Tusker model.
    CHECK(&mob_model(MobType::COUNT) == &mob_model(MobType::Tusker));
}

TEST(test_mob_atlas_pixels) {
    const std::vector<Color> a = mob_atlas_pixels(), b = mob_atlas_pixels();
    CHECK(a.size() == static_cast<std::size_t>(MOB_ATLAS_PX * MOB_ATLAS_PX));
    CHECK(MOB_ATLAS_PX == MOB_ATLAS_TILES * MOB_TILE_PX);
    bool same = a.size() == b.size(), opaque = true;
    for (std::size_t i = 0; i < a.size() && i < b.size(); ++i) {
        same = same && a[i].r == b[i].r && a[i].g == b[i].g && a[i].b == b[i].b && a[i].a == b[i].a;
        opaque = opaque && a[i].a == 255;
    }
    CHECK(same);    // deterministic
    CHECK(opaque);
    // Every tile has some texture: not one flat color.
    for (int t = 0; t < MOB_ATLAS_TILES * MOB_ATLAS_TILES; ++t) {
        const int x0 = t % MOB_ATLAS_TILES * MOB_TILE_PX, y0 = t / MOB_ATLAS_TILES * MOB_TILE_PX;
        std::set<int> colors;
        for (int y = 0; y < MOB_TILE_PX; ++y)
            for (int x = 0; x < MOB_TILE_PX; ++x) {
                const Color c = a[static_cast<std::size_t>((y0 + y) * MOB_ATLAS_PX + x0 + x)];
                colors.insert(c.r << 16 | c.g << 8 | c.b);
            }
        CHECK(colors.size() >= 2);
    }
}

TEST(test_mob_part_swing) {
    const BoxPart leg{Vector3{0.f, 0.2f, 0.f}, Vector3{0.1f, 0.4f, 0.1f}, 0, true, 0.f};
    const BoxPart back{Vector3{0.f, 0.2f, 0.f}, Vector3{0.1f, 0.4f, 0.1f}, 0, true, 3.14159265f};
    const BoxPart body{Vector3{0.f, 0.5f, 0.f}, Vector3{0.5f, 0.3f, 0.5f}, 0, false, 0.f};
    CHECK(part_swing(leg, 0.f) == 0.f);
    CHECK_NEAR(part_swing(leg, 0.2617994f), 0.6, 1e-4);  // sin(pi/2) x 0.6
    CHECK_NEAR(part_swing(back, 0.2617994f), -0.6, 1e-4);
    CHECK(part_swing(body, 0.2617994f) == 0.f);
    for (float w = 0.f; w < 20.f; w += 0.37f) CHECK(std::fabs(part_swing(leg, w)) <= 0.6f + 1e-6f);
}

TEST(test_mob_tint) {
    Mob m{};
    m.type = MobType::Tusker;
    m.health = 10.f;
    const Color light{200, 200, 200, 255};
    Color c = mob_tint(m, light);
    CHECK(c.r == 200 && c.g == 200 && c.b == 200 && c.a == 255);
    m.hurt = 0.2f;  // red while hurt
    c = mob_tint(m, light);
    CHECK(c.r == 200 && c.g < 120 && c.b < 120 && c.a == 255);
    m.hurt = 0.f;
    m.type = MobType::Gloomwalker;
    m.health = 10.f;
    c = mob_tint(m, light);
    CHECK(c.a == 255);  // not fading
    m.state = AiState::Fade;
    c = mob_tint(m, light);
    CHECK(c.a == 127 || c.a == 128);  // half its health left: half visible
    m.health = 0.f;
    CHECK(mob_tint(m, light).a == 0);
    m.health = 20.f;
    CHECK(mob_tint(m, light).a == 255);
}
