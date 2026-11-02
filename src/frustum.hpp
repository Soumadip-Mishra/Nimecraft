// View frustum planes and box culling.
#pragma once

#include "raylib.h"  // Vector3, Vector4, Matrix only

#include <array>

struct Frustum {
    std::array<Vector4, 6> planes;  // normalized, inside when dot >= 0
};

// Gribb-Hartmann on raymath column-major m0..m15, where
// view_proj = MatrixMultiply(MatrixLookAt(...), MatrixPerspective(...)) (raylib order).
Frustum frustum_from(Matrix view_proj);

// False only when the box lies entirely outside one plane (conservative: may keep a hidden box).
bool aabb_visible(const Frustum& f, Vector3 min, Vector3 max);
