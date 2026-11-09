// Creature looks: box models, the generated mob atlas, the leg swing, and the hurt and fade tints.
// Logic only (raylib for Vector3 and Color); render.* draws them.
#pragma once

#include "mobs.hpp"

#include "raylib.h"  // Vector3, Color only

#include <cstdint>
#include <vector>

constexpr int MOB_ATLAS_TILES = 4, MOB_TILE_PX = 16, MOB_ATLAS_PX = MOB_ATLAS_TILES * MOB_TILE_PX;

// Mob atlas cells, row-major in the 4x4 atlas. Not persisted.
enum MobTile : uint8_t {
    MT_TUSKER_HIDE,
    MT_TUSKER_FACE,
    MT_BONE,
    MT_WOOL,
    MT_FLUFF_FACE,
    MT_HORN,
    MT_HOPPER_FUR,
    MT_HOPPER_FACE,
    MT_GLOOM_BODY,
    MT_GLOOM_FACE,
    MT_THORN_STEM,
    MT_THORN_BULB,
    MT_CRAWLER_SHELL,
    MT_CRAWLER_FACE,
    MT_THORN,
    MT_SPARE,
};

// One box of a model in the mob's own frame: +X forward (the yaw direction), +Y up, +Z to the
// side, offset = box center from the feet center. A swinging part rotates about the Z axis through
// its top face center by part_swing.
struct BoxPart {
    Vector3 offset, size;
    uint8_t tile;
    bool swings;
    float phase;
};

// 4-8 boxes per creature; unknown types give the Tusker model.
const std::vector<BoxPart>& mob_model(MobType t);

// MOB_ATLAS_PX^2 opaque pixels, row-major: each tile a base color with deterministic speckle;
// face tiles carry eyes. Deterministic.
std::vector<Color> mob_atlas_pixels();

// Radians: sin(walk_distance * 6 + phase) * 0.6 for a swinging part, 0 otherwise.
float part_swing(const BoxPart& p, float walk_distance);

// The light color, turned red (green and blue x 0.4) while hurt > 0; a fading mob's alpha is
// health / max health (rounded).
Color mob_tint(const Mob& m, Color light);
