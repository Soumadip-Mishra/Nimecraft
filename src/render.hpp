// GPU side of chunk rendering: the block atlas, one raylib Mesh per chunk, frustum culling, and the
// block highlight. Main thread only (it owns the GL context); covered by --smoke and manual checks.
#pragma once

#include "coords.hpp"
#include "mesher.hpp"

#include "raylib.h"

#include <unordered_map>

Image generate_atlas();  // 128x128, deterministic pixel art for each Tile

class Renderer {
public:
    void init();                                 // after InitWindow
    void upload(ChunkPos p, const MeshData& m);  // replaces; empty mesh = remove
    void remove(ChunkPos p);
    int draw(const Camera3D& cam, float aspect);  // returns chunks drawn after culling
    void draw_highlight(int x, int y, int z);     // DrawCubeWires 1.002 size, BLACK
    void shutdown();

private:
    Material material_{};
    bool ready_ = false;
    std::unordered_map<ChunkPos, Mesh, ChunkPosHash> meshes_;
};
