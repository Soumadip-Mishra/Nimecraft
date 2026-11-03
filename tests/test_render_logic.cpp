// Tests for the window-free decisions in src/render.*: translucent draw order and the fog range
// (M2 Task 6), and the sun and moon placement (M3 Task 5).
#include "coords.hpp"
#include "render.hpp"
#include "test.hpp"

#include <cmath>
#include <cstddef>
#include <vector>

namespace {

bool same(const std::vector<ChunkPos>& a, const std::vector<ChunkPos>& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (!(a[i] == b[i])) return false;
    return true;
}

}  // namespace

TEST(test_sort_back_to_front) {
    // Eye at the centre of chunk (0,0,0); chunk centres 1, 3, and 2 chunks away along x.
    const Vector3 eye{8.f, 8.f, 8.f};
    const std::vector<ChunkPos> in{{1, 0, 0}, {3, 0, 0}, {2, 0, 0}};
    CHECK(same(sort_back_to_front(in, eye), {{3, 0, 0}, {2, 0, 0}, {1, 0, 0}}));
}

TEST(test_sort_empty_and_single) {
    const Vector3 eye{0.f, 70.f, 0.f};
    CHECK(sort_back_to_front({}, eye).empty());
    const std::vector<ChunkPos> one = sort_back_to_front({{4, 2, -7}}, eye);
    CHECK(one.size() == 1);
    CHECK(same(one, {{4, 2, -7}}));
}

TEST(test_sort_equal_distance_deterministic) {
    // (1,0,0) and (-1,0,0) are equally far from the eye; ties go by ChunkPos (x, then y, then z).
    const Vector3 eye{8.f, 8.f, 8.f};
    const std::vector<ChunkPos> want{{-1, 0, 0}, {1, 0, 0}};
    CHECK(same(sort_back_to_front({{1, 0, 0}, {-1, 0, 0}}, eye), want));
    CHECK(same(sort_back_to_front({{-1, 0, 0}, {1, 0, 0}}, eye), want));
    // Same distance, same x: y then z decide.
    const std::vector<ChunkPos> want2{{0, -1, 0}, {0, 0, -1}, {0, 0, 1}, {0, 1, 0}};
    CHECK(same(sort_back_to_front({{0, 0, 1}, {0, 1, 0}, {0, -1, 0}, {0, 0, -1}}, eye), want2));
}

// ---- Coverage Checklist additions ----

TEST(test_sort_negative_and_vertical) {
    // Eye inside chunk (-1, 4, -1) at block (-8, 72, -8); distances mix all three axes.
    const Vector3 eye{-8.f, 72.f, -8.f};
    const std::vector<ChunkPos> in{{-1, 4, -1}, {-1, 0, -1}, {-3, 4, -1}, {-1, 5, -2}};
    // Centres: (-8,72,-8) d=0; (-8,8,-8) d=64; (-40,72,-8) d=32; (-8,88,-24) d^2=512 (~22.6).
    CHECK(same(sort_back_to_front(in, eye), {{-1, 0, -1}, {-3, 4, -1}, {-1, 5, -2}, {-1, 4, -1}}));
}

TEST(test_sort_far_coordinates) {
    // Near block x = 1,000,000 (chunk 62500) the order still follows distance exactly.
    const Vector3 eye{1000008.f, 8.f, 8.f};
    const std::vector<ChunkPos> in{{62501, 0, 0}, {62503, 0, 0}, {62502, 0, 0}, {62500, 0, 0}};
    CHECK(same(sort_back_to_front(in, eye), {{62503, 0, 0}, {62502, 0, 0}, {62501, 0, 0}, {62500, 0, 0}}));
}

TEST(test_fog_range) {
    // Linear fog from 0.7 * rd * 16 to rd * 16 blocks.
    FogRange f = fog_range(8);
    CHECK_NEAR(f.start, 89.6, 1e-4);
    CHECK_NEAR(f.end, 128.0, 1e-4);
    f = fog_range(4);  // the --rd limits 4..16
    CHECK_NEAR(f.start, 44.8, 1e-4);
    CHECK_NEAR(f.end, 64.0, 1e-4);
    f = fog_range(16);
    CHECK_NEAR(f.start, 179.2, 1e-4);
    CHECK_NEAR(f.end, 256.0, 1e-4);
}

// ---- M3 Task 5: sun and moon placement ----

TEST(test_sun_direction) {
    // Angle 0 (sunrise) on the +X horizon, pi/2 straight up, pi (sunset) on the -X horizon,
    // 3 pi / 2 straight down (the moon, opposite, is then overhead).
    const Vector3 rise = sun_direction(0.f);
    CHECK_NEAR(rise.x, 1.0, 1e-6);
    CHECK_NEAR(rise.y, 0.0, 1e-6);
    CHECK_NEAR(rise.z, 0.0, 1e-6);
    const Vector3 noon = sun_direction(1.5707963f);
    CHECK_NEAR(noon.x, 0.0, 1e-6);
    CHECK_NEAR(noon.y, 1.0, 1e-6);
    const Vector3 set = sun_direction(3.1415927f);
    CHECK_NEAR(set.x, -1.0, 1e-6);
    CHECK_NEAR(set.y, 0.0, 1e-6);
    const Vector3 midnight = sun_direction(4.712389f);
    CHECK_NEAR(midnight.y, -1.0, 1e-6);
    for (float a = 0.f; a < 6.3f; a += 0.1f) {
        const Vector3 d = sun_direction(a);
        CHECK_NEAR(d.x * d.x + d.y * d.y + d.z * d.z, 1.0, 1e-5);
    }
}

TEST(test_sky_disc_faces_camera) {
    // A disc of `segments` triangles centred `distance` along dir from the eye, radius r, every
    // triangle counter-clockwise seen from the eye (raylib's DrawTriangle3D front face).
    const Vector3 eye{10.f, 70.f, -5.f};
    for (Vector3 dir : {Vector3{1, 0, 0}, Vector3{0, 1, 0}, Vector3{0, -1, 0}, Vector3{0.6f, 0.8f, 0}}) {
        const std::vector<Vector3> tri = sky_disc(eye, dir, 300.f, 20.f, 16);
        CHECK(tri.size() == 48);
        const Vector3 c{eye.x + dir.x * 300.f, eye.y + dir.y * 300.f, eye.z + dir.z * 300.f};
        for (std::size_t i = 0; i + 2 < tri.size(); i += 3) {
            const Vector3 a = tri[i], b = tri[i + 1], d = tri[i + 2];
            CHECK_NEAR(a.x, c.x, 1e-3);  // each triangle fans from the centre
            CHECK_NEAR(a.y, c.y, 1e-3);
            for (Vector3 p : {b, d}) {
                const double dx = p.x - c.x, dy = p.y - c.y, dz = p.z - c.z;
                CHECK_NEAR(std::sqrt(dx * dx + dy * dy + dz * dz), 20.0, 1e-3);
                CHECK_NEAR(dx * dir.x + dy * dir.y + dz * dir.z, 0.0, 1e-3);  // in the disc plane
            }
            const Vector3 e1{b.x - a.x, b.y - a.y, b.z - a.z}, e2{d.x - a.x, d.y - a.y, d.z - a.z};
            const Vector3 n{e1.y * e2.z - e1.z * e2.y, e1.z * e2.x - e1.x * e2.z, e1.x * e2.y - e1.y * e2.x};
            CHECK(n.x * -dir.x + n.y * -dir.y + n.z * -dir.z > 0.f);  // toward the eye
        }
    }
    CHECK(sky_disc(eye, Vector3{1, 0, 0}, 300.f, 20.f, 2).size() == 9);  // at least 3 segments
}
