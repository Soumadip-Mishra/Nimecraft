// Tests for the window-free decisions in src/render.*: translucent draw order and the fog range
// (M2 Task 6).
#include "coords.hpp"
#include "render.hpp"
#include "test.hpp"

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
