// Tests for src/player.*: movement, gravity, AABB collision, look angles, spawn.
#include "block.hpp"
#include "chunk.hpp"
#include "coords.hpp"
#include "player.hpp"
#include "test.hpp"
#include "world.hpp"

#include <cmath>
#include <cstddef>
#include <limits>
#include <memory>

namespace {

// Columns (ox-2..ox+2, -2..2) with STONE at y = y0..y1.
World layered_world(int y0, int y1, int ox = 0) {
    World w(1);
    for (int cx = ox - 2; cx <= ox + 2; ++cx)
        for (int cz = -2; cz <= 2; ++cz) {
            Column col;
            for (std::size_t i = 0; i < col.size(); ++i) col[i] = std::make_unique<Chunk>();
            for (int y = y0; y <= y1; ++y)
                for (int lz = 0; lz < CHUNK; ++lz)
                    for (int lx = 0; lx < CHUNK; ++lx)
                        col[static_cast<std::size_t>(y / CHUNK)]->blocks[static_cast<std::size_t>(
                            Chunk::index(lx, y % CHUNK, lz))] = blk::STONE;
            w.insert_column(ColumnPos{cx, cz}, std::move(col));
        }
    return w;
}

// The task's flat world: STONE y = 0..69, floor top at y = 70.
World flat_world() {
    return layered_world(0, 69);
}

Player at(float x, float y, float z) {
    Player p{};
    p.pos = Vector3{x, y, z};
    return p;
}

void run(Player& p, const World& w, const PlayerInput& in, int steps, float dt = 1.f / 60.f) {
    for (int i = 0; i < steps; ++i) player_update(p, w, in, dt);
}

}  // namespace

TEST(test_player_rests_on_floor) {
    World w = flat_world();
    Player p = at(8.5f, 70.f, 8.5f);
    run(p, w, PlayerInput{}, 60);
    CHECK_NEAR(p.pos.y, 70.0, 1e-3);
    CHECK(p.on_ground);
    CHECK_NEAR(p.pos.x, 8.5, 1e-6);
    CHECK_NEAR(p.pos.z, 8.5, 1e-6);
}

TEST(test_player_falls_and_lands) {
    World w = flat_world();
    Player p = at(8.5f, 80.f, 8.5f);
    player_update(p, w, PlayerInput{}, 1.f / 60.f);
    CHECK(!p.on_ground);
    CHECK(p.pos.y < 80.f);
    CHECK(p.vel.y < 0.f);
    run(p, w, PlayerInput{}, 120);
    CHECK_NEAR(p.pos.y, 70.0, 1e-3);
    CHECK(p.on_ground);
    CHECK(p.vel.y == 0.f);
}

TEST(test_player_max_fall_speed) {
    World w = layered_world(0, 0);
    Player p = at(8.5f, 250.f, 8.5f);
    run(p, w, PlayerInput{}, 180);  // 3 s of free fall would reach 84 without the cap
    CHECK(p.vel.y >= -50.f - 1e-3f);
    CHECK_NEAR(p.vel.y, -50.0, 1e-3);
}

TEST(test_player_no_tunnel_on_huge_dt) {
    World w = layered_world(69, 69);
    Player p = at(8.5f, 72.f, 8.5f);
    p.vel.y = -50.f;
    player_update(p, w, PlayerInput{}, 1.0f);
    CHECK(p.pos.y >= 70.f - 1e-3f);
}

TEST(test_player_wall) {
    World w = flat_world();
    for (int y = 70; y <= 72; ++y)
        for (int z = -32; z <= 31; ++z) w.set(12, y, z, blk::STONE);
    Player p = at(8.5f, 70.f, 8.5f);
    PlayerInput in;
    in.forward = 1.f;
    run(p, w, in, 120);
    CHECK(p.pos.x <= 11.7f + 1e-3f);
    CHECK(p.pos.x >= 11.7f - 1e-3f);  // it walked all the way up to the wall
    CHECK_NEAR(p.pos.z, 8.5, 1e-4);
}

TEST(test_player_wall_negative_direction) {
    World w = flat_world();
    for (int y = 70; y <= 72; ++y)
        for (int x = -32; x <= 47; ++x) w.set(x, y, 4, blk::STONE);
    Player p = at(8.5f, 70.f, 8.5f);
    p.yaw = -1.5707964f;  // forward = (cos, 0, sin) = (0, 0, -1)
    PlayerInput in;
    in.forward = 1.f;
    run(p, w, in, 120);
    CHECK_NEAR(p.pos.z, 5.3, 1e-3);  // the wall's +Z face is at z = 5
}

TEST(test_player_jump_height) {
    World w = flat_world();
    Player p = at(8.5f, 70.f, 8.5f);
    run(p, w, PlayerInput{}, 2);
    CHECK(p.on_ground);
    PlayerInput jump;
    jump.jump = true;
    player_update(p, w, jump, 1.f / 60.f);
    float max_y = p.pos.y;
    for (int i = 0; i < 120; ++i) {
        player_update(p, w, PlayerInput{}, 1.f / 60.f);
        if (p.pos.y > max_y) max_y = p.pos.y;
    }
    CHECK(max_y >= 71.3f);
    CHECK(max_y <= 71.6f);
    CHECK_NEAR(p.pos.y, 70.0, 1e-3);
}

TEST(test_player_no_jump_in_air) {
    World w = flat_world();
    Player p = at(8.5f, 80.f, 8.5f);
    PlayerInput jump;
    jump.jump = true;
    player_update(p, w, jump, 1.f / 60.f);
    CHECK(p.vel.y < 0.f);
}

TEST(test_player_frozen_when_unloaded) {
    World w = flat_world();
    Player p = at(500.5f, 90.f, 500.5f);
    PlayerInput in;
    in.forward = 1.f;
    in.jump = true;
    in.mouse_dx = 10.f;
    run(p, w, in, 60);
    CHECK(p.pos.x == 500.5f);
    CHECK(p.pos.y == 90.f);
    CHECK(p.pos.z == 500.5f);
    CHECK(p.vel.y == 0.f);
    CHECK_NEAR(p.yaw, 60 * 10 * 0.003, 1e-4);
}

TEST(test_player_look_angles) {
    World w = flat_world();
    Player p = at(8.5f, 70.f, 8.5f);
    PlayerInput in;
    in.mouse_dx = 100.f;
    in.mouse_dy = 50.f;
    player_update(p, w, in, 1.f / 60.f);
    CHECK_NEAR(p.yaw, 0.3, 1e-6);
    CHECK_NEAR(p.pitch, -0.15, 1e-6);
    in.mouse_dx = 0.f;
    in.mouse_dy = -100000.f;
    player_update(p, w, in, 1.f / 60.f);
    CHECK_NEAR(p.pitch, 1.55, 1e-6);
    in.mouse_dy = 100000.f;
    player_update(p, w, in, 1.f / 60.f);
    CHECK_NEAR(p.pitch, -1.55, 1e-6);
}

TEST(test_player_look_and_eye) {
    Player p = at(1.f, 70.f, 2.f);
    const Vector3 eye = player_eye(p);
    CHECK_NEAR(eye.x, 1.0, 1e-6);
    CHECK_NEAR(eye.y, 71.62, 1e-4);
    CHECK_NEAR(eye.z, 2.0, 1e-6);
    Vector3 l = player_look(p);
    CHECK_NEAR(l.x, 1.0, 1e-6);
    CHECK_NEAR(l.y, 0.0, 1e-6);
    CHECK_NEAR(l.z, 0.0, 1e-6);
    p.yaw = 1.5707964f;
    l = player_look(p);
    CHECK_NEAR(l.x, 0.0, 1e-6);
    CHECK_NEAR(l.z, 1.0, 1e-6);
    p.pitch = 1.5707964f;
    l = player_look(p);
    CHECK_NEAR(l.y, 1.0, 1e-6);
}

TEST(test_player_walk_directions) {
    World w = flat_world();
    PlayerInput fwd;
    fwd.forward = 1.f;
    Player a = at(0.5f, 70.f, 0.5f);
    run(a, w, fwd, 60);  // 1 s at 4.3 blocks/s along +X (yaw 0)
    CHECK_NEAR(a.pos.x, 4.8, 0.01);
    CHECK_NEAR(a.pos.z, 0.5, 1e-4);

    PlayerInput right;
    right.right = 1.f;
    Player b = at(0.5f, 70.f, 0.5f);
    run(b, w, right, 60);  // right = (-sin 0, 0, cos 0) = +Z
    CHECK_NEAR(b.pos.x, 0.5, 1e-4);
    CHECK_NEAR(b.pos.z, 4.8, 0.01);
}

TEST(test_player_diagonal_normalized) {
    World w = flat_world();
    PlayerInput in;
    in.forward = 1.f;
    in.right = 1.f;
    Player p = at(0.5f, 70.f, 0.5f);
    run(p, w, in, 60);
    const double dx = p.pos.x - 0.5, dz = p.pos.z - 0.5;
    CHECK_NEAR(std::sqrt(dx * dx + dz * dz), 4.3, 0.01);
    // A short input vector is not stretched to length 1.
    PlayerInput half;
    half.forward = 0.5f;
    Player q = at(0.5f, 70.f, 0.5f);
    run(q, w, half, 60);
    CHECK_NEAR(q.pos.x, 0.5 + 2.15, 0.01);
}

TEST(test_player_intersects_block) {
    const Player p = at(8.5f, 70.f, 8.5f);
    CHECK(player_intersects_block(p, 8, 70, 8));
    CHECK(player_intersects_block(p, 8, 71, 8));
    CHECK(!player_intersects_block(p, 8, 72, 8));
    CHECK(!player_intersects_block(p, 9, 70, 8));
    CHECK(!player_intersects_block(p, 8, 69, 8));  // the floor block under the feet
    CHECK(!player_intersects_block(p, 7, 70, 8));
    CHECK(!player_intersects_block(p, 8, 70, 9));
    CHECK(!player_intersects_block(p, 8, 70, 7));
    const Player edge = at(9.f, 70.f, 9.f);  // straddles four columns of blocks
    CHECK(player_intersects_block(edge, 8, 70, 8));
    CHECK(player_intersects_block(edge, 9, 71, 9));
    CHECK(player_intersects_block(edge, 8, 70, 9));
}

TEST(test_player_ceiling) {
    World w = flat_world();
    for (int z = 0; z < 16; ++z)
        for (int x = 0; x < 16; ++x) w.set(x, 72, z, blk::STONE);
    Player p = at(8.5f, 70.f, 8.5f);
    run(p, w, PlayerInput{}, 2);
    PlayerInput jump;
    jump.jump = true;
    player_update(p, w, jump, 1.f / 60.f);
    bool hit_ceiling = false;
    for (int i = 0; i < 60; ++i) {
        CHECK(p.pos.y + PLAYER_H <= 72.f + 1e-3f);
        // Contact: within one substep of fall (28 / 120^2 = 0.002) below the ceiling.
        if (p.pos.y + PLAYER_H >= 72.f - 0.01f) {
            hit_ceiling = true;
            CHECK(p.vel.y <= 0.f);
        }
        player_update(p, w, PlayerInput{}, 1.f / 60.f);
    }
    CHECK(hit_ceiling);
    CHECK_NEAR(p.pos.y, 70.0, 1e-3);
}

TEST(test_player_negative_coords) {
    World w = flat_world();
    Player p = at(-8.5f, 70.f, -8.5f);
    run(p, w, PlayerInput{}, 60);
    CHECK_NEAR(p.pos.y, 70.0, 1e-3);
    CHECK(p.on_ground);
}

TEST(test_player_crosses_zero) {
    World w = flat_world();
    Player p = at(1.5f, 70.f, 1.5f);
    p.yaw = 3.1415927f;  // forward = -X, the walk crosses x = 0 into the -1 column
    PlayerInput in;
    in.forward = 1.f;
    run(p, w, in, 60);
    CHECK_NEAR(p.pos.x, 1.5 - 4.3, 0.01);
    CHECK_NEAR(p.pos.y, 70.0, 1e-3);
    CHECK(p.on_ground);
}

TEST(test_player_freezes_entering_unloaded) {
    // Columns end at x = 47: once the feet center crosses x = 48 the player stops dead.
    World w = flat_world();
    Player p = at(40.5f, 70.f, 8.5f);
    PlayerInput in;
    in.forward = 1.f;
    run(p, w, in, 180);
    CHECK(p.pos.x >= 48.f);
    CHECK(p.pos.x < 48.f + 4.3f / 60.f);  // at most one frame past the border
    CHECK_NEAR(p.pos.y, 70.0, 1e-3);
}

TEST(test_player_far_coordinates) {
    // The documented playable range is |x|, |z| <= 30,000 (float positions; see player.hpp).
    // Column 1875 starts at x = 30,000; columns 1873..1877 cover x = 29,968..30,047.
    World w = layered_world(0, 69, 1875);
    PlayerInput in;
    in.forward = 1.f;
    Player p = at(30000.5f, 70.f, 8.5f);
    run(p, w, in, 60);  // 1 s at 4.3 blocks/s
    CHECK_NEAR(p.pos.x, 30000.5 + 4.3, 4.3 * 0.02);
    CHECK_NEAR(p.pos.y, 70.0, 1e-3);
    CHECK(p.on_ground);

    for (int y = 70; y <= 72; ++y)
        for (int z = -32; z <= 31; ++z) w.set(30010, y, z, blk::STONE);
    Player q = at(30005.5f, 70.f, 8.5f);
    run(q, w, in, 120);
    CHECK_NEAR(q.pos.x, 30009.7, 1e-3);  // stops flush at the wall
    CHECK_NEAR(q.pos.y, 70.0, 1e-3);     // and does not climb it
}

TEST(test_player_large_dt_matches_small) {
    // dt is clamped to 0.1 s (task file), so the task's "dt = 1.0 once vs 60 x 1/60" cannot hold;
    // with the user's approval the comparison is dt = 0.1 once vs 6 x 1/60, and dt = 1.0 must
    // move exactly as far as the clamped 0.1.
    World w = flat_world();
    PlayerInput in;
    in.forward = 1.f;
    Player once = at(0.5f, 70.f, 0.5f), steps = at(0.5f, 70.f, 0.5f), huge = at(0.5f, 70.f, 0.5f);
    player_update(once, w, in, 0.1f);
    run(steps, w, in, 6);
    player_update(huge, w, in, 1.0f);
    CHECK(std::fabs(once.pos.x - steps.pos.x) < 0.05f);
    CHECK_NEAR(once.pos.x, 0.5 + 0.43, 1e-3);
    CHECK_NEAR(huge.pos.x, once.pos.x, 1e-5);
    CHECK_NEAR(huge.pos.y, 70.0, 1e-3);
}

TEST(test_player_bad_dt_ignored) {
    World w = flat_world();
    PlayerInput in;
    in.forward = 1.f;
    Player p = at(0.5f, 70.f, 0.5f);
    player_update(p, w, in, 0.f);
    player_update(p, w, in, -1.f);
    player_update(p, w, in, std::numeric_limits<float>::quiet_NaN());
    CHECK(p.pos.x == 0.5f);
    CHECK(p.pos.y == 70.f);
    CHECK(std::isfinite(p.vel.y));
}

TEST(test_player_step_needs_jump) {
    // A 1-block step stops walking; jumping clears it. A 2-block step cannot be jumped.
    World w = flat_world();
    for (int z = -32; z <= 31; ++z) w.set(12, 70, z, blk::STONE);
    for (int z = -32; z <= 31; ++z)
        for (int y = 70; y <= 71; ++y) w.set(20, y, z, blk::STONE);
    Player p = at(8.5f, 70.f, 8.5f);
    PlayerInput walk;
    walk.forward = 1.f;
    run(p, w, walk, 120);
    CHECK_NEAR(p.pos.x, 11.7, 1e-3);
    PlayerInput hop = walk;
    hop.jump = true;
    run(p, w, hop, 120);  // hops over the step at x = 12, then jumps against the wall at x = 20
    run(p, w, walk, 60);  // stop jumping and settle
    CHECK_NEAR(p.pos.x, 19.7, 1e-3);
    CHECK_NEAR(p.pos.y, 70.0, 1e-3);
}

TEST(test_spawn_player_on_surface) {
    World w = flat_world();
    Player p = spawn_player(w);
    CHECK(p.pos.y == 70.f);
    CHECK(p.pos.x == 0.5f);
    CHECK(p.pos.z == 0.5f);
    CHECK(p.vel.y == 0.f);
    for (int y = 70; y <= 75; ++y) w.set(0, y, 0, blk::LOG);
    CHECK(spawn_player(w).pos.y == 76.f);
}

TEST(test_spawn_player_empty_column) {
    World w(1);
    w.insert_column(ColumnPos{0, 0}, Column{});
    CHECK(spawn_player(w).pos.y == 0.f);  // nothing solid: stand on the DEEPSTONE below the world
    w.set(0, 255, 0, blk::STONE);
    CHECK(spawn_player(w).pos.y == 256.f);
}
