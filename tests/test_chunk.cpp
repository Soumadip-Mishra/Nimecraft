// Tests for the light storage of src/chunk.* (M3 Task 1): sky and block light nibbles.
// Block storage tests stay in tests/test_world.cpp with the World tests (M1 Task 2).
#include "chunk.hpp"
#include "test.hpp"

#include <algorithm>
#include <cstdint>

TEST(test_light_nibbles) {
    Chunk fresh;
    CHECK(std::all_of(fresh.light.begin(), fresh.light.end(), [](uint8_t v) { return v == 0; }));
    CHECK(sky_light(fresh, 0) == 0);
    CHECK(block_light(fresh, 4095) == 0);

    Chunk c;
    for (int s = 0; s <= 15; ++s)
        for (int b = 0; b <= 15; ++b) {
            const int i = s * 16 + b;  // a different cell per pair
            set_sky_light(c, i, static_cast<uint8_t>(s));
            set_block_light(c, i, static_cast<uint8_t>(b));
            CHECK(sky_light(c, i) == s);
            CHECK(block_light(c, i) == b);
            // Rewriting one channel keeps the other.
            set_sky_light(c, i, static_cast<uint8_t>(15 - s));
            CHECK(block_light(c, i) == b);
            CHECK(sky_light(c, i) == 15 - s);
            set_block_light(c, i, static_cast<uint8_t>(15 - b));
            CHECK(sky_light(c, i) == 15 - s);
            CHECK(block_light(c, i) == 15 - b);
        }
    // High nibble sky, low nibble block.
    Chunk d;
    set_sky_light(d, 7, 12);
    set_block_light(d, 7, 5);
    CHECK(d.light[7] == 0xC5);
    // Neighbors untouched; the last cell works.
    CHECK(d.light[6] == 0 && d.light[8] == 0);
    set_sky_light(d, 4095, 15);
    set_block_light(d, 4095, 15);
    CHECK(d.light[4095] == 0xFF);
    // Light writes are not block edits.
    CHECK(d.version == 0);
}

// Values above 15 keep only their low 4 bits, so they never leak into the other channel.
TEST(test_light_nibble_overflow) {
    Chunk c;
    set_block_light(c, 0, 9);
    set_sky_light(c, 0, 0x1F);
    CHECK(sky_light(c, 0) == 15);
    CHECK(block_light(c, 0) == 9);
    set_block_light(c, 0, 0xF3);
    CHECK(block_light(c, 0) == 3);
    CHECK(sky_light(c, 0) == 15);
}
