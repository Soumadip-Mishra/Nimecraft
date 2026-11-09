// Tests for generate_icon in src/render.* and --export-icon in src/cli.* (M8 Task 7).
#include "cli.hpp"
#include "render.hpp"
#include "test.hpp"

#include <cstddef>
#include <cstring>
#include <optional>
#include <span>
#include <vector>

namespace {

unsigned char alpha_at(const Image& img, int x, int y) {
    return static_cast<const unsigned char*>(img.data)[(static_cast<std::size_t>(y) * img.width + x) * 4 + 3];
}

// Square RGBA, corners clear, center opaque.
void check_icon_shape(int size) {
    Image img = generate_icon(size);
    CHECK(img.data != nullptr);
    CHECK(img.width == size);
    CHECK(img.height == size);
    CHECK(img.mipmaps == 1);
    CHECK(img.format == PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    if (img.data != nullptr && img.width == size && img.height == size) {
        CHECK(alpha_at(img, 0, 0) == 0);
        CHECK(alpha_at(img, size - 1, 0) == 0);
        CHECK(alpha_at(img, 0, size - 1) == 0);
        CHECK(alpha_at(img, size - 1, size - 1) == 0);
        CHECK(alpha_at(img, size / 2, size / 2) == 255);
        // Only fully clear or fully opaque pixels, and the block covers a good part of the square.
        int opaque = 0;
        for (int y = 0; y < size; ++y)
            for (int x = 0; x < size; ++x) {
                const unsigned char a = alpha_at(img, x, y);
                CHECK(a == 0 || a == 255);
                opaque += a == 255 ? 1 : 0;
            }
        CHECK(opaque > size * size / 3);
        CHECK(opaque < size * size * 9 / 10);
    }
    UnloadImage(img);
}

std::optional<CliOptions> cli(std::vector<const char*> args) {
    return parse_cli(std::span<const char* const>(args.data(), args.size()));
}

}  // namespace

TEST(test_icon_size_and_alpha) {
    check_icon_shape(256);
}

TEST(test_icon_deterministic) {
    Image a = generate_icon(256), b = generate_icon(256);
    CHECK(a.data != nullptr && b.data != nullptr);
    if (a.data != nullptr && b.data != nullptr) CHECK(std::memcmp(a.data, b.data, 256 * 256 * 4) == 0);
    UnloadImage(a);
    UnloadImage(b);
}

TEST(test_icon_small_sizes) {
    check_icon_shape(16);
    check_icon_shape(64);
}

TEST(test_icon_faces_shaded) {
    // The three faces use the grass top tile (green) and the grass side tile, lit 3 ways: the top
    // face is brightest, the right face darkest. Sample one pixel inside each face at 256 px.
    Image img = generate_icon(256);
    const auto px = [&img](int x, int y) {
        return static_cast<const unsigned char*>(img.data) + (static_cast<std::size_t>(y) * 256 + x) * 4;
    };
    const unsigned char* top = px(128, 60);     // above the center: top face
    const unsigned char* left = px(70, 170);    // lower left: left face
    const unsigned char* right = px(186, 170);  // lower right: right face
    CHECK(top[3] == 255 && left[3] == 255 && right[3] == 255);
    CHECK(top[1] > top[0] && top[1] > top[2]);  // grass top is green
    const int sum_top = top[0] + top[1] + top[2];
    // Total brightness of a wide band inside each side face. The two faces show the same side tile
    // mirrored, so the band totals compare like the shades: right / left = 0.6 / 0.8 = 0.75.
    long l = 0, r = 0;
    for (int y = 125; y <= 197; ++y) {
        for (int x = 40; x <= 120; ++x) l += px(x, y)[0] + px(x, y)[1] + px(x, y)[2];
        for (int x = 135; x <= 215; ++x) r += px(x, y)[0] + px(x, y)[1] + px(x, y)[2];
    }
    CHECK(l > 0);
    if (l > 0) CHECK_NEAR(static_cast<double>(r) / static_cast<double>(l), 0.75, 0.03);
    CHECK(sum_top > 0);
    UnloadImage(img);
}

TEST(test_icon_size_bounds) {
    check_icon_shape(1024);
    // --export-icon needs a file; a size, when given, is 16..1024.
    CHECK(!cli({"--export-icon"}).has_value());
    CHECK(!cli({"--export-icon", "icon.png", "0"}).has_value());
    CHECK(!cli({"--export-icon", "icon.png", "-1"}).has_value());
    CHECK(!cli({"--export-icon", "icon.png", "15"}).has_value());
    CHECK(!cli({"--export-icon", "icon.png", "1025"}).has_value());
    CHECK(!cli({"--export-icon", "icon.png", "64px"}).has_value());
    CHECK(!cli({"--export-icon", ""}).has_value());
    const std::optional<CliOptions> lo = cli({"--export-icon", "icon.png", "16"});
    CHECK(lo && lo->export_icon && lo->icon_size == 16);
    const std::optional<CliOptions> hi = cli({"--export-icon", "icon.png", "1024"});
    CHECK(hi && hi->export_icon && hi->icon_size == 1024);
}
