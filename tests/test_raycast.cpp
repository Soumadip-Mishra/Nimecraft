// Tests for src/raycast.*: voxel DDA against World.
#include "block.hpp"
#include "chunk.hpp"
#include "coords.hpp"
#include "raycast.hpp"
#include "test.hpp"
#include "world.hpp"

#include <cmath>
#include <cstddef>
#include <limits>
#include <memory>

namespace {

// Columns (-2..2, -2..2) with STONE at y = 0..69, so the floor top is at y = 70.
World flat_world() {
    World w(1);
    for (int cx = -2; cx <= 2; ++cx)
        for (int cz = -2; cz <= 2; ++cz) {
            Column col;
            for (std::size_t i = 0; i < col.size(); ++i) col[i] = std::make_unique<Chunk>();
            for (int y = 0; y < 70; ++y)
                for (int lz = 0; lz < CHUNK; ++lz)
                    for (int lx = 0; lx < CHUNK; ++lx)
                        col[static_cast<std::size_t>(y / CHUNK)]->blocks[static_cast<std::size_t>(
                            Chunk::index(lx, y % CHUNK, lz))] = blk::STONE;
            w.insert_column(ColumnPos{cx, cz}, std::move(col));
        }
    return w;
}

bool hit_at(const RayHit& h, int x, int y, int z) {
    return h.hit && h.x == x && h.y == y && h.z == z;
}

bool normal_is(const RayHit& h, int nx, int ny, int nz) {
    return h.nx == nx && h.ny == ny && h.nz == nz;
}

}  // namespace

TEST(test_raycast_hits) {
    World w = flat_world();
    w.set(5, 80, 5, blk::STONE);
    const RayHit h = raycast_voxel(w, Vector3{5.5f, 80.5f, 0.5f}, Vector3{0, 0, 1}, 10.f);
    CHECK(hit_at(h, 5, 80, 5));
    CHECK(normal_is(h, 0, 0, -1));
    // The task file says 4.0, but the ray starts at z = 0.5 and enters the block's face at z = 5,
    // so it travels 4.5 (the DDA entry distance); corrected with the user's approval.
    CHECK_NEAR(h.distance, 4.5, 1e-4);
    CHECK(!raycast_voxel(w, Vector3{5.5f, 80.5f, 0.5f}, Vector3{0, 0, 1}, 3.f).hit);
}

TEST(test_raycast_max_dist_boundary) {
    World w = flat_world();
    w.set(5, 80, 5, blk::STONE);
    CHECK(raycast_voxel(w, Vector3{5.5f, 80.5f, 0.5f}, Vector3{0, 0, 1}, 4.5f).hit);
    CHECK(!raycast_voxel(w, Vector3{5.5f, 80.5f, 0.5f}, Vector3{0, 0, 1}, 4.49f).hit);
}

TEST(test_raycast_axis_and_negative) {
    World w = flat_world();
    w.set(-5, 80, -5, blk::STONE);
    const RayHit px = raycast_voxel(w, Vector3{-7.5f, 80.5f, -4.5f}, Vector3{1, 0, 0}, 10.f);
    CHECK(hit_at(px, -5, 80, -5));
    CHECK(normal_is(px, -1, 0, 0));
    CHECK_NEAR(px.distance, 2.5, 1e-4);
    const RayHit ny = raycast_voxel(w, Vector3{-4.5f, 83.5f, -4.5f}, Vector3{0, -1, 0}, 10.f);
    CHECK(hit_at(ny, -5, 80, -5));
    CHECK(normal_is(ny, 0, 1, 0));
    CHECK_NEAR(ny.distance, 2.5, 1e-4);
    const RayHit pz = raycast_voxel(w, Vector3{-4.5f, 80.5f, -7.5f}, Vector3{0, 0, 1}, 10.f);
    CHECK(hit_at(pz, -5, 80, -5));
    CHECK(normal_is(pz, 0, 0, -1));
    CHECK(std::isfinite(pz.distance));
    // And from the other sides.
    const RayHit nx = raycast_voxel(w, Vector3{-1.5f, 80.5f, -4.5f}, Vector3{-1, 0, 0}, 10.f);
    CHECK(hit_at(nx, -5, 80, -5));
    CHECK(normal_is(nx, 1, 0, 0));
    const RayHit nz = raycast_voxel(w, Vector3{-4.5f, 80.5f, -1.5f}, Vector3{0, 0, -1}, 10.f);
    CHECK(hit_at(nz, -5, 80, -5));
    CHECK(normal_is(nz, 0, 0, 1));
}

TEST(test_raycast_floor_from_above) {
    World w = flat_world();
    const RayHit h = raycast_voxel(w, Vector3{3.5f, 80.5f, 3.5f}, Vector3{0, -1, 0}, 20.f);
    CHECK(hit_at(h, 3, 69, 3));
    CHECK(normal_is(h, 0, 1, 0));
    CHECK_NEAR(h.distance, 10.5, 1e-4);
}

TEST(test_raycast_inside_block) {
    World w = flat_world();
    w.set(5, 80, 5, blk::STONE);
    const RayHit h = raycast_voxel(w, Vector3{5.5f, 80.5f, 5.5f}, Vector3{0, 0, 1}, 10.f);
    CHECK(hit_at(h, 5, 80, 5));
    CHECK(h.distance == 0.f);
    CHECK(normal_is(h, 0, 0, 0));
}

TEST(test_raycast_integer_origin) {
    World w = flat_world();
    w.set(5, 80, 5, blk::STONE);
    const RayHit h = raycast_voxel(w, Vector3{5.0f, 80.5f, 0.5f}, Vector3{0, 0, 1}, 10.f);
    CHECK(hit_at(h, 5, 80, 5));
}

TEST(test_raycast_diagonal_edge) {
    World w = flat_world();
    w.set(6, 80, 5, blk::STONE);
    const float s = 1.f / std::sqrt(2.f);
    const RayHit h = raycast_voxel(w, Vector3{5.5f, 80.5f, 4.5f}, Vector3{s, 0, s}, 5.f);
    CHECK(hit_at(h, 6, 80, 5));
    // The blocks beside the corner: a diagonal skip from (5,80,4) to (6,80,5) would miss both.
    World a = flat_world();
    a.set(6, 80, 4, blk::STONE);
    CHECK(hit_at(raycast_voxel(a, Vector3{5.5f, 80.5f, 4.5f}, Vector3{s, 0, s}, 5.f), 6, 80, 4));
    World b = flat_world();  // both side blocks solid: the ray must stop at one of them
    b.set(6, 80, 4, blk::STONE);
    b.set(5, 80, 5, blk::STONE);
    const RayHit hb = raycast_voxel(b, Vector3{5.5f, 80.5f, 4.5f}, Vector3{s, 0, s}, 5.f);
    CHECK(hit_at(hb, 6, 80, 4) || hit_at(hb, 5, 80, 5));
}

TEST(test_raycast_infinite_max_dist_misses) {
    World w = flat_world();
    const float inf = std::numeric_limits<float>::infinity();
    CHECK(!raycast_voxel(w, Vector3{0.5f, 80.5f, 0.5f}, Vector3{1, 0, 0}, inf).hit);
    CHECK(!raycast_voxel(w, Vector3{0.5f, 80.5f, 0.5f}, Vector3{0, -1, 0}, inf).hit);
}

TEST(test_raycast_unloaded) {
    World w = flat_world();
    CHECK(!raycast_voxel(w, Vector3{100.5f, 80.5f, 0.5f}, Vector3{1, 0, 0}, 50.f).hit);
    CHECK(!raycast_voxel(w, Vector3{100.5f, 80.5f, 0.5f}, Vector3{0, -1, 0}, 50.f).hit);
}

TEST(test_raycast_ignores_below_and_above_world) {
    World w = flat_world();
    // Below y = 0 World::get reports DEEPSTONE, but only blocks with 0 <= y < 256 can be hit.
    CHECK(!raycast_voxel(w, Vector3{100.5f, 1.5f, 0.5f}, Vector3{0, -1, 0}, 10.f).hit);
    CHECK(!raycast_voxel(w, Vector3{100.5f, -3.5f, 0.5f}, Vector3{0, -1, 0}, 1e9f).hit);
    CHECK(!raycast_voxel(w, Vector3{0.5f, 250.5f, 0.5f}, Vector3{0, 1, 0}, 1e9f).hit);
    CHECK(!raycast_voxel(w, Vector3{0.5f, 300.5f, 0.5f}, Vector3{0, 1, 0}, 1e9f).hit);
    // y = 0 itself is in range.
    World w2(1);
    w2.insert_column(ColumnPos{0, 0}, Column{});
    w2.set(1, 0, 1, blk::STONE);
    const RayHit h = raycast_voxel(w2, Vector3{1.5f, 3.5f, 1.5f}, Vector3{0, -1, 0}, 10.f);
    CHECK(hit_at(h, 1, 0, 1));
    // y = 255 too.
    w2.set(1, 255, 1, blk::STONE);
    CHECK(hit_at(raycast_voxel(w2, Vector3{1.5f, 250.5f, 1.5f}, Vector3{0, 1, 0}, 10.f), 1, 255, 1));
}

TEST(test_raycast_zero_dir_misses) {
    World w = flat_world();
    CHECK(!raycast_voxel(w, Vector3{0.5f, 80.5f, 0.5f}, Vector3{0, 0, 0}, 10.f).hit);
}

TEST(test_raycast_far_coordinates) {
    World w(1);
    w.insert_column(ColumnPos{62500, 0}, Column{});
    w.set(1000005, 80, 5, blk::STONE);
    const RayHit h = raycast_voxel(w, Vector3{1000000.5f, 80.5f, 5.5f}, Vector3{1, 0, 0}, 10.f);
    CHECK(hit_at(h, 1000005, 80, 5));
    CHECK(normal_is(h, -1, 0, 0));
    CHECK_NEAR(h.distance, 4.5, 1e-3);
}

TEST(test_raycast_skips_non_solid) {
    World w = flat_world();
    w.set(5, 80, 3, blk::AIR);
    w.set(5, 80, 5, blk::LEAVES);  // solid: leaves can be targeted
    CHECK(hit_at(raycast_voxel(w, Vector3{5.5f, 80.5f, 0.5f}, Vector3{0, 0, 1}, 10.f), 5, 80, 5));
}
