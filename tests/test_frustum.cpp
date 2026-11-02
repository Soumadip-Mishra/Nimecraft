// Tests for src/frustum.*: plane extraction from a raymath view-projection and AABB culling.
#include "frustum.hpp"
#include "test.hpp"

#include "raymath.h"

#include <cmath>

namespace {

constexpr double FOVY_DEG = 70.0, ASPECT = 16.0 / 9.0, NEAR = 0.05, FAR = 1000.0;

Frustum camera(Vector3 eye, Vector3 target) {
    const Matrix view = MatrixLookAt(eye, target, Vector3{0, 1, 0});
    const Matrix proj = MatrixPerspective(FOVY_DEG * DEG2RAD, ASPECT, NEAR, FAR);
    return frustum_from(MatrixMultiply(view, proj));
}

bool visible(const Frustum& f, float x0, float y0, float z0, float x1, float y1, float z1) {
    return aabb_visible(f, Vector3{x0, y0, z0}, Vector3{x1, y1, z1});
}

}  // namespace

TEST(test_frustum_basic) {
    const Frustum f = camera(Vector3{0, 0, 0}, Vector3{0, 0, -1});
    CHECK(visible(f, -1, -1, -10, 1, 1, -8));
    CHECK(!visible(f, -1, -1, 8, 1, 1, 10));         // behind
    CHECK(!visible(f, -200, -1, -10, -190, 1, -8));  // far left
    CHECK(!visible(f, -1, -1, -2000, 1, 1, -1500));  // past far
    CHECK(visible(f, -13, -1, -10, -12, 1, -8));     // straddles the left plane
}

TEST(test_frustum_planes_normalized) {
    const Frustum f = camera(Vector3{3, 4, 5}, Vector3{4, 4, 5});
    for (const Vector4& p : f.planes)
        CHECK_NEAR(std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z), 1.0, 1e-5);
}

TEST(test_frustum_side_plane_boundary) {
    // At z = -10 the left plane sits at x = -10 * tan(35 deg) * 16/9 = -12.4481.
    const Frustum f = camera(Vector3{0, 0, 0}, Vector3{0, 0, -1});
    CHECK(!visible(f, -14, -1, -10, -12.5f, 1, -10));
    CHECK(visible(f, -14, -1, -10, -12.4f, 1, -10));
    // Top plane: y = 10 * tan(35 deg) = 7.0021 at z = -10.
    CHECK(!visible(f, -1, 7.05f, -10, 1, 9, -10));
    CHECK(visible(f, -1, 6.95f, -10, 1, 9, -10));
}

TEST(test_frustum_rotated) {
    const Frustum f = camera(Vector3{0, 0, 0}, Vector3{1, 0, 0});
    CHECK(visible(f, 8, -1, -1, 10, 1, 1));
    CHECK(!visible(f, -1, -1, -10, 1, 1, -8));
}

TEST(test_frustum_looking_down) {
    const Frustum f = camera(Vector3{0, 100, 0}, Vector3{0.001f, 0, 0});
    CHECK(visible(f, -8, 64, -8, 8, 80, 8));      // the chunk under the camera
    CHECK(!visible(f, -8, 120, -8, 8, 136, 8));   // the chunk above it
}

TEST(test_frustum_box_around_eye) {
    const Frustum f = camera(Vector3{0, 0, 0}, Vector3{0, 0, -1});
    CHECK(visible(f, -1, -1, -1, 1, 1, 1));
}

TEST(test_frustum_chunk_far_coords) {
    const Frustum f = camera(Vector3{1000000, 80, 1000000}, Vector3{1000000, 80, 999999});
    CHECK(visible(f, 999992, 64, 999964, 1000008, 80, 999980));     // 20..36 blocks ahead
    CHECK(!visible(f, 999992, 64, 1000020, 1000008, 80, 1000036));  // 20..36 blocks behind
}
