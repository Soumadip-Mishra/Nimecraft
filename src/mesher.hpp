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

// The chunk's 16^3 blocks plus a 1-block border: local coordinates -1..16 on each axis, with each
// cell's light packed as in Chunk::light (high nibble sky, low nibble block).
struct Padded {
    std::array<BlockId, 18 * 18 * 18> b{};
    std::array<uint8_t, 18 * 18 * 18> light{};
    std::array<uint8_t, 18 * 18 * 18> meta{};        // Chunk::meta; 0 outside loaded chunks
    BlockId at(int lx, int ly, int lz) const;        // lx, ly, lz in [-1, 16]
    uint8_t light_at(int lx, int ly, int lz) const;  // same range
    uint8_t meta_at(int lx, int ly, int lz) const;   // same range
};

// Copies chunk p and its border, blocks, light, and meta. Below the world reads DEEPSTONE with no
// light, above it AIR with sky 15, unloaded columns AIR with no light (blocks exactly as
// World::get does).
Padded snapshot(const World& w, ChunkPos p);  // main thread only

// Quads only: every quad is 4 consecutive vertices and 6 consecutive indices into them.
struct MeshData {
    std::vector<float> positions, texcoords, normals;  // 3, 2, 3 per vertex, world coordinates
    // RGBA per vertex: R = sky light * 17, G = block light * 17, B = face shade * AO, A = 255.
    std::vector<uint8_t> colors;
    // 6 per quad. 32-bit: a Cutout layer can exceed 65,536 vertices (leaves never cull leaves),
    // so the renderer cuts layers with split_mesh before uploading 16-bit GPU indices.
    std::vector<uint32_t> indices;
    std::size_t vertex_count() const;
};

struct ChunkMesh {
    MeshData opaque, cutout, translucent;  // one per Layer
    bool empty() const;                    // no vertices in any layer
};

// Every block goes into the mesh of its BlockDef::layer. Each face uses the block's tile for that
// face (u0 = (t % 8) / 8, v0 = (t / 8) / 8, size 1/8), its Shade, and two triangles that are
// counter-clockwise seen from outside.
// - Cube, Opaque or Cutout: a face is drawn unless its neighbor is opaque.
// - Cube or Liquid, Translucent: a face is drawn unless its neighbor is opaque or has the same id.
// - Liquid: when the block above is not the same id, the top face and the top edges of the side
//   faces sit at liquid_top(meta): a flowing level 1..7 at y + (8 - level) / 9, a source (0),
//   falling water (8), and any other meta at y + 0.875.
// - Cross: two diagonal quads, each emitted for both sides (16 vertices), shade SHADE_X, never
//   culled. SUNROOT_CROP uses tile T_SUNROOT_0 + its meta (stage), meta above 3 as 3.
// AIR and unknown ids (>= blk::COUNT) are never meshed.
// Smooth light and ambient occlusion (cube and liquid faces): a vertex touches 4 cells on the
// face side - the face's neighbor cell, side1 and side2 (that cell stepped toward the vertex
// along each face axis), and the corner (both steps). R and G average sky and block light over
// those of the 4 that are not opaque, times 17, rounded. AO level = (side1 && side2) ? 0 :
// 3 - (side1 + side2 + corner), counting opaque cells; B = shade * {0.55, 0.7, 0.85, 1.0}[level],
// rounded. A quad whose levels give a0 + a2 > a1 + a3 is split along its 1-3 diagonal instead of
// 0-2. Cross quads take the light of the plant's own cell at every vertex and B = SHADE_X.
ChunkMesh build_mesh(const Padded& pad, ChunkPos p);  // pure, thread-safe

constexpr std::size_t MAX_PART_VERTICES = 65536;  // vertices a 16-bit index reaches

// Cuts m into consecutive parts of at most max_vertices vertices, whole quads only, with each
// part's indices rebased to start at its own first vertex. max_vertices is rounded down to a
// multiple of 4, and a limit below 4 still puts one quad in each part. An empty mesh gives no
// parts; a mesh within the limit gives one part equal to m.
std::vector<MeshData> split_mesh(const MeshData& m, std::size_t max_vertices = MAX_PART_VERTICES);
