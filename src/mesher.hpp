// Face-culled chunk meshes built from a padded block snapshot.
// snapshot() reads the World and runs on the main thread only; build_mesh() is pure and runs on
// worker threads (Global Constraints).
#pragma once

#include "block.hpp"
#include "coords.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

class World;

constexpr int ATLAS_TILES = 8;  // 8x8 grid of 16 px tiles = 128x128 atlas

enum Shade : uint8_t { SHADE_TOP = 255, SHADE_X = 204, SHADE_Z = 166, SHADE_BOTTOM = 128 };

// The chunk's 16^3 blocks plus a 1-block border: local coordinates -1..16 on each axis.
struct Padded {
    std::array<BlockId, 18 * 18 * 18> b{};
    BlockId at(int lx, int ly, int lz) const;  // lx, ly, lz in [-1, 16]
};

// Copies chunk p and its border. Below the world reads DEEPSTONE, above it and unloaded columns
// read AIR, exactly as World::get does.
Padded snapshot(const World& w, ChunkPos p);  // main thread only

struct MeshData {
    std::vector<float> positions, texcoords, normals;  // 3, 2, 3 per vertex, world coordinates
    std::vector<uint8_t> colors;                       // RGBA, R=G=B=shade, A=255
    std::vector<uint16_t> indices;                     // 6 per face
    std::size_t vertex_count() const;
};

// One quad per face of an opaque block whose neighbor is not opaque. Each face uses the block's
// tile for that face (u0 = (t % 8) / 8, v0 = (t / 8) / 8, size 1/8), its Shade, and two
// triangles that are counter-clockwise seen from outside.
MeshData build_mesh(const Padded& pad, ChunkPos p);  // pure, thread-safe
