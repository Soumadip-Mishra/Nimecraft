// GPU side of chunk rendering: the block atlas, one raylib Mesh per chunk, frustum culling, and the
// block highlight. Main thread only (it owns the GL context); covered by --smoke and manual checks.
#pragma once

#include "coords.hpp"
#include "mesher.hpp"

#include "raylib.h"

#include <array>
#include <unordered_map>
#include <vector>

// 128x128, deterministic pixel art for each Tile. Cutout tiles (leaves, plants) use alpha 0 or
// 255 only; Translucent tiles (water, ice) are partly transparent everywhere; all others opaque.
Image generate_atlas();

// Chunks ordered farthest first by the distance from eye to each chunk's centre (p * 16 + 8),
// ties broken by ChunkPos (x, then y, then z, ascending), so the order never depends on the input.
std::vector<ChunkPos> sort_back_to_front(std::vector<ChunkPos> chunks, Vector3 eye);

// Linear fog toward the sky colour over horizontal distance: start 0.7 * rd * 16, end rd * 16.
// rd is the render distance in columns, 4..16 as parse_cli accepts (rd >= 1 keeps end > start).
struct FogRange {
    float start, end;
};
FogRange fog_range(int rd);

class Renderer {
public:
    void init();                                  // after InitWindow
    void set_fog(FogRange range, Color sky);      // before draw
    void upload(ChunkPos p, const ChunkMesh& m);  // replaces; empty mesh = remove
    void remove(ChunkPos p);
    // Opaque, then Cutout (alpha < 0.5 discarded), then Translucent layers of every visible
    // chunk; the Translucent layer is drawn back to front with alpha blending and depth writes
    // off. Returns chunks drawn after culling.
    int draw(const Camera3D& cam, float aspect);
    void draw_highlight(int x, int y, int z);  // DrawCubeWires 1.002 size, BLACK
    void shutdown();

private:
    // Per layer (Opaque, Cutout, Translucent), the split_mesh parts as GPU meshes.
    using ChunkGpu = std::array<std::vector<Mesh>, 3>;

    Material material_{};
    bool ready_ = false;
    int loc_cam_ = -1, loc_fog_color_ = -1, loc_fog_start_ = -1, loc_fog_end_ = -1, loc_cutoff_ = -1;
    FogRange fog_{1.0e6f, 2.0e6f};
    Color sky_{135, 206, 235, 255};
    std::unordered_map<ChunkPos, ChunkGpu, ChunkPosHash> meshes_;
};
