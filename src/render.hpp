// GPU side of chunk rendering: the block atlas, one raylib Mesh per chunk, frustum culling, and the
// block highlight. Main thread only (it owns the GL context); covered by --smoke and manual checks.
#pragma once

#include "coords.hpp"
#include "items.hpp"
#include "mesher.hpp"
#include "mobs.hpp"

#include "raylib.h"

#include <array>
#include <unordered_map>
#include <vector>

// 128x128, deterministic pixel art for each Tile. Cutout tiles (leaves, plants) use alpha 0 or
// 255 only; Translucent tiles (water, ice) are partly transparent everywhere; all others opaque.
Image generate_atlas();

// 128x128 icons for the non-block items, one 16 px cell per item at ItemDef::icon (id - STICK):
// opaque pixels on a clear background. Tools are one shape per kind tinted by tier; unused cells
// are magenta. Deterministic.
Image generate_item_atlas();

// The game icon (M8 Task 7): size x size RGBA (size clamped to 16..1024), an isometric grass block
// drawn from the generated atlas's grass top and grass side tiles with three face shades (top 1.0,
// left 0.8, right 0.6) on a fully transparent background; every pixel is fully clear or fully
// opaque. Deterministic. CPU only (no window needed); free it with UnloadImage.
Image generate_icon(int size);

// Chunks ordered farthest first by the distance from eye to each chunk's centre (p * 16 + 8),
// ties broken by ChunkPos (x, then y, then z, ascending), so the order never depends on the input.
std::vector<ChunkPos> sort_back_to_front(std::vector<ChunkPos> chunks, Vector3 eye);

// Linear fog toward the sky colour over horizontal distance: start 0.7 * rd * 16, end rd * 16.
// rd is the render distance in columns, 4..16 as parse_cli accepts (rd >= 1 keeps end > start).
struct FogRange {
    float start, end;
};
FogRange fog_range(int rd);

// Unit direction from the eye to the sun for world_time's sun_angle: (cos a, sin a, 0), so it rises
// on the +X horizon, stands overhead at pi/2, and sets on -X. The moon is the opposite direction.
Vector3 sun_direction(float angle);

// A disc of max(3, segments) triangles (3 vertices each, fanned from the centre) at eye +
// dir * distance, radius `radius`, in the plane facing the eye, each counter-clockwise seen from
// the eye. dir is a unit vector.
std::vector<Vector3> sky_disc(Vector3 eye, Vector3 dir, float distance, float radius, int segments);

class Renderer {
public:
    void init();                                  // after InitWindow
    void set_fog(FogRange range, Color sky);      // before draw
    void set_daylight(float day_light);           // before draw: world_time's daylight_at
    void upload(ChunkPos p, const ChunkMesh& m);  // replaces; empty mesh = remove
    void remove(ChunkPos p);
    // Sun and moon discs at sun_angle, inside BeginMode3D before draw(): no depth writes, so
    // every chunk drawn afterwards covers them.
    void draw_sky(const Camera3D& cam, float sun_angle);
    // Opaque, then Cutout (alpha < 0.5 discarded), then Translucent layers of every visible
    // chunk; the Translucent layer is drawn back to front with alpha blending and depth writes
    // off. Vertex colors carry light (R sky, G block, B shade * AO, see mesher.hpp); the shader
    // lights each texel by max(sky * daylight, block). Returns chunks drawn after culling.
    int draw(const Camera3D& cam, float aspect);
    void draw_highlight(int x, int y, int z);  // DrawCubeWires 1.002 size, BLACK
    // Crack overlay T_CRACK_0 + stage (0..3) on the six faces of block (x, y, z), slightly outside it.
    void draw_crack(int x, int y, int z, int stage);
    // A dropped item: a billboard of its icon (block atlas tile or item icon), size in blocks.
    void draw_item(const Camera3D& cam, ItemId id, Vector3 pos, float size, Color tint);
    // A creature: its mob_model boxes from the mob atlas, turned to its yaw, legs swung by its walk
    // distance, colored by tint (mob_tint: light, hurt red, fade alpha; alpha-blended below 255).
    void draw_mob(const Mob& m, Color tint);
    void draw_thorn(Vector3 pos);  // a small thorn projectile
    // The atlases for 2D icons (ui_draw); valid between init and shutdown.
    Texture2D block_atlas() const;
    Texture2D item_atlas() const;
    // Source rectangle (in pixels) of an item's icon in block_atlas() (block items) or item_atlas().
    static Rectangle icon_rect(ItemId id);
    void shutdown();

private:
    // Per layer (Opaque, Cutout, Translucent), the split_mesh parts as GPU meshes.
    using ChunkGpu = std::array<std::vector<Mesh>, 3>;

    Material material_{};
    Texture2D items_tex_{};
    Texture2D mob_tex_{};
    bool ready_ = false;
    int loc_cam_ = -1, loc_fog_color_ = -1, loc_fog_start_ = -1, loc_fog_end_ = -1, loc_cutoff_ = -1,
        loc_daylight_ = -1;
    FogRange fog_{1.0e6f, 2.0e6f};
    float daylight_ = 1.f;
    Color sky_{135, 206, 235, 255};
    std::unordered_map<ChunkPos, ChunkGpu, ChunkPosHash> meshes_;
};
