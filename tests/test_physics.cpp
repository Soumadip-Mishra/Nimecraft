// Tests for src/physics.*: the shared AABB sweep used by the player, items, and (M6) mobs.
#include "block.hpp"
#include "chunk.hpp"
#include "coords.hpp"
#include "physics.hpp"
#include "test.hpp"
#include "world.hpp"

#include <cmath>
#include <cstddef>
#include <limits>
#include <memory>

namespace {

// Columns (-2..2, -2..2) with STONE at y = y0..y1.
World layered(int y0, int y1) {
    World w(1);
    for (int cx = -2; cx <= 2; ++cx)
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

constexpr Vector3 SMALL{0.125f, 0.f, 0.125f};  // a 0.25-wide box
constexpr float SMALL_H = 0.25f;
constexpr Vector3 PLAYERISH{0.3f, 0.f, 0.3f};

}  // namespace

TEST(test_move_aabb_small_box) {
    World w = layered(0, 69);  // floor top at y = 70
    const MoveResult r = move_aabb(w, Vector3{8.5f, 75.f, 8.5f}, Vector3{0, -10.f, 0}, SMALL, SMALL_H, 1.f);
    CHECK_NEAR(r.pos.y, 70.0, 1e-4);
    CHECK(r.on_ground);
    CHECK(r.vel.y == 0.f);
    CHECK_NEAR(r.pos.x, 8.5, 1e-6);
    CHECK_NEAR(r.pos.z, 8.5, 1e-6);
    CHECK(!r.hit_x);
    CHECK(!r.hit_z);
}

TEST(test_move_aabb_hit_flags) {
    World w = layered(0, 69);
    // A wall of STONE at x = 10, y = 70..72.
    for (int z = 0; z < 16; ++z)
        for (int y = 70; y <= 72; ++y) w.set(10, y, z, blk::STONE);
    const MoveResult r = move_aabb(w, Vector3{9.5f, 70.f, 8.5f}, Vector3{5.f, 0, 0}, PLAYERISH, 1.8f, 0.5f);
    CHECK(r.hit_x);
    CHECK(!r.hit_z);
    CHECK(r.vel.x == 0.f);
    CHECK(r.vel.y == 0.f);
    CHECK(r.vel.z == 0.f);
    CHECK_NEAR(r.pos.x, 9.7, 1e-4);  // flush: 10 - 0.3

    // -Z into a wall at z = 5 sets hit_z only and keeps vel.x.
    for (int x = 0; x < 16; ++x)
        for (int y = 70; y <= 72; ++y) w.set(x, y, 5, blk::STONE);
    const MoveResult s = move_aabb(w, Vector3{3.5f, 70.f, 6.5f}, Vector3{0.f, 0, -5.f}, PLAYERISH, 1.8f, 0.5f);
    CHECK(s.hit_z);
    CHECK(!s.hit_x);
    CHECK(s.vel.z == 0.f);
    CHECK_NEAR(s.pos.z, 6.3, 1e-4);  // 6 + 0.3
}

TEST(test_move_aabb_slides_along_wall) {
    World w = layered(0, 69);
    for (int z = -16; z < 32; ++z)
        for (int y = 70; y <= 72; ++y) w.set(10, y, z, blk::STONE);
    const MoveResult r = move_aabb(w, Vector3{9.5f, 70.f, 8.5f}, Vector3{3.f, 0, 3.f}, PLAYERISH, 1.8f, 1.f);
    CHECK(r.hit_x);
    CHECK(!r.hit_z);
    CHECK_NEAR(r.pos.x, 9.7, 1e-4);
    CHECK_NEAR(r.pos.z, 11.5, 1e-3);  // kept moving 3 blocks along Z
    CHECK(r.vel.z == 3.f);
}

TEST(test_move_aabb_no_tunnel) {
    World w = layered(69, 69);  // a 1-thick floor, top at y = 70, air below it
    const MoveResult r = move_aabb(w, Vector3{8.5f, 80.f, 8.5f}, Vector3{0, -50.f, 0}, SMALL, SMALL_H, 1.f);
    CHECK_NEAR(r.pos.y, 70.0, 1e-4);
    CHECK(r.on_ground);
    // Far faster than one block per substep still stops on the floor.
    const MoveResult s = move_aabb(w, Vector3{8.5f, 200.f, 8.5f}, Vector3{0, -5000.f, 0}, SMALL, SMALL_H, 1.f);
    CHECK_NEAR(s.pos.y, 70.0, 1e-4);
    CHECK(s.on_ground);
}

TEST(test_move_aabb_zero_dt) {
    World w = layered(0, 69);
    const Vector3 p{8.5f, 75.f, 8.5f}, v{1.f, -2.f, 3.f};
    for (float dt : {0.f, -1.f, std::numeric_limits<float>::quiet_NaN()}) {
        const MoveResult r = move_aabb(w, p, v, SMALL, SMALL_H, dt);
        CHECK(r.pos.x == p.x && r.pos.y == p.y && r.pos.z == p.z);
        CHECK(r.vel.x == v.x && r.vel.y == v.y && r.vel.z == v.z);
        CHECK(!r.on_ground && !r.hit_x && !r.hit_z);
    }
}

TEST(test_move_aabb_unloaded) {
    World w = layered(0, 69);  // columns -2..2 loaded; column 5 is not
    const Vector3 p{5 * 16 + 8.5f, 90.f, 8.5f}, v{1.f, -20.f, 0};
    const MoveResult r = move_aabb(w, p, v, SMALL, SMALL_H, 1.f);
    CHECK(r.pos.x == p.x && r.pos.y == p.y && r.pos.z == p.z);
    CHECK(r.vel.x == v.x && r.vel.y == v.y);
    CHECK(!r.on_ground);
    // A box at a negative position next to the border of the loaded area moves normally.
    const MoveResult s = move_aabb(w, Vector3{-30.5f, 75.f, -30.5f}, Vector3{0, -10.f, 0}, SMALL, SMALL_H, 1.f);
    CHECK_NEAR(s.pos.y, 70.0, 1e-4);
    CHECK(s.on_ground);
}

// Coverage rows 2 and 14: dt above the 1 s cap moves as far as dt = 1; a non-finite velocity
// moves nothing.
TEST(test_move_aabb_dt_cap_and_bad_velocity) {
    World w = layered(0, 69);
    const MoveResult a = move_aabb(w, Vector3{0.5f, 70.f, 0.5f}, Vector3{2.f, 0, 0}, SMALL, SMALL_H, 1.f);
    const MoveResult b = move_aabb(w, Vector3{0.5f, 70.f, 0.5f}, Vector3{2.f, 0, 0}, SMALL, SMALL_H, 50.f);
    CHECK_NEAR(a.pos.x, 2.5, 1e-4);
    CHECK_NEAR(b.pos.x, 2.5, 1e-4);
    const MoveResult c = move_aabb(w, Vector3{0.5f, 75.f, 0.5f},
                                   Vector3{std::numeric_limits<float>::infinity(), 0, 0}, SMALL, SMALL_H, 1.f);
    CHECK(c.pos.x == 0.5f && c.pos.y == 75.f);
}

// The box overlaps cells only through its interior; resting exactly on a block top is touching.
TEST(test_aabb_at_and_block_overlap) {
    const AABB b = aabb_at(Vector3{1.5f, 70.f, -0.5f}, PLAYERISH, 1.8f);
    CHECK_NEAR(b.min.x, 1.2, 1e-6);
    CHECK_NEAR(b.max.x, 1.8, 1e-6);
    CHECK_NEAR(b.min.y, 70.0, 1e-6);
    CHECK_NEAR(b.max.y, 71.8, 1e-5);
    CHECK_NEAR(b.min.z, -0.8, 1e-6);
    CHECK_NEAR(b.max.z, -0.2, 1e-6);
    CHECK(aabb_overlaps_block(b, 1, 70, -1));
    CHECK(aabb_overlaps_block(b, 1, 71, -1));
    CHECK(!aabb_overlaps_block(b, 1, 69, -1));  // the block below only touches
    CHECK(!aabb_overlaps_block(b, 1, 72, -1));
    CHECK(!aabb_overlaps_block(b, 2, 70, -1));
    CHECK(!aabb_overlaps_block(b, 1, 70, 0));
    CHECK(!aabb_overlaps_block(b, 0, 70, -1));
}
