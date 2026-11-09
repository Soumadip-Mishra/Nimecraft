#include "render.hpp"

#include "block.hpp"
#include "frustum.hpp"
#include "mob_models.hpp"

#include "raymath.h"
#include "rlgl.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

constexpr int TILE_PX = 16;
constexpr int ATLAS_PX = ATLAS_TILES * TILE_PX;  // 128

constexpr float FOG_START_FRACTION = 0.7f;
constexpr float CUTOUT_ALPHA = 0.5f;  // Opaque and Cutout layers discard texels below this

// Sun and moon: discs SKY_DISTANCE from the eye (inside the default far plane of 1000).
constexpr float SKY_DISTANCE = 400.f, SUN_RADIUS = 26.f, MOON_RADIUS = 18.f;
constexpr int SKY_SEGMENTS = 24;
constexpr Color SUN_COLOR{255, 244, 196, 255}, SUN_HALO{255, 214, 140, 70}, MOON_COLOR{214, 222, 240, 255};

// Chunk shader, GLSL 330. Meshes are drawn with an identity model matrix, so vertexPosition is the
// world position the fog distance is measured from.
constexpr const char* CHUNK_VS = R"(#version 330
in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec4 vertexColor;
uniform mat4 mvp;
out vec2 fragTexCoord;
out vec4 fragColor;
out vec3 fragWorld;
void main() {
    fragTexCoord = vertexTexCoord;
    fragColor = vertexColor;
    fragWorld = vertexPosition;
    gl_Position = mvp * vec4(vertexPosition, 1.0);
}
)";

constexpr const char* CHUNK_FS = R"(#version 330
in vec2 fragTexCoord;
in vec4 fragColor;
in vec3 fragWorld;
uniform sampler2D texture0;
uniform vec4 colDiffuse;
uniform vec3 camPos;
uniform vec3 fogColor;
uniform float fogStart;
uniform float fogEnd;
uniform float alphaCutoff;
uniform float daylight;
out vec4 finalColor;
void main() {
    // fragColor: r sky light, g block light (both 0..1), b face shade * ambient occlusion.
    vec4 tex = texture(texture0, fragTexCoord) * colDiffuse;
    float light = max(fragColor.r * daylight, fragColor.g);
    vec4 c = vec4(tex.rgb * fragColor.b * (0.06 + 0.94 * pow(light, 1.6)), tex.a);
    if (c.a < alphaCutoff) discard;
    float d = length(fragWorld.xz - camPos.xz);
    float f = clamp((d - fogStart) / (fogEnd - fogStart), 0.0, 1.0);
    finalColor = vec4(mix(c.rgb, fogColor, f), c.a);
}
)";

struct Rgb {
    int r, g, b;
    int a = 255;
};

// Integer hash of a pixel and a salt, for deterministic texture noise.
uint32_t pixel_hash(int x, int y, uint32_t salt) {
    uint32_t h = static_cast<uint32_t>(x) * 0x8DA6B343u ^ static_cast<uint32_t>(y) * 0xD8163841u ^
                 salt * 0xCB1AB31Fu;
    h ^= h >> 13;
    h *= 0x5BD1E995u;
    h ^= h >> 15;
    return h;
}

// Scales a color by a factor in percent, clamped to a byte. The result is opaque.
Rgb shade(Rgb c, int percent) {
    auto f = [percent](int v) { return std::clamp(v * percent / 100, 0, 255); };
    return Rgb{f(c.r), f(c.g), f(c.b)};
}

Rgb with_alpha(Rgb c, int a) {
    c.a = a;
    return c;
}

// Base color varied by up to +-span percent of noise.
Rgb noisy(Rgb base, int x, int y, uint32_t salt, int span) {
    const int n = static_cast<int>(pixel_hash(x, y, salt) % static_cast<uint32_t>(2 * span + 1));
    return shade(base, 100 - span + n);
}

constexpr Rgb GRASS{96, 158, 56}, DIRT{128, 92, 62}, STONE{122, 124, 128}, SAND{214, 201, 152},
              BARK{98, 74, 46}, WOOD{180, 146, 94}, LEAF{56, 112, 44}, PLANK{166, 128, 78},
              DEEP{58, 56, 66}, WATER{44, 92, 196}, GRAVEL{132, 126, 120}, CLAY{150, 158, 172},
              SNOW{236, 240, 246}, ICE{150, 192, 236}, SANDSTONE{206, 186, 128}, CACTUS{62, 126, 56},
              SPINE{220, 214, 170}, PINE_BARK{72, 52, 38}, PINE_WOOD{168, 136, 92},
              PINE_LEAF{36, 84, 56}, PETAL{208, 76, 118}, POLLEN{236, 198, 64}, QUARTZ{230, 224, 236},
              COAL{38, 38, 42}, COPPER{198, 112, 62}, TIN{190, 196, 204}, IRON{206, 164, 132},
              SILVER{232, 238, 246}, FLAME{238, 136, 40}, FLAME_CORE{255, 240, 170},
              IRON_FRAME{58, 58, 64}, GLOW{246, 186, 92}, STEM{214, 210, 190}, GLOWCAP_TEAL{64, 196, 180},
              SPOT{200, 255, 240}, KILN_BRICK{150, 84, 60}, CRATE_WOOD{150, 112, 70}, GLASS_TINT{196, 226, 236},
              BLANKET{70, 104, 150}, PILLOW{226, 220, 200}, MOSS{84, 120, 60};
constexpr Rgb CLEAR{0, 0, 0, 0};
constexpr int WATER_ALPHA = 170, ICE_ALPHA = 200, GLASS_ALPHA = 60, GLASS_RIM_ALPHA = 170, GLASS_GLINT_ALPHA = 200;

// A grass or snow fringe of uneven depth (3..5 px) over dirt.
Rgb fringe(Rgb top, int span, int x, int y, uint32_t salt) {
    const int depth = 3 + static_cast<int>(pixel_hash(x, 0, salt) % 3);
    return y < depth ? noisy(top, x, y, salt, span) : noisy(DIRT, x, y, salt + 50, 10);
}

// Square growth rings around the center, with bark around the edge.
Rgb rings(Rgb wood, Rgb bark, int x, int y, uint32_t salt) {
    const int ring = std::max(std::abs(2 * x - 15), std::abs(2 * y - 15)) / 2;  // 0..7
    if (ring == 7) return noisy(bark, x, y, salt, 8);
    return noisy(ring % 2 == 0 ? wood : shade(wood, 84), x, y, salt, 5);
}

// Stone with 2x2-pixel clusters of an ore color.
Rgb ore(Rgb color, int x, int y, uint32_t salt) {
    const bool cluster = pixel_hash(x / 2, y / 2, salt + 11) % 6 == 0 && pixel_hash(x, y, salt + 13) % 4 != 0;
    if (cluster) return noisy(color, x, y, salt, 8);
    return noisy(STONE, x, y, salt, 6);
}

// Color of pixel (x, y), both in [0, 16), of tile t. Original pixel art, made only from code.
Rgb tile_pixel(int t, int x, int y) {
    const uint32_t salt = static_cast<uint32_t>(t) + 1;
    switch (t) {
    case T_GRASS_TOP: return noisy(GRASS, x, y, salt, 12);
    case T_GRASS_SIDE: return fringe(GRASS, 12, x, y, salt);
    case T_DIRT: return noisy(DIRT, x, y, salt, 12);
    case T_STONE: {
        const bool speck = pixel_hash(x, y, salt + 7) % 9 == 0;
        return noisy(speck ? shade(STONE, 78) : STONE, x, y, salt, 6);
    }
    case T_SAND: return noisy(SAND, x, y, salt, 5);
    case T_LOG_SIDE: {
        // Vertical bark grooves every 4 px, jittered per column.
        const bool groove = (x + static_cast<int>(pixel_hash(x, 0, salt) % 2)) % 4 == 0;
        return noisy(groove ? shade(BARK, 72) : BARK, x, y, salt, 8);
    }
    case T_LOG_TOP: return rings(WOOD, BARK, x, y, salt);
    case T_LEAVES: {
        if (pixel_hash(x, y, salt + 9) % 5 == 0) return CLEAR;  // gaps between the leaves
        const uint32_t h = pixel_hash(x, y, salt) % 7;
        return noisy(h == 0 ? shade(LEAF, 65) : (h == 1 ? shade(LEAF, 120) : LEAF), x, y, salt, 8);
    }
    case T_PLANKS: {
        // Four boards of 4 px; each board has its own seam position and tone.
        const int board = y / 4;
        const int seam = static_cast<int>(pixel_hash(board, 0, salt) % 16);
        if (y % 4 == 3 || x == seam) return shade(PLANK, 70);
        return noisy(shade(PLANK, 92 + board * 4), x, y, salt, 5);
    }
    case T_DEEPSTONE: {
        const bool vein = pixel_hash(x, y, salt + 3) % 11 == 0;
        return noisy(vein ? shade(DEEP, 140) : DEEP, x, y, salt, 8);
    }
    case T_WATER: {
        // Light ripple dashes on a blue body.
        const bool ripple = (x + y * 3 + static_cast<int>(pixel_hash(0, y, salt) % 4)) % 8 == 0;
        return with_alpha(noisy(ripple ? shade(WATER, 125) : WATER, x, y, salt, 4), WATER_ALPHA);
    }
    case T_GRAVEL: {
        // Pebbles of about 3 px, each with its own tone.
        const int tone = 78 + static_cast<int>(pixel_hash(x / 3, y / 3, salt) % 5) * 11;
        return noisy(shade(GRAVEL, tone), x, y, salt, 6);
    }
    case T_CLAY: return noisy(y % 5 == 0 ? shade(CLAY, 92) : CLAY, x, y, salt, 4);
    case T_SNOW: return noisy(SNOW, x, y, salt, 3);
    case T_SNOW_SIDE: return fringe(SNOW, 3, x, y, salt);
    case T_ICE: {
        const bool streak = (x - y + 16) % 7 == 0;
        return with_alpha(noisy(streak ? shade(ICE, 112) : ICE, x, y, salt, 3), ICE_ALPHA);
    }
    case T_SANDSTONE_TOP: {
        const bool rim = x == 0 || x == 15 || y == 0 || y == 15;
        return noisy(rim ? shade(SANDSTONE, 88) : SANDSTONE, x, y, salt, 4);
    }
    case T_SANDSTONE_SIDE: {
        // Horizontal bedding lines every 5 px under a lighter cap.
        if (y % 5 == 4) return noisy(shade(SANDSTONE, 82), x, y, salt, 3);
        return noisy(y < 3 ? shade(SANDSTONE, 108) : SANDSTONE, x, y, salt, 4);
    }
    case T_CACTUS_TOP: {
        const int ring = std::max(std::abs(2 * x - 15), std::abs(2 * y - 15)) / 2;  // 0..7
        if (ring == 0) return shade(CACTUS, 70);
        return noisy(ring >= 6 ? CACTUS : shade(CACTUS, 125), x, y, salt, 5);
    }
    case T_CACTUS_SIDE: {
        if (pixel_hash(x, y, salt + 5) % 23 == 0) return SPINE;
        return noisy(x % 4 == 1 ? shade(CACTUS, 80) : CACTUS, x, y, salt, 6);
    }
    case T_PINE_LOG_SIDE: {
        // Staggered bark plates.
        const bool groove = (x + (y / 3) * 2) % 5 == 0;
        return noisy(groove ? shade(PINE_BARK, 70) : PINE_BARK, x, y, salt, 8);
    }
    case T_PINE_LOG_TOP: return rings(PINE_WOOD, PINE_BARK, x, y, salt);
    case T_PINE_LEAVES: {
        if (pixel_hash(x, y, salt + 9) % 4 == 0) return CLEAR;
        return noisy((x + y) % 3 == 0 ? shade(PINE_LEAF, 75) : PINE_LEAF, x, y, salt, 8);
    }
    case T_TALL_GRASS: {
        // Blades of different heights rising from the bottom edge, gaps between them.
        const int top = 3 + static_cast<int>(pixel_hash(x, 0, salt + 1) % 9);
        const bool blade = pixel_hash(x, 1, salt) % 3 != 0;
        if (!blade || y < top) return CLEAR;
        return noisy(shade(GRASS, 90 + (15 - y) * 2), x, y, salt, 6);
    }
    case T_FLOWER: {
        // A diamond of petals around a pollen center on a short stem with two leaves.
        const int d = std::abs(2 * x - 15) + std::abs(2 * y - 9);
        if (d <= 4) return POLLEN;
        if (d <= 9) return noisy(PETAL, x, y, salt, 6);
        const bool stem = (x == 7 || x == 8) && y >= 8;
        const bool leaf = (y == 11 && (x == 9 || x == 10)) || (y == 12 && (x == 5 || x == 6));
        if (stem || leaf) return shade(GRASS, 80);
        return CLEAR;
    }
    case T_COAL_ORE: return ore(COAL, x, y, salt);
    case T_COPPER_ORE: return ore(COPPER, x, y, salt);
    case T_TIN_ORE: return ore(TIN, x, y, salt);
    case T_IRON_ORE: return ore(IRON, x, y, salt);
    case T_SILVER_ORE: return ore(SILVER, x, y, salt);
    case T_QUARTZ: {
        // Crossing facet lines.
        if ((x + y) % 6 == 0) return shade(QUARTZ, 82);
        return noisy((x - y + 16) % 6 == 0 ? shade(QUARTZ, 110) : QUARTZ, x, y, salt, 3);
    }
    case T_TORCH: {
        // A short wooden stick with a flame on top: white-yellow core, orange rim.
        if ((x == 7 || x == 8) && y >= 7) return noisy(WOOD, x, y, salt, 8);
        const int d = std::abs(2 * x - 15) + std::abs(2 * y - 9);  // around (7.5, 4.5)
        if (d <= 2) return FLAME_CORE;
        if (d <= 5 && y >= 2) return noisy(FLAME, x, y, salt, 6);
        return CLEAR;
    }
    case T_LANTERN: {
        // Dark iron frame and bars around warm glowing glass, with cut corners.
        if ((x <= 1 || x >= 14) && (y <= 1 || y >= 14)) return CLEAR;
        const bool frame = x <= 1 || x >= 14 || y <= 1 || y >= 14 || x == 7 || x == 8;
        if (frame) return noisy(IRON_FRAME, x, y, salt, 6);
        const int d = std::abs(2 * y - 15);
        return noisy(shade(GLOW, 112 - d * 2), x, y, salt, 4);
    }
    case T_GLOWCAP: {
        // A pale stem under a teal cap with bright spots.
        if ((x == 7 || x == 8) && y >= 9) return noisy(STEM, x, y, salt, 5);
        const int dx = 2 * x - 15, dy = 2 * y - 17;  // cap centred at (7.5, 8.5), drawn above y 9
        if (y <= 8 && dx * dx + 4 * dy * dy <= 144) {
            if (pixel_hash(x, y, salt + 3) % 7 == 0) return SPOT;
            return noisy(GLOWCAP_TEAL, x, y, salt, 8);
        }
        return CLEAR;
    }
    case T_WORKBENCH_TOP: {
        // A plank top with a darker border and a carved square grid of four work fields.
        const bool rim = x == 0 || x == 15 || y == 0 || y == 15;
        const bool groove = x == 7 || y == 7 || x == 2 || x == 13 || y == 2 || y == 13;
        if (rim) return noisy(shade(PLANK, 62), x, y, salt, 5);
        return noisy(groove ? shade(PLANK, 78) : shade(PLANK, 108), x, y, salt, 5);
    }
    case T_WORKBENCH_SIDE: {
        // Planks with two dark legs and a tool rack shelf across the upper third.
        if (y <= 1) return noisy(shade(PLANK, 70), x, y, salt, 5);
        if (y == 5 || y == 6) return noisy(shade(BARK, 110), x, y, salt, 6);
        const bool leg = (x <= 2 || x >= 13) && y >= 7;
        if (leg) return noisy(shade(BARK, 96), x, y, salt, 6);
        if (y >= 7) return y % 4 == 3 ? shade(PLANK, 66) : noisy(shade(PLANK, 86), x, y, salt, 5);
        return noisy(PLANK, x, y, salt, 5);
    }
    case T_KILN_TOP: {
        // Rough fired bricks around a dark round vent.
        const int dx = 2 * x - 15, dy = 2 * y - 15;
        if (dx * dx + dy * dy <= 36) return noisy(shade(KILN_BRICK, 30), x, y, salt, 10);
        const bool mortar = y % 4 == 0 || (x + (y / 4) % 2 * 4) % 8 == 0;
        return noisy(mortar ? shade(KILN_BRICK, 70) : KILN_BRICK, x, y, salt, 7);
    }
    case T_KILN_SIDE: {
        // Bricks with an arched fire mouth glowing at the bottom.
        const int dx = std::abs(2 * x - 15);
        const bool mouth = y >= 8 && y <= 14 && dx <= 7 && (y >= 10 || dx <= 5);
        if (mouth) return y >= 12 ? noisy(FLAME, x, y, salt, 10) : noisy(shade(KILN_BRICK, 28), x, y, salt, 8);
        const bool mortar = y % 4 == 0 || (x + (y / 4) % 2 * 4) % 8 == 0;
        return noisy(mortar ? shade(KILN_BRICK, 70) : KILN_BRICK, x, y, salt, 7);
    }
    case T_CRATE_TOP: {
        // Boards framed by a dark band, crossed by one diagonal brace.
        const bool frame = x <= 1 || x >= 14 || y <= 1 || y >= 14;
        if (frame) return noisy(shade(CRATE_WOOD, 66), x, y, salt, 5);
        if (std::abs(x - y) <= 1) return noisy(shade(CRATE_WOOD, 84), x, y, salt, 5);
        return noisy(x % 4 == 1 ? shade(CRATE_WOOD, 90) : CRATE_WOOD, x, y, salt, 5);
    }
    case T_CRATE_SIDE: {
        // Horizontal slats in a frame with iron corner plates.
        const bool corner = (x <= 2 || x >= 13) && (y <= 2 || y >= 13);
        if (corner) return noisy(IRON_FRAME, x, y, salt, 6);
        const bool frame = x <= 1 || x >= 14 || y <= 1 || y >= 14;
        if (frame) return noisy(shade(CRATE_WOOD, 66), x, y, salt, 5);
        return noisy(y % 4 == 1 ? shade(CRATE_WOOD, 76) : CRATE_WOOD, x, y, salt, 5);
    }
    case T_GLASS: {
        // Faint panes with a slightly denser rim and two short glints; see-through everywhere.
        const bool rim = x == 0 || x == 15 || y == 0 || y == 15;
        const bool glint = (x - y == 6 && x >= 9 && x <= 12) || (x - y == 9 && x >= 11 && x <= 13);
        if (rim) return with_alpha(shade(GLASS_TINT, 86), GLASS_RIM_ALPHA);
        if (glint) return with_alpha(Rgb{250, 252, 255}, GLASS_GLINT_ALPHA);
        return with_alpha(GLASS_TINT, GLASS_ALPHA);
    }
    case T_CRACK_0:
    case T_CRACK_1:
    case T_CRACK_2:
    case T_CRACK_3: {
        // Dark fracture lines that branch out from the center as the stage grows.
        const int stage = t - T_CRACK_0;  // 0..3
        const int reach = 3 + stage * 2;  // half-length of the cracks in pixels
        const int dx = x - 7, dy = y - 8;
        const bool diag = (dx == dy || dx == -dy - 1) && std::abs(dx) <= reach;
        const bool jag = (y == 8 - (x % 3) || x == 7 + (y % 3) - 1) && std::abs(dx) + std::abs(dy) <= reach + 1;
        const bool chips = stage >= 2 && pixel_hash(x, y, 77) % (stage == 2 ? 11u : 6u) == 0;
        if (diag || jag || chips) return Rgb{24, 22, 20};
        return CLEAR;
    }
    case T_BEDROLL_TOP: {
        // A rolled-out wool blanket with a pillow band at one end and a stitched border.
        if (y <= 3) return noisy(x == 0 || x == 15 ? shade(PILLOW, 80) : PILLOW, x, y, salt, 4);
        if (x == 0 || x == 15 || y == 15) return noisy(shade(BLANKET, 60), x, y, salt, 5);
        if (x == 2 || x == 13) return noisy(shade(BLANKET, 82), x, y, salt, 5);
        return noisy((x + y) % 6 == 0 ? shade(BLANKET, 90) : BLANKET, x, y, salt, 6);
    }
    case T_BEDROLL_SIDE: {
        // Blanket over a plank base, with the rolled edge as a lighter stripe.
        if (y >= 10) return noisy(y == 10 ? shade(PLANK, 70) : PLANK, x, y, salt, 6);
        if (y <= 1) return noisy(shade(BLANKET, 116), x, y, salt, 4);
        return noisy(y % 3 == 0 ? shade(BLANKET, 84) : BLANKET, x, y, salt, 6);
    }
    case T_CAIRN_TOP: {
        // Stacked round stones seen from above: a ring of rocks around a capstone.
        const int dx = 2 * x - 15, dy = 2 * y - 15, r2 = dx * dx + dy * dy;  // r2 in 2-px units
        if (r2 <= 40) return noisy(shade(STONE, 116), x, y, salt, 6);
        if (r2 <= 56) return noisy(shade(STONE, 58), x, y, salt, 4);
        const bool gap = pixel_hash(x / 3, y / 3, salt + 5) % 5 == 0;
        return noisy(gap ? shade(DIRT, 70) : shade(STONE, 96), x, y, salt, 9);
    }
    case T_CAIRN_SIDE: {
        // Courses of rough stones with dark joints, a moss fleck here and there.
        const int course = y / 4;
        const bool joint = y % 4 == 3 || (x + course * 3) % 6 == 5;
        if (joint) return noisy(shade(STONE, 52), x, y, salt, 4);
        if (pixel_hash(x, y, salt + 9) % 13 == 0) return noisy(MOSS, x, y, salt, 6);
        return noisy(course % 2 == 0 ? shade(STONE, 104) : shade(STONE, 92), x, y, salt, 8);
    }
    default: return Rgb{255, 0, 255};  // unused atlas cell
    }
}

// ---- Item icons (M4 Task 6) ----

constexpr Rgb HANDLE{124, 88, 52}, BRONZE{204, 146, 70}, IRON_METAL{206, 208, 214}, HIDE_LEATHER{150, 100, 62},
              MEAT_RAW{214, 92, 102}, MEAT_COOKED{138, 78, 44}, FAT{246, 226, 214}, WOOL_WHITE{238, 236, 228},
              FIBER_GREEN{120, 168, 74}, SILK_WHITE{232, 236, 244}, GLOOM{86, 52, 120};

// Head colors by tier: 1 wood (planks), 2 stone, 3 copper, 4 bronze, 5 iron; 6 = silver.
Rgb tier_color(int tier) {
    constexpr Rgb TIERS[7] = {PLANK, PLANK, STONE, COPPER, BRONZE, IRON_METAL, SILVER};
    return TIERS[std::clamp(tier, 0, 6)];
}

enum class Part : uint8_t { None, Handle, Head };

// Tool shapes in a 16x16 cell (0 pick, 1 axe, 2 shovel, 3 sword), the handle running from the
// bottom left toward the top right. x + y is constant along the handle, x - y across it.
Part tool_part(int kind, int x, int y) {
    if (x < 0 || x > 15 || y < 0 || y > 15) return Part::None;
    const int s = x + y, d = x - y;
    const bool shaft = s == 15 || s == 16;
    switch (kind) {
    case 0: {  // pick: a bar across the handle top, thinner toward its tips
        const bool bar = s >= 5 && s <= 25 && (d == 5 || (std::abs(s - 15) <= 7 && (d == 4 || d == 6)));
        if (bar) return Part::Head;
        return shaft && x >= 2 && x <= 10 ? Part::Handle : Part::None;
    }
    case 1: {  // axe: a blade fanning out to one side of the handle top
        if (s >= 7 && s <= 14 && d >= 2 && d <= 11 && std::abs(d - 7) <= 15 - s) return Part::Head;
        return shaft && x >= 2 && x <= 11 ? Part::Handle : Part::None;
    }
    case 2: {  // shovel: a long handle ending in a flat spade with a pointed tip
        const bool spade = s >= 13 && s <= 18 && d >= 7 && d <= 13 && !(d == 13 && (s == 13 || s == 18)) &&
                           !(d == 12 && (s == 13 || s == 18));
        if (spade || (d == 14 && (s == 15 || s == 16))) return Part::Head;
        return shaft && x >= 2 && x <= 10 ? Part::Handle : Part::None;
    }
    default: {  // sword: a short grip, a cross guard, a long blade
        if (s >= 11 && s <= 19 && (d == -8 || d == -7)) return Part::Head;
        if (x >= 5 && x <= 14 && (s == 15 || (x <= 13 && (s == 14 || s == 16)))) return Part::Head;
        return shaft && x >= 1 && x <= 3 ? Part::Handle : Part::None;
    }
    }
}

// Armor shapes: 0 cap or helmet, 1 chest, 2 legs, 3 boots.
bool armor_shape(int piece, int x, int y) {
    switch (piece) {
    case 0: {
        const int dx = 2 * x - 15, dy = 2 * y - 18;
        return (dx * dx + dy * dy * 2 <= 196 && y <= 9 && y >= 2) || (y >= 9 && y <= 11 && x >= 1 && x <= 14 &&
                                                                     (x <= 4 || x >= 11));
    }
    case 1: return (y >= 2 && y <= 14 && x >= 3 && x <= 12 && !(y <= 4 && x >= 6 && x <= 9)) ||
                   (y >= 2 && y <= 8 && (x <= 2 || x >= 13) && x >= 1 && x <= 14);
    case 2: return y >= 2 && y <= 14 && x >= 3 && x <= 12 && !(y >= 6 && x >= 7 && x <= 8);
    default: return (y >= 6 && y <= 13 && ((x >= 2 && x <= 5) || (x >= 10 && x <= 13))) ||
                    (y >= 11 && y <= 13 && (x == 1 || x == 6 || x == 9 || x == 14));
    }
}

// Pixel (x, y) of the icon for item index i = id - STICK. Opaque or fully clear only.
Rgb item_pixel(int i, int x, int y) {
    const uint32_t salt = static_cast<uint32_t>(i) * 7 + 101;
    constexpr int PICK0 = itm::PICK_WOOD - itm::STICK, ARMOR0 = itm::HIDE_HELMET - itm::STICK;
    if (i >= PICK0 && i < ARMOR0) {
        const int k = i - PICK0;  // 0..20: 5 picks, 5 axes, 5 shovels, 6 swords (silver last)
        const int kind = std::min(k / 5, 3), tier = k == 20 ? 6 : k % 5 + 1;
        const Part p = tool_part(kind, x, y);
        if (p == Part::None) return CLEAR;
        if (p == Part::Handle) return noisy(HANDLE, x, y, 9, 6);
        // Edge pixels of the head are darker, so every tier reads against any background.
        const bool edge = tool_part(kind, x + 1, y) != Part::Head || tool_part(kind, x, y - 1) != Part::Head;
        return noisy(shade(tier_color(tier), edge ? 72 : 108), x, y, 17, 4);
    }
    if (i >= ARMOR0 && i < itm::END - itm::STICK) {
        const int k = i - ARMOR0;  // 0..7: hide cap..boots, iron helmet..boots
        if (!armor_shape(k % 4, x, y)) return CLEAR;
        const Rgb base = k < 4 ? HIDE_LEATHER : IRON_METAL;
        const bool edge = !armor_shape(k % 4, x + 1, y) || !armor_shape(k % 4, x, y + 1);
        return noisy(shade(base, edge ? 70 : 104), x, y, salt, 6);
    }
    const int dx = 2 * x - 15, dy = 2 * y - 15;  // from the cell center, doubled
    switch (i + itm::STICK) {
    case itm::STICK: return (x - y == 0 || x - y == 1) && x >= 3 && x <= 12 ? noisy(HANDLE, x, y, salt, 8) : CLEAR;
    case itm::COAL: {
        if (dx * dx + dy * dy > 120 + static_cast<int>(pixel_hash(x, y, salt) % 40)) return CLEAR;
        return noisy(pixel_hash(x, y, salt + 1) % 5 == 0 ? Rgb{70, 70, 78} : COAL, x, y, salt, 10);
    }
    case itm::COPPER_INGOT:
    case itm::TIN_INGOT:
    case itm::BRONZE_INGOT:
    case itm::IRON_INGOT:
    case itm::SILVER_INGOT: {
        constexpr Rgb METALS[5] = {COPPER, TIN, BRONZE, IRON_METAL, SILVER};
        const Rgb m = METALS[i + itm::STICK - itm::COPPER_INGOT];
        // A bar seen at an angle: top face trapezoid over a darker front.
        const bool top = y >= 5 && y <= 7 && x >= 4 + (7 - y) && x <= 11 + (y - 5);
        const bool front = y >= 8 && y <= 11 && x >= 2 && x <= 13;
        if (top) return noisy(shade(m, 118), x, y, salt, 3);
        if (front) return noisy(y == 11 ? shade(m, 66) : m, x, y, salt, 4);
        return CLEAR;
    }
    case itm::QUARTZ_SHARD: {
        const int ax = std::abs(dx), ay = std::abs(dy + 1);
        if (ax * 3 + ay > 24) return CLEAR;
        return noisy(ax <= 2 ? shade(QUARTZ, 112) : QUARTZ, x, y, salt, 3);
    }
    case itm::RAW_MEAT:
    case itm::COOKED_MEAT:
    case itm::SMALL_MEAT:
    case itm::COOKED_SMALL_MEAT: {
        const bool cooked = i + itm::STICK == itm::COOKED_MEAT || i + itm::STICK == itm::COOKED_SMALL_MEAT;
        const bool small = i + itm::STICK == itm::SMALL_MEAT || i + itm::STICK == itm::COOKED_SMALL_MEAT;
        const int r = small ? 60 : 150;
        if (dx * dx + dy * dy * 2 > r) return CLEAR;
        if ((dx + dy) % 7 == 0) return cooked ? shade(MEAT_COOKED, 70) : FAT;  // a streak of fat or char
        return noisy(cooked ? MEAT_COOKED : MEAT_RAW, x, y, salt, 6);
    }
    case itm::HIDE: {
        const bool inside = x >= 2 && x <= 13 && y >= 3 && y <= 12 && !((x == 2 || x == 13) && (y == 3 || y == 12)) &&
                            !(y == 3 && x >= 6 && x <= 9);
        return inside ? noisy(HIDE_LEATHER, x, y, salt, 10) : CLEAR;
    }
    case itm::WOOL: {
        const int a = (dx + 5) * (dx + 5) + dy * dy, b = (dx - 5) * (dx - 5) + dy * dy, c = dx * dx + (dy + 6) * (dy + 6);
        if (a > 56 && b > 56 && c > 56) return CLEAR;
        return noisy(WOOL_WHITE, x, y, salt, 6);
    }
    case itm::FIBER: return (x == 4 || x == 8 || x == 11) && y >= 2 + x % 3 && y <= 14 ? noisy(FIBER_GREEN, x, y, salt, 10)
                                                                                        : CLEAR;
    case itm::SILK: {
        const bool thread = (y == 4 || y == 8 || y == 12) && x >= 2 && x <= 13 && (x + y / 4) % 4 != 0;
        return thread ? noisy(SILK_WHITE, x, y, salt, 4) : CLEAR;
    }
    case itm::GLOOM_DUST: {
        const bool pile = y >= 9 && std::abs(dx) <= (y - 8) * 3;
        const bool mote = y < 9 && pixel_hash(x, y, salt) % 9 == 0;
        return pile || mote ? noisy(GLOOM, x, y, salt, 14) : CLEAR;
    }
    default: return Rgb{255, 0, 255};
    }
}

// Copies one split_mesh part into a raylib Mesh and uploads it. UnloadMesh frees the CPU arrays
// with RL_FREE, so they come from RL_MALLOC.
Mesh to_gpu(const MeshData& m) {
    auto copy = [](const auto& v) {
        using T = typename std::decay_t<decltype(v)>::value_type;
        auto* out = static_cast<T*>(RL_MALLOC(v.size() * sizeof(T)));
        std::memcpy(out, v.data(), v.size() * sizeof(T));
        return out;
    };
    Mesh mesh{};
    mesh.vertexCount = static_cast<int>(m.vertex_count());
    mesh.triangleCount = static_cast<int>(m.indices.size() / 3);
    mesh.vertices = copy(m.positions);
    mesh.texcoords = copy(m.texcoords);
    mesh.normals = copy(m.normals);
    mesh.colors = copy(m.colors);
    // split_mesh keeps every part within MAX_PART_VERTICES, so each index fits 16 bits.
    mesh.indices = static_cast<unsigned short*>(RL_MALLOC(m.indices.size() * sizeof(unsigned short)));
    for (std::size_t i = 0; i < m.indices.size(); ++i)
        mesh.indices[i] = static_cast<unsigned short>(m.indices[i]);
    UploadMesh(&mesh, false);
    return mesh;
}

}  // namespace

Image generate_atlas() {
    auto* px = static_cast<unsigned char*>(RL_MALLOC(ATLAS_PX * ATLAS_PX * 4));
    for (int y = 0; y < ATLAS_PX; ++y)
        for (int x = 0; x < ATLAS_PX; ++x) {
            const int t = (y / TILE_PX) * ATLAS_TILES + x / TILE_PX;
            const Rgb c = t < T_COUNT ? tile_pixel(t, x % TILE_PX, y % TILE_PX) : Rgb{255, 0, 255};
            unsigned char* p = px + (static_cast<std::size_t>(y) * ATLAS_PX + x) * 4;
            p[0] = static_cast<unsigned char>(c.r);
            p[1] = static_cast<unsigned char>(c.g);
            p[2] = static_cast<unsigned char>(c.b);
            p[3] = static_cast<unsigned char>(c.a);
        }
    return Image{px, ATLAS_PX, ATLAS_PX, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
}

Image generate_item_atlas() {
    auto* px = static_cast<unsigned char*>(RL_MALLOC(ATLAS_PX * ATLAS_PX * 4));
    const int used = itm::END - itm::STICK;
    for (int y = 0; y < ATLAS_PX; ++y)
        for (int x = 0; x < ATLAS_PX; ++x) {
            const int t = (y / TILE_PX) * ATLAS_TILES + x / TILE_PX;
            const Rgb c = t < used ? item_pixel(t, x % TILE_PX, y % TILE_PX) : Rgb{255, 0, 255};
            unsigned char* p = px + (static_cast<std::size_t>(y) * ATLAS_PX + x) * 4;
            p[0] = static_cast<unsigned char>(c.r);
            p[1] = static_cast<unsigned char>(c.g);
            p[2] = static_cast<unsigned char>(c.b);
            p[3] = static_cast<unsigned char>(c.a);
        }
    return Image{px, ATLAS_PX, ATLAS_PX, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
}

std::vector<ChunkPos> sort_back_to_front(std::vector<ChunkPos> chunks, Vector3 eye) {
    // Squared distances in double: exact for chunk centres and float eyes far from the origin.
    auto dist2 = [&eye](ChunkPos p) {
        const double dx = static_cast<double>(p.x) * CHUNK + CHUNK / 2 - eye.x;
        const double dy = static_cast<double>(p.y) * CHUNK + CHUNK / 2 - eye.y;
        const double dz = static_cast<double>(p.z) * CHUNK + CHUNK / 2 - eye.z;
        return dx * dx + dy * dy + dz * dz;
    };
    std::sort(chunks.begin(), chunks.end(), [&dist2](ChunkPos a, ChunkPos b) {
        const double da = dist2(a), db = dist2(b);
        if (da != db) return da > db;
        if (a.x != b.x) return a.x < b.x;
        if (a.y != b.y) return a.y < b.y;
        return a.z < b.z;
    });
    return chunks;
}

FogRange fog_range(int rd) {
    const float end = static_cast<float>(rd * CHUNK);
    return FogRange{FOG_START_FRACTION * end, end};
}

Vector3 sun_direction(float angle) {
    return Vector3{std::cos(angle), std::sin(angle), 0.f};
}

std::vector<Vector3> sky_disc(Vector3 eye, Vector3 dir, float distance, float radius, int segments) {
    segments = std::max(3, segments);
    const Vector3 c = Vector3Add(eye, Vector3Scale(dir, distance));
    // u and v span the disc plane; u x v = -dir, so increasing angle runs counter-clockwise as seen
    // from the eye.
    const Vector3 ref = std::abs(dir.y) < 0.9f ? Vector3{0, 1, 0} : Vector3{1, 0, 0};
    const Vector3 u = Vector3Normalize(Vector3CrossProduct(dir, ref));
    const Vector3 v = Vector3CrossProduct(u, dir);
    auto rim = [&](int i) {
        const float a = 2.f * PI * static_cast<float>(i) / static_cast<float>(segments);
        return Vector3Add(c, Vector3Add(Vector3Scale(u, radius * std::cos(a)), Vector3Scale(v, radius * std::sin(a))));
    };
    std::vector<Vector3> out;
    out.reserve(static_cast<std::size_t>(segments) * 3);
    for (int i = 0; i < segments; ++i) {
        out.push_back(c);
        out.push_back(rim(i));
        out.push_back(rim(i + 1));
    }
    return out;
}

void Renderer::init() {
    const Image atlas = generate_atlas();
    const Texture2D tex = LoadTextureFromImage(atlas);
    UnloadImage(atlas);
    SetTextureFilter(tex, TEXTURE_FILTER_POINT);
    material_ = LoadMaterialDefault();
    material_.maps[MATERIAL_MAP_DIFFUSE].texture = tex;
    // A shader that fails to compile comes back with the default shader's id (rlgl swaps it in),
    // which IsShaderValid accepts and UnloadShader skips, so test the id and free its locs here.
    const Shader shader = LoadShaderFromMemory(CHUNK_VS, CHUNK_FS);
    if (IsShaderValid(shader) && shader.id != rlGetShaderIdDefault()) {
        material_.shader = shader;
        loc_cam_ = GetShaderLocation(shader, "camPos");
        loc_fog_color_ = GetShaderLocation(shader, "fogColor");
        loc_fog_start_ = GetShaderLocation(shader, "fogStart");
        loc_fog_end_ = GetShaderLocation(shader, "fogEnd");
        loc_cutoff_ = GetShaderLocation(shader, "alphaCutoff");
        loc_daylight_ = GetShaderLocation(shader, "daylight");
    } else {
        MemFree(shader.locs);
        TraceLog(LOG_WARNING, "nimecraft: chunk shader failed; drawing without fog or cutout");
    }
    const Image items = generate_item_atlas();
    items_tex_ = LoadTextureFromImage(items);
    UnloadImage(items);
    SetTextureFilter(items_tex_, TEXTURE_FILTER_POINT);
    // The pixels stay owned by the vector: LoadTextureFromImage copies them to the GPU.
    std::vector<Color> mob_px = mob_atlas_pixels();
    const Image mobs{mob_px.data(), MOB_ATLAS_PX, MOB_ATLAS_PX, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
    mob_tex_ = LoadTextureFromImage(mobs);
    SetTextureFilter(mob_tex_, TEXTURE_FILTER_POINT);
    ready_ = true;
}

Texture2D Renderer::block_atlas() const {
    return material_.maps[MATERIAL_MAP_DIFFUSE].texture;
}

Texture2D Renderer::item_atlas() const {
    return items_tex_;
}

Rectangle Renderer::icon_rect(ItemId id) {
    const int cell = item_def(id).icon;
    return Rectangle{static_cast<float>(cell % ATLAS_TILES * TILE_PX), static_cast<float>(cell / ATLAS_TILES * TILE_PX),
                     static_cast<float>(TILE_PX), static_cast<float>(TILE_PX)};
}

void Renderer::draw_crack(int x, int y, int z, int stage) {
    constexpr float GROW = 0.003f;  // just outside the block faces, so the overlay never z-fights
    const int tile = T_CRACK_0 + std::clamp(stage, 0, 3);
    const float u0 = static_cast<float>(tile % ATLAS_TILES) / ATLAS_TILES, v0 = static_cast<float>(tile / ATLAS_TILES) / ATLAS_TILES;
    const float u1 = u0 + 1.f / ATLAS_TILES, v1 = v0 + 1.f / ATLAS_TILES;
    const float lo[3] = {x - GROW, y - GROW, z - GROW}, hi[3] = {x + 1 + GROW, y + 1 + GROW, z + 1 + GROW};
    // Per face: 4 corners as (xi, yi, zi) picks of lo (0) or hi (1), counter-clockwise from outside.
    static constexpr int FACE[6][4][3] = {
        {{1, 0, 0}, {1, 1, 0}, {1, 1, 1}, {1, 0, 1}}, {{0, 0, 1}, {0, 1, 1}, {0, 1, 0}, {0, 0, 0}},
        {{0, 1, 0}, {0, 1, 1}, {1, 1, 1}, {1, 1, 0}}, {{0, 0, 0}, {1, 0, 0}, {1, 0, 1}, {0, 0, 1}},
        {{1, 0, 1}, {1, 1, 1}, {0, 1, 1}, {0, 0, 1}}, {{0, 0, 0}, {0, 1, 0}, {1, 1, 0}, {1, 0, 0}},
    };
    const float us[4] = {u1, u1, u0, u0}, vs[4] = {v1, v0, v0, v1};
    rlDrawRenderBatchActive();
    BeginBlendMode(BLEND_ALPHA);
    rlSetTexture(block_atlas().id);
    rlBegin(RL_QUADS);
    rlColor4ub(255, 255, 255, 200);
    for (const auto& face : FACE)
        for (int c = 0; c < 4; ++c) {
            rlTexCoord2f(us[c], vs[c]);
            rlVertex3f(face[c][0] ? hi[0] : lo[0], face[c][1] ? hi[1] : lo[1], face[c][2] ? hi[2] : lo[2]);
        }
    rlEnd();
    rlSetTexture(0);
    EndBlendMode();
}

void Renderer::draw_item(const Camera3D& cam, ItemId id, Vector3 pos, float size, Color tint) {
    const Texture2D tex = id < blk::COUNT ? block_atlas() : item_atlas();
    DrawBillboardRec(cam, tex, icon_rect(id), Vector3{pos.x, pos.y + size / 2, pos.z}, Vector2{size, size}, tint);
}

void Renderer::draw_mob(const Mob& m, Color tint) {
    // Per face: 4 corners as picks of -half (0) or +half (1), counter-clockwise from outside.
    static constexpr int FACE[6][4][3] = {
        {{1, 0, 0}, {1, 1, 0}, {1, 1, 1}, {1, 0, 1}}, {{0, 0, 1}, {0, 1, 1}, {0, 1, 0}, {0, 0, 0}},
        {{0, 1, 0}, {0, 1, 1}, {1, 1, 1}, {1, 1, 0}}, {{0, 0, 0}, {1, 0, 0}, {1, 0, 1}, {0, 0, 1}},
        {{1, 0, 1}, {1, 1, 1}, {0, 1, 1}, {0, 0, 1}}, {{0, 0, 0}, {0, 1, 0}, {1, 1, 0}, {1, 0, 0}},
    };
    const bool blend = tint.a < 255;
    rlDrawRenderBatchActive();
    if (blend) BeginBlendMode(BLEND_ALPHA);
    rlPushMatrix();
    rlTranslatef(m.pos.x, m.pos.y, m.pos.z);
    rlRotatef(-m.yaw * RAD2DEG, 0.f, 1.f, 0.f);  // the model's +X turns to (cos yaw, 0, sin yaw)
    rlSetTexture(mob_tex_.id);
    for (const BoxPart& p : mob_model(m.type)) {
        const float tile_u = static_cast<float>(p.tile % MOB_ATLAS_TILES) / MOB_ATLAS_TILES;
        const float tile_v = static_cast<float>(p.tile / MOB_ATLAS_TILES) / MOB_ATLAS_TILES;
        const float u[4] = {tile_u + 1.f / MOB_ATLAS_TILES, tile_u + 1.f / MOB_ATLAS_TILES, tile_u, tile_u};
        const float v[4] = {tile_v + 1.f / MOB_ATLAS_TILES, tile_v, tile_v, tile_v + 1.f / MOB_ATLAS_TILES};
        const float hx = p.size.x / 2, hy = p.size.y / 2, hz = p.size.z / 2;
        rlPushMatrix();
        rlTranslatef(p.offset.x, p.offset.y + hy, p.offset.z);  // pivot: the top face center
        const float swing = part_swing(p, m.walk);
        if (swing != 0.f) rlRotatef(swing * RAD2DEG, 0.f, 0.f, 1.f);
        rlTranslatef(0.f, -hy, 0.f);
        rlBegin(RL_QUADS);
        rlColor4ub(tint.r, tint.g, tint.b, tint.a);
        for (const auto& face : FACE)
            for (int c = 0; c < 4; ++c) {
                rlTexCoord2f(u[c], v[c]);
                rlVertex3f(face[c][0] ? hx : -hx, face[c][1] ? hy : -hy, face[c][2] ? hz : -hz);
            }
        rlEnd();
        rlPopMatrix();
    }
    rlSetTexture(0);
    rlPopMatrix();
    if (blend) {
        rlDrawRenderBatchActive();
        EndBlendMode();
    }
}

void Renderer::draw_thorn(Vector3 pos) {
    DrawCube(pos, 0.12f, 0.12f, 0.12f, Color{150, 170, 60, 255});
}

void Renderer::set_fog(FogRange range, Color sky) {
    fog_ = range;
    sky_ = sky;
}

void Renderer::set_daylight(float day_light) {
    daylight_ = day_light;
}

void Renderer::draw_sky(const Camera3D& cam, float sun_angle) {
    const Vector3 sun = sun_direction(sun_angle);
    const Vector3 moon{-sun.x, -sun.y, -sun.z};
    auto disc = [&cam](Vector3 dir, float radius, Color c) {
        if (dir.y < -0.25f) return;  // well below the horizon
        const std::vector<Vector3> tri = sky_disc(cam.position, dir, SKY_DISTANCE, radius, SKY_SEGMENTS);
        for (std::size_t i = 0; i + 2 < tri.size(); i += 3) DrawTriangle3D(tri[i], tri[i + 1], tri[i + 2], c);
    };
    rlDrawRenderBatchActive();
    rlDisableDepthMask();
    BeginBlendMode(BLEND_ALPHA);
    disc(sun, SUN_RADIUS * 1.6f, SUN_HALO);
    disc(sun, SUN_RADIUS, SUN_COLOR);
    disc(moon, MOON_RADIUS, MOON_COLOR);
    EndBlendMode();
    rlDrawRenderBatchActive();
    rlEnableDepthMask();
}

void Renderer::upload(ChunkPos p, const ChunkMesh& m) {
    remove(p);
    if (m.empty()) return;
    const MeshData* layers[3] = {&m.opaque, &m.cutout, &m.translucent};
    ChunkGpu gpu;
    for (std::size_t l = 0; l < gpu.size(); ++l)
        for (const MeshData& part : split_mesh(*layers[l])) gpu[l].push_back(to_gpu(part));
    meshes_.emplace(p, std::move(gpu));
}

void Renderer::remove(ChunkPos p) {
    auto it = meshes_.find(p);
    if (it == meshes_.end()) return;
    for (auto& layer : it->second)
        for (Mesh& mesh : layer) UnloadMesh(mesh);
    meshes_.erase(it);
}

int Renderer::draw(const Camera3D& cam, float aspect) {
    // The same projection BeginMode3D builds, so culling matches what is on screen.
    const Matrix view = MatrixLookAt(cam.position, cam.target, cam.up);
    const Matrix proj = MatrixPerspective(cam.fovy * DEG2RAD, aspect, rlGetCullDistanceNear(),
                                          rlGetCullDistanceFar());
    const Frustum f = frustum_from(MatrixMultiply(view, proj));
    std::vector<const ChunkGpu*> visible;
    std::vector<ChunkPos> translucent;
    for (const auto& [p, gpu] : meshes_) {
        const Vector3 lo{static_cast<float>(p.x * CHUNK), static_cast<float>(p.y * CHUNK),
                         static_cast<float>(p.z * CHUNK)};
        const Vector3 hi{lo.x + CHUNK, lo.y + CHUNK, lo.z + CHUNK};
        if (!aabb_visible(f, lo, hi)) continue;
        visible.push_back(&gpu);
        if (!gpu[2].empty()) translucent.push_back(p);
    }

    const Shader& sh = material_.shader;
    auto set_float = [&sh](int loc, float v) {
        if (loc >= 0) SetShaderValue(sh, loc, &v, SHADER_UNIFORM_FLOAT);
    };
    if (loc_cam_ >= 0) SetShaderValue(sh, loc_cam_, &cam.position, SHADER_UNIFORM_VEC3);
    if (loc_fog_color_ >= 0) {
        const float sky[3] = {sky_.r / 255.f, sky_.g / 255.f, sky_.b / 255.f};
        SetShaderValue(sh, loc_fog_color_, sky, SHADER_UNIFORM_VEC3);
    }
    set_float(loc_fog_start_, fog_.start);
    set_float(loc_fog_end_, fog_.end);
    set_float(loc_daylight_, daylight_);

    // Opaque and Cutout: depth-tested and depth-writing, texels below CUTOUT_ALPHA discarded.
    set_float(loc_cutoff_, CUTOUT_ALPHA);
    for (std::size_t l = 0; l < 2; ++l)
        for (const ChunkGpu* gpu : visible)
            for (const Mesh& mesh : (*gpu)[l]) DrawMesh(mesh, material_, MatrixIdentity());

    // Translucent: back to front, alpha-blended, depth-tested but not depth-writing.
    set_float(loc_cutoff_, 0.0f);
    rlDrawRenderBatchActive();
    rlDisableDepthMask();
    BeginBlendMode(BLEND_ALPHA);
    for (ChunkPos p : sort_back_to_front(std::move(translucent), cam.position))
        for (const Mesh& mesh : meshes_.at(p)[2]) DrawMesh(mesh, material_, MatrixIdentity());
    EndBlendMode();
    rlDrawRenderBatchActive();
    rlEnableDepthMask();
    return static_cast<int>(visible.size());
}

void Renderer::draw_highlight(int x, int y, int z) {
    DrawCubeWires(Vector3{x + 0.5f, y + 0.5f, z + 0.5f}, 1.002f, 1.002f, 1.002f, BLACK);
}

void Renderer::shutdown() {
    for (auto& entry : meshes_)
        for (auto& layer : entry.second)
            for (Mesh& mesh : layer) UnloadMesh(mesh);
    meshes_.clear();
    if (ready_) {
        UnloadMaterial(material_);  // also unloads the block atlas texture
        UnloadTexture(items_tex_);
        UnloadTexture(mob_tex_);
    }
    ready_ = false;
}
