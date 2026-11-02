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

    bool opaque = true;
    for (int y = 0; y < 128; ++y)
        for (int x = 0; x < 128; ++x) opaque = opaque && pixel(a, x, y)[3] == 255;
    CHECK(opaque);

    // Cells past T_COUNT (10) are unused and marked magenta: cell 63 is the bottom-right one.
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
