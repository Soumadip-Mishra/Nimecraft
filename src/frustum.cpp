#include "frustum.hpp"

#include <cmath>
#include <cstddef>

namespace {

Vector4 normalized(float a, float b, float c, float d) {
    const float len = std::sqrt(a * a + b * b + c * c);
    return Vector4{a / len, b / len, c / len, d / len};
}

}  // namespace

Frustum frustum_from(Matrix m) {
    // Rows of the clip matrix: clip = (r0.v, r1.v, r2.v, r3.v) for v = (x, y, z, 1).
    const float r0[4] = {m.m0, m.m4, m.m8, m.m12};
    const float r1[4] = {m.m1, m.m5, m.m9, m.m13};
    const float r2[4] = {m.m2, m.m6, m.m10, m.m14};
    const float r3[4] = {m.m3, m.m7, m.m11, m.m15};
    const float* rows[3] = {r0, r1, r2};
    Frustum f{};
    for (std::size_t i = 0; i < 3; ++i) {
        const float* r = rows[i];
        // -w <= clip_i <= w gives the planes r3 + ri >= 0 and r3 - ri >= 0.
        f.planes[2 * i] = normalized(r3[0] + r[0], r3[1] + r[1], r3[2] + r[2], r3[3] + r[3]);
        f.planes[2 * i + 1] = normalized(r3[0] - r[0], r3[1] - r[1], r3[2] - r[2], r3[3] - r[3]);
    }
    return f;
}

bool aabb_visible(const Frustum& f, Vector3 min, Vector3 max) {
    for (const Vector4& p : f.planes) {
        // The box corner farthest along the plane normal; if even it is outside, all of it is.
        const float x = p.x >= 0 ? max.x : min.x;
        const float y = p.y >= 0 ? max.y : min.y;
        const float z = p.z >= 0 ? max.z : min.z;
        if (p.x * x + p.y * y + p.z * z + p.w < 0) return false;
    }
    return true;
}
