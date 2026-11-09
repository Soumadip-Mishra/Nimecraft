// Tests for the window-free rules in src/audio.* (M8 Task 2): positional volume and pan, the
// per-sound cooldown, footsteps, creature calls, and music timing.
#include "audio.hpp"
#include "test.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <set>

namespace {

constexpr float HALF_TURN = 3.14159265f;

void touch(const std::filesystem::path& f) {
    std::ofstream out(f, std::ios::binary);
    out << "x";
}

}  // namespace

TEST(test_spatial_falloff) {
    const Vector3 l{0, 64, 0};
    CHECK_NEAR(spatialize(l, 0.f, Vector3{0, 64, 0}).volume, 1.0, 1e-5);
    CHECK_NEAR(spatialize(l, 0.f, Vector3{8, 64, 0}).volume, 0.5, 1e-5);
    CHECK_NEAR(spatialize(l, 0.f, Vector3{16, 64, 0}).volume, 0.0, 1e-5);
    CHECK_NEAR(spatialize(l, 0.f, Vector3{4, 64, 0}).volume, 0.75, 1e-5);
    CHECK_NEAR(spatialize(l, 0.f, Vector3{0, 72, 0}).volume, 0.5, 1e-5);   // 3D distance: straight up
    CHECK_NEAR(spatialize(l, 0.f, Vector3{3, 68, 0}).volume, 1.0 - 5.0 / 16.0, 1e-5);  // 3-4-5
    // Just inside and just outside the edge.
    CHECK(spatialize(l, 0.f, Vector3{15.9f, 64, 0}).volume > 0.f);
    CHECK(spatialize(l, 0.f, Vector3{16.1f, 64, 0}).volume == 0.f);
    // max_dist scales the falloff.
    CHECK_NEAR(spatialize(l, 0.f, Vector3{4, 64, 0}, 8.f).volume, 0.5, 1e-5);
}

TEST(test_spatial_out_of_range_silent) {
    CHECK(spatialize(Vector3{0, 64, 0}, 0.f, Vector3{20, 64, 0}).volume == 0.f);
    CHECK(spatialize(Vector3{0, 64, 0}, 1.f, Vector3{-300, 10, 900}).volume == 0.f);
}

TEST(test_spatial_pan_left_right) {
    const Vector3 l{0, 64, 0};
    // Facing +X (yaw 0): right is +Z (player.cpp moves right along (-sin yaw, 0, cos yaw)).
    CHECK(spatialize(l, 0.f, Vector3{0, 64, 5}).pan > 0.f);
    CHECK(spatialize(l, 0.f, Vector3{0, 64, -5}).pan < 0.f);
    CHECK_NEAR(spatialize(l, 0.f, Vector3{5, 64, 0}).pan, 0.0, 1e-4);   // straight ahead
    CHECK_NEAR(spatialize(l, 0.f, Vector3{-5, 64, 0}).pan, 0.0, 1e-4);  // straight behind
    CHECK_NEAR(spatialize(l, 0.f, Vector3{0, 64, 5}).pan, 1.0, 1e-4);   // fully right
    CHECK_NEAR(spatialize(l, 0.f, Vector3{0, 64, -5}).pan, -1.0, 1e-4);
    CHECK_NEAR(spatialize(l, 0.f, Vector3{3, 64, 3}).pan, 0.70710678, 1e-4);  // 45 degrees right
    // Facing +Z (yaw pi/2): right is -X.
    CHECK(spatialize(l, HALF_TURN / 2, Vector3{-5, 64, 0}).pan > 0.9f);
    CHECK(spatialize(l, HALF_TURN / 2, Vector3{5, 64, 0}).pan < -0.9f);
    CHECK_NEAR(spatialize(l, HALF_TURN / 2, Vector3{0, 64, 5}).pan, 0.0, 1e-4);
    // Directly above: no horizontal angle, centered.
    CHECK_NEAR(spatialize(l, 0.f, Vector3{0, 70, 0}).pan, 0.0, 1e-4);
    // Pan always in [-1, 1], including large yaw values and sources out of range.
    for (int i = 0; i < 64; ++i) {
        const float yaw = static_cast<float>(i) * 0.7f - 20.f;
        const Vector3 s{static_cast<float>(i % 7) - 3.f, 64.f, static_cast<float>(i % 5) * 9.f - 18.f};
        const Spatial sp = spatialize(l, yaw, s);
        CHECK(sp.pan >= -1.f && sp.pan <= 1.f);
        CHECK(sp.volume >= 0.f && sp.volume <= 1.f);
    }
}

TEST(test_spatial_same_position) {
    const Spatial s = spatialize(Vector3{5, 70, -3}, 1.3f, Vector3{5, 70, -3});
    CHECK(s.volume == 1.f);
    CHECK(s.pan == 0.f);
    CHECK(!std::isnan(s.volume) && !std::isnan(s.pan));
}

TEST(test_spatial_invalid_input) {
    const float nan = std::numeric_limits<float>::quiet_NaN(), inf = std::numeric_limits<float>::infinity();
    const Vector3 l{0, 64, 0};
    for (const Spatial& s : {spatialize(l, 0.f, Vector3{nan, 64, 0}), spatialize(l, nan, Vector3{4, 64, 0}),
                             spatialize(Vector3{inf, 0, 0}, 0.f, Vector3{4, 64, 0}),
                             spatialize(l, 0.f, Vector3{4, 64, 0}, 0.f), spatialize(l, 0.f, Vector3{4, 64, 0}, -3.f),
                             spatialize(l, 0.f, Vector3{4, 64, 0}, nan)}) {
        CHECK(s.volume == 0.f);
        CHECK(s.pan == 0.f);
    }
    // At the listener's exact position a bad max_dist still plays at full volume.
    CHECK(spatialize(l, 0.f, l, 0.f).volume == 1.f);
}

TEST(test_spatial_far_coordinates) {
    // x = 1,000,000: float spacing is 0.0625, so 8 blocks away stays 8 blocks away.
    const Spatial s = spatialize(Vector3{1000000.f, 64, -1000000.f}, 0.f, Vector3{1000000.f, 64, -999992.f});
    CHECK_NEAR(s.volume, 0.5, 1e-3);
    CHECK_NEAR(s.pan, 1.0, 1e-3);
    CHECK(spatialize(Vector3{-1000000.f, 64, 0}, 0.f, Vector3{-999980.f, 64, 0}).volume == 0.f);
}

TEST(test_sfx_limiter) {
    SfxLimiter lim;
    CHECK(lim.allow(Sfx::Dig, 0.0));
    CHECK(!lim.allow(Sfx::Dig, 0.079));
    CHECK(lim.allow(Sfx::Dig, 0.08));
    // Different sounds do not block each other.
    CHECK(lim.allow(Sfx::Place, 0.08));
    CHECK(lim.allow(Sfx::Hurt, 0.081));
    CHECK(!lim.allow(Sfx::Place, 0.1));
    // A denied call does not restart the wait: allowed at 1.0, denied at 1.05, allowed at 1.08.
    SfxLimiter b;
    CHECK(b.allow(Sfx::Click, 1.0));
    CHECK(!b.allow(Sfx::Click, 1.05));
    CHECK(b.allow(Sfx::Click, 1.08));
    // Every sound starts allowed, even at a negative clock.
    SfxLimiter c;
    for (int s = 0; s < static_cast<int>(Sfx::Count); ++s) CHECK(c.allow(static_cast<Sfx>(s), -5.0));
    // Out of range and non-finite are denied.
    SfxLimiter d;
    CHECK(!d.allow(Sfx::Count, 0.0));
    CHECK(!d.allow(static_cast<Sfx>(250), 0.0));
    CHECK(!d.allow(Sfx::Dig, std::numeric_limits<double>::quiet_NaN()));
    CHECK(!d.allow(Sfx::Dig, std::numeric_limits<double>::infinity()));
    CHECK(d.allow(Sfx::Dig, 3.0));  // the bad calls left Dig untouched
}

TEST(test_footstep_interval) {
    CHECK(footstep_interval(false) == 0.45f);
    CHECK(footstep_interval(true) == 0.35f);
}

TEST(test_footsteps_distance) {
    Footsteps f;
    CHECK(!f.step(0.44f, false, true));
    CHECK(f.step(0.01f, false, true));   // 0.45 reached
    CHECK(f.walked < 0.001f);
    CHECK(!f.step(0.34f, true, true));
    CHECK(f.step(0.02f, true, true));    // 0.36 >= 0.35 sprinting, 0.01 left over
    CHECK_NEAR(f.walked, 0.01, 1e-5);
    // Off the ground nothing counts.
    Footsteps air;
    CHECK(!air.step(5.f, false, false));
    CHECK(air.walked == 0.f);
    // Bad distances add nothing.
    Footsteps bad;
    CHECK(!bad.step(-1.f, false, true));
    CHECK(!bad.step(std::numeric_limits<float>::quiet_NaN(), false, true));
    CHECK(!bad.step(std::numeric_limits<float>::infinity(), false, true));
    CHECK(bad.walked == 0.f);
    // A long frame plays one step and keeps less than one interval: 1.0 m = 2 * 0.45 + 0.1.
    Footsteps big;
    CHECK(big.step(1.0f, false, true));
    CHECK_NEAR(big.walked, 0.1, 1e-5);
    CHECK(!big.step(0.3f, false, true));
    CHECK(big.step(0.05f, false, true));
    // 60 frames of 1/60 s at 4.3 m/s (one second) play 9 steps (4.3 / 0.45 = 9.56).
    Footsteps walk;
    int steps = 0;
    for (int i = 0; i < 60; ++i) steps += walk.step(4.3f / 60.f, false, true) ? 1 : 0;
    CHECK(steps == 9);
}

TEST(test_step_sfx_by_block) {
    CHECK(step_sfx(blk::GRASS) == Sfx::StepGrass);
    CHECK(step_sfx(blk::DIRT) == Sfx::StepGrass);
    CHECK(step_sfx(blk::CLAY) == Sfx::StepGrass);
    CHECK(step_sfx(blk::LEAVES) == Sfx::StepGrass);
    CHECK(step_sfx(blk::PINE_LEAVES) == Sfx::StepGrass);
    CHECK(step_sfx(blk::SAND) == Sfx::StepSand);
    CHECK(step_sfx(blk::GRAVEL) == Sfx::StepSand);
    CHECK(step_sfx(blk::LOG) == Sfx::StepWood);
    CHECK(step_sfx(blk::PINE_LOG) == Sfx::StepWood);
    CHECK(step_sfx(blk::PLANKS) == Sfx::StepWood);
    CHECK(step_sfx(blk::WORKBENCH) == Sfx::StepWood);
    CHECK(step_sfx(blk::CRATE) == Sfx::StepWood);
    CHECK(step_sfx(blk::SNOW) == Sfx::StepSnow);
    CHECK(step_sfx(blk::STONE) == Sfx::StepStone);
    CHECK(step_sfx(blk::DEEPSTONE) == Sfx::StepStone);
    CHECK(step_sfx(blk::IRON_ORE) == Sfx::StepStone);
    CHECK(step_sfx(blk::ICE) == Sfx::StepStone);
    CHECK(step_sfx(blk::GLASS) == Sfx::StepStone);
    CHECK(step_sfx(blk::SANDSTONE) == Sfx::StepStone);
    CHECK(!step_sfx(blk::AIR));
    CHECK(!step_sfx(blk::WATER));
    CHECK(!step_sfx(blk::TALL_GRASS));
    CHECK(!step_sfx(blk::TORCH));
    CHECK(!step_sfx(blk::SAPLING));
    CHECK(!step_sfx(blk::COUNT));
    CHECK(!step_sfx(static_cast<BlockId>(9999)));
}

TEST(test_mob_call) {
    CHECK(mob_call(MobType::Tusker) == Sfx::TuskerGrunt);
    CHECK(mob_call(MobType::Fluffhorn) == Sfx::FluffhornBleat);
    CHECK(mob_call(MobType::Hopper) == Sfx::HopperSqueak);
    CHECK(mob_call(MobType::Gloomwalker) == Sfx::GloomMoan);
    CHECK(mob_call(MobType::Thornspitter) == Sfx::ThornShoot);
    CHECK(mob_call(MobType::CaveCrawler) == Sfx::CrawlerHiss);
    CHECK(mob_call(MobType::COUNT) == Sfx::Click);
}

TEST(test_music_delay_range) {
    uint32_t rng = 12345u;
    double lo = 1e9, hi = -1e9;
    for (int i = 0; i < 2000; ++i) {
        const double d = next_music_delay(rng);
        CHECK(d >= 300.0 && d <= 600.0);
        lo = d < lo ? d : lo;
        hi = d > hi ? d : hi;
    }
    CHECK(lo < 310.0);  // the whole range is used
    CHECK(hi > 590.0);
    // Deterministic from the state; a zero state still works.
    uint32_t a = 7u, b = 7u;
    CHECK(next_music_delay(a) == next_music_delay(b));
    CHECK(a == b);
    uint32_t z = 0u;
    const double dz = next_music_delay(z);
    CHECK(dz >= 300.0 && dz <= 600.0);
    CHECK(z != 0u);
}

TEST(test_pick_track) {
    uint32_t rng = 99u;
    CHECK(pick_track(rng, 0) == -1);
    CHECK(pick_track(rng, -3) == -1);
    for (int i = 0; i < 100; ++i) CHECK(pick_track(rng, 1) == 0);
    std::set<int> seen;
    for (int i = 0; i < 500; ++i) {
        const int t = pick_track(rng, 3);
        CHECK(t >= 0 && t < 3);
        seen.insert(t);
    }
    CHECK(seen.size() == 3);
}

TEST(test_find_music) {
    test::TempDir tmp;
    CHECK(find_music(tmp.path() / "missing").empty());
    touch(tmp.path() / "plain_file");
    CHECK(find_music(tmp.path() / "plain_file").empty());  // not a folder
    const std::filesystem::path dir = tmp.path() / std::filesystem::path(u8"músic");  // non-ASCII folder
    std::filesystem::create_directories(dir / "sub.ogg");    // a folder named like a track
    touch(dir / "b.ogg");
    touch(dir / "a.OGG");
    touch(dir / "c.mp3");
    touch(dir / "notes.txt");
    touch(dir / "ogg");
    touch(dir / "sub.ogg" / "deep.ogg");  // no subfolder scan
    const auto found = find_music(dir);
    CHECK(found.size() == 2);
    if (found.size() == 2) {
        CHECK(found[0].filename() == "a.OGG");
        CHECK(found[1].filename() == "b.ogg");
    }
    CHECK(find_music(tmp.path()).empty());  // the temp root has no tracks of its own
}
