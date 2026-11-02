// Voxel ray traversal (Amanatides-Woo DDA) against the World's solid blocks.
#pragma once

#include "raylib.h"  // Vector3 only

class World;

struct RayHit {
    bool hit = false;
    int x, y, z;     // the block hit
    int nx, ny, nz;  // face entered; (0,0,0) when the origin is inside the block
    float distance;  // along the ray to where it enters the block; 0 when inside
};

// dir normalized; hits solid blocks with 0 <= y < 256 within max_dist. Unloaded columns are AIR.
// A non-finite max_dist misses (an endless horizontal ray would never stop).
RayHit raycast_voxel(const World& w, Vector3 origin, Vector3 dir, float max_dist);
