// Tests for the window-free part of src/render.*: the generated block atlas.
#include "block.hpp"
#include "render.hpp"
#include "test.hpp"

#include <cstddef>
#include <cstring>

namespace {

const unsigned char* pixel(const Image& img, int x, int y) {
    return static_cast<const unsigned char*>(img.data) + (static_cast<std::size_t>(y) * 128 + x) * 4;
}

}  // namespace

TEST(test_render_atlas_deterministic) {
    const Image a = generate_atlas();
    const Image b = generate_atlas();
    CHECK(a.width == 128);
    CHECK(a.height == 128);
    CHECK(a.mipmaps == 1);
    CHECK(a.format == PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    CHECK(std::memcmp(a.data, b.data, 128 * 128 * 4) == 0);

    // Alpha per tile (M2 Task 1): Cutout tiles mix fully transparent and fully opaque pixels (M2
    // Task 6's shader discards alpha < 0.5); Translucent tiles are see-through everywhere; every other
    // tile, and every unused cell, is fully opaque.
    for (int t = 0; t < 64; ++t) {
        const bool cutout = t == T_LEAVES || t == T_PINE_LEAVES || t == T_TALL_GRASS || t == T_FLOWER;
        const bool translucent = t == T_WATER || t == T_ICE;
        int clear = 0, solid = 0, partial = 0;
        for (int y = 0; y < 16; ++y)
            for (int x = 0; x < 16; ++x) {
                const unsigned char alpha = pixel(a, (t % 8) * 16 + x, (t / 8) * 16 + y)[3];
                clear += alpha == 0 ? 1 : 0;
                solid += alpha == 255 ? 1 : 0;
                partial += alpha != 0 && alpha != 255 ? 1 : 0;
            }
        if (cutout) {
            CHECK(clear > 0);
            CHECK(solid > 0);
            CHECK(partial == 0);
        } else if (translucent) {
            CHECK(partial == 256);
        } else {
            CHECK(solid == 256);
        }
    }

    // Cells past T_COUNT (31) are unused and marked magenta: cell 63 is the bottom-right one.
    const unsigned char* unused = pixel(a, 127, 127);
    CHECK(unused[0] == 255 && unused[1] == 0 && unused[2] == 255);
    // Used tiles are never magenta; grass top (tile 0) is greener than it is red or blue.
    const unsigned char* grass = pixel(a, 8, 8);
    CHECK(grass[1] > grass[0] && grass[1] > grass[2]);
    // Different tiles differ: stone (tile 3) and sand (tile 4) share no pixel.
    CHECK(std::memcmp(pixel(a, 3 * 16, 0), pixel(a, 4 * 16, 0), 4) != 0);
    MemFree(a.data);
    MemFree(b.data);
}

// Every used tile is its own art: no two of the T_COUNT (31) tiles are pixel-identical, and none is
// the magenta of an unused cell.
TEST(test_render_atlas_tiles_distinct) {
    const Image a = generate_atlas();
    auto same = [&a](int s, int t) {
        for (int y = 0; y < 16; ++y)
            if (std::memcmp(pixel(a, (s % 8) * 16, (s / 8) * 16 + y),
                            pixel(a, (t % 8) * 16, (t / 8) * 16 + y), 16 * 4) != 0)
                return false;
        return true;
    };
    for (int s = 0; s < T_COUNT; ++s) {
        for (int t = s + 1; t < T_COUNT; ++t) CHECK(!same(s, t));
        CHECK(!same(s, 63));
    }
    MemFree(a.data);
}
