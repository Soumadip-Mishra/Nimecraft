#include "terrain.hpp"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <memory>
#include <vector>

namespace {

constexpr int MIN_HEIGHT = 20, MAX_HEIGHT = 200;
constexpr int SAND_MAX = SEA_LEVEL + 1;  // land at or below this height is beach sand
constexpr int HIGHLAND_STONE = 140;      // Highlands above this height are bare stone
constexpr int HIGHLAND_OAK_MAX = 140;    // Highlands oaks only on ground below this height
constexpr int FEATURE_TOP = 9;           // a pine's tip sits at most 9 above the ground
static_assert(MAX_HEIGHT + FEATURE_TOP < WORLD_HEIGHT, "features must fit in the column");
constexpr int FEATURE_MARGIN = 3;        // features reach 2 blocks from their cell
constexpr uint32_t OAK_SALT = 0x7EE5u, FLOWER_SALT = 0xF10Eu, TALL_GRASS_SALT = 0x6A55u, PINE_SALT = 0x714Eu,
                   CACTUS_SALT = 0xCAC7u;
constexpr uint8_t OAK_TRUNK = 5;

constexpr int CLIMATE_OCTAVES = 3;
constexpr uint32_t TEMPERATURE_SALT = 0x7E3Au, HUMIDITY_SALT = 0x40D1u, CONTINENTAL_SALT = 0xC047u,
                   MOUNTAIN_SALT = 0x3047u, OCEAN_FLOOR_SALT = 0x0CEAu;

// Height shape per Biome, in enum order.
constexpr int BIOME_BASE[] = {66, 68, 68, 66, 68, 90, 34};
constexpr int BIOME_AMP[] = {12, 16, 20, 10, 14, 110, 20};
static_assert(sizeof(BIOME_BASE) / sizeof(int) == static_cast<std::size_t>(Biome::COUNT));
static_assert(sizeof(BIOME_AMP) / sizeof(int) == static_cast<std::size_t>(Biome::COUNT));

constexpr int BLEND_STEP = 4;    // blend points and biome samples every 4 blocks
constexpr int BLEND_RADIUS = 2;  // 5x5 samples around each blend point
constexpr float OCEAN_FLOOR_MAX = 54.f;  // the coastal cap never goes below this
constexpr float COAST_SLOPE = 1500.f;    // cap rise per unit of continental above 0.35

constexpr BlockId OCEAN_FLOOR[3] = {blk::SAND, blk::GRAVEL, blk::CLAY};

constexpr uint32_t CAVE_SALT_A = 0xCA7Eu, CAVE_SALT_B = 0x5A17u;
constexpr int CAVE_MIN_Y = 5, CAVE_ROOF = 4;  // caves span y = 5 .. surface - 4
constexpr float CAVE_BAND = 0.035f;
constexpr uint32_t LATTICE3_Y = 0x27D4EB2Fu;

constexpr int RAVINE_CELL = 64, RAVINE_REACH_CELLS = 2, RAVINE_CHANCE = 24, RAVINE_WIDTH = 4;
constexpr uint32_t RAVINE_SALT = 0xEA71u, RAVINE_X_SALT = 0xEA72u, RAVINE_Z_SALT = 0xEA73u,
                   RAVINE_ANGLE_SALT = 0xEA74u, RAVINE_LENGTH_SALT = 0xEA75u, RAVINE_DEPTH_SALT = 0xEA76u;
constexpr int WATER_GUARD = 3;  // carving keeps cells with WATER this many blocks above

constexpr int MAX_COLUMN = (1 << 27) - 2;  // supported |ColumnPos| (generate_column contract)
constexpr uint32_t ORE_SALT = 0x0BE5u, ORE_STEP = 0x9E3779B9u;
constexpr uint32_t ORE_RULE_STRIDE = 0x10000u;  // per-rule offset of the vein hash seed
constexpr int SURFACE_LAYERS = 4;               // ore origins sit below the top 4 blocks

constexpr int GEODE_CELL = 64, GEODE_CHANCE = 40, GEODE_Y_MIN = 5, GEODE_Y_SPAN = 36;  // y 5..40
constexpr int GEODE_R = 4, GEODE_R2 = 16, GEODE_INNER_R2 = 9;
constexpr uint32_t GEODE_SALT = 0x6E0Du, GEODE_X_SALT = 0x6E0Eu, GEODE_Z_SALT = 0x6E0Fu, GEODE_Y_SALT = 0x6E10u;

// Lattice value in [0,1]: the top 24 bits of the hash, exact in a float.
float lattice(uint32_t seed, int x, int z) {
    return static_cast<float>(hash2(seed, x, z) >> 8) / 16777215.0f;
}

float lattice3(uint32_t seed, int x, int y, int z) {
    return lattice(seed + static_cast<uint32_t>(y) * LATTICE3_Y, x, z);
}

float smoothstep(float t) {
    return t * t * (3.0f - 2.0f * t);
}

static_assert(48 + 63 + RAVINE_WIDTH / 2 < RAVINE_CELL * RAVINE_REACH_CELLS + 1,
              "a ravine must not reach columns more than 2 cells from its own");

// Unit direction of a ravine: the point at fraction `angle` around the square
// (1,-1) -> (1,1) -> (-1,1) -> (-1,-1), normalized.
void ravine_direction(float angle, float& dx, float& dz) {
    const float a = angle * 4.0f;
    const float side = std::floor(a), f = a - side;
    float px = -1.0f + 2.0f * f, pz = -1.0f;  // side 3
    if (side < 1.0f) {
        px = 1.0f;
        pz = -1.0f + 2.0f * f;
    } else if (side < 2.0f) {
        px = 1.0f - 2.0f * f;
        pz = 1.0f;
    } else if (side < 3.0f) {
        px = -1.0f;
        pz = 1.0f - 2.0f * f;
    }
    const float len = std::sqrt(px * px + pz * pz);
    dx = px / len;
    dz = pz / len;
}

// Sets (lx, y, lz) to AIR unless it is DEEPSTONE or WATER, or WATER lies within WATER_GUARD
// blocks above it.
void carve(Column& col, int lx, int y, int lz);

BlockId& cell(Column& col, int lx, int y, int lz) {
    return col[static_cast<std::size_t>(y / CHUNK)]->blocks[static_cast<std::size_t>(Chunk::index(lx, y % CHUNK, lz))];
}

void carve(Column& col, int lx, int y, int lz) {
    BlockId& b = cell(col, lx, y, lz);
    if (b == blk::DEEPSTONE || b == blk::WATER) return;
    for (int up = y + 1; up <= y + WATER_GUARD && up < WORLD_HEIGHT; ++up)
        if (cell(col, lx, up, lz) == blk::WATER) return;
    b = blk::AIR;
}

// A ravine with its unit direction, for the coverage test shared by carving and features.
struct RavineLine {
    Ravine r;
    float dx, dz;
};

std::vector<RavineLine> ravine_lines(uint32_t seed, ColumnPos c) {
    std::vector<RavineLine> out;
    for (const Ravine& r : ravines_near(seed, c)) {
        RavineLine l{r, 0.0f, 0.0f};
        ravine_direction(r.angle, l.dx, l.dz);
        out.push_back(l);
    }
    return out;
}

// True when (x, z) lies along the ravine (t in [0,1]) within ravine_width(t) / 2 of its line.
bool ravine_covers(const RavineLine& l, int x, int z) {
    // Position relative to the origin: along the ravine (t) and across it.
    const float rx = static_cast<float>(x - l.r.x0), rz = static_cast<float>(z - l.r.z0);
    const float t = (rx * l.dx + rz * l.dz) / static_cast<float>(l.r.length);
    if (t < 0.0f || t > 1.0f) return false;
    return std::abs(rz * l.dx - rx * l.dz) <= ravine_width(t) / 2.0f;
}

// The top block the layer rules give ground height h in this biome (before carving).
BlockId surface_block(uint32_t seed, Biome biome, int h, int x, int z) {
    if (biome == Biome::Ocean) return OCEAN_FLOOR[hash2(seed ^ OCEAN_FLOOR_SALT, x, z) % 3];
    if (biome == Biome::Desert || h <= SAND_MAX) return blk::SAND;
    if (biome == Biome::Snowfield) return blk::SNOW;
    if (biome == Biome::Highlands && h > HIGHLAND_STONE) return blk::STONE;
    return blk::GRASS;
}

float climate_field(uint32_t seed, uint32_t salt, int x, int z, float scale) {
    return fbm2(seed ^ salt, static_cast<float>(x) / scale, static_cast<float>(z) / scale, CLIMATE_OCTAVES);
}
float temperature_of(uint32_t seed, int x, int z) { return climate_field(seed, TEMPERATURE_SALT, x, z, 512.f); }
float humidity_of(uint32_t seed, int x, int z) { return climate_field(seed, HUMIDITY_SALT, x, z, 512.f); }
float continental_of(uint32_t seed, int x, int z) { return climate_field(seed, CONTINENTAL_SALT, x, z, 768.f); }
float mountain_of(uint32_t seed, int x, int z) { return climate_field(seed, MOUNTAIN_SALT, x, z, 384.f); }

// The largest block rectangle side height_rect handles, and the blend points and biome samples
// it needs for it: blend points span floor_div(x0, 4) .. floor_div(x0 + n - 1, 4) + 1.
constexpr int RECT_MAX = CHUNK;
constexpr int BLEND_MAX = (RECT_MAX - 1) / BLEND_STEP + 3;
constexpr int SAMPLES_MAX = BLEND_MAX + 2 * BLEND_RADIUS;

// Heights of the n x n blocks starting at (x0, z0), n in [1, RECT_MAX], into out[lx + lz * n].
// Biome sums are integers (exact), so every caller gets bit-identical heights for a block.
void height_rect(uint32_t seed, int x0, int z0, int n, int* out) {
    const int gx0 = floor_div(x0, BLEND_STEP), gz0 = floor_div(z0, BLEND_STEP);
    const int blends = floor_div(x0 + n - 1, BLEND_STEP) - gx0 + 2;
    const int blends_z = floor_div(z0 + n - 1, BLEND_STEP) - gz0 + 2;
    const int samples = blends + 2 * BLEND_RADIUS, samples_z = blends_z + 2 * BLEND_RADIUS;

    // Biome samples at blend-grid positions gx0 - 2 .. gx0 + blends + 1 (and the same in z).
    Biome biome[SAMPLES_MAX][SAMPLES_MAX];
    for (int j = 0; j < samples_z; ++j)
        for (int i = 0; i < samples; ++i)
            biome[j][i] = biome_at(seed, (gx0 - BLEND_RADIUS + i) * BLEND_STEP,
                                   (gz0 - BLEND_RADIUS + j) * BLEND_STEP);

    float base[BLEND_MAX][BLEND_MAX], amp[BLEND_MAX][BLEND_MAX];
    for (int j = 0; j < blends_z; ++j)
        for (int i = 0; i < blends; ++i) {
            int sum_base = 0, sum_amp = 0;
            for (int dz = 0; dz <= 2 * BLEND_RADIUS; ++dz)
                for (int dx = 0; dx <= 2 * BLEND_RADIUS; ++dx) {
                    const auto b = static_cast<std::size_t>(biome[j + dz][i + dx]);
                    sum_base += BIOME_BASE[b];
                    sum_amp += BIOME_AMP[b];
                }
            constexpr float count = static_cast<float>((2 * BLEND_RADIUS + 1) * (2 * BLEND_RADIUS + 1));
            base[j][i] = static_cast<float>(sum_base) / count;
            amp[j][i] = static_cast<float>(sum_amp) / count;
        }

    auto lerp = [](float a, float b, float t) { return a + (b - a) * t; };
    for (int lz = 0; lz < n; ++lz)
        for (int lx = 0; lx < n; ++lx) {
            const int x = x0 + lx, z = z0 + lz;
            const int i = floor_div(x, BLEND_STEP) - gx0, j = floor_div(z, BLEND_STEP) - gz0;
            const float tx = static_cast<float>(x - (gx0 + i) * BLEND_STEP) / static_cast<float>(BLEND_STEP);
            const float tz = static_cast<float>(z - (gz0 + j) * BLEND_STEP) / static_cast<float>(BLEND_STEP);
            const float b = lerp(lerp(base[j][i], base[j][i + 1], tx), lerp(base[j + 1][i], base[j + 1][i + 1], tx), tz);
            const float a = lerp(lerp(amp[j][i], amp[j][i + 1], tx), lerp(amp[j + 1][i], amp[j + 1][i + 1], tx), tz);
            const float land = b + a * fbm2(seed, static_cast<float>(x) / 96.0f, static_cast<float>(z) / 96.0f, 5);
            const float cap = std::max(OCEAN_FLOOR_MAX, static_cast<float>(SEA_LEVEL - 1) +
                                                            (continental_of(seed, x, z) - 0.35f) * COAST_SLOPE);
            // Both terms are >= 0, so the int conversion is a floor.
            out[lx + lz * n] = std::clamp(static_cast<int>(std::min(land, cap)), MIN_HEIGHT, MAX_HEIGHT);
        }
}

bool in_supported_range(int v) {
    return v >= -MAX_COLUMN && v <= MAX_COLUMN;
}

// Ground heights of the 3x3 columns around a column, index (nx+1) + (nz+1)*3. A neighbor past
// the supported range |pos| <= MAX_COLUMN is not valid (its heights would overflow) and holds no
// ore veins or features.
struct NeighborHeights {
    int h[9][CHUNK * CHUNK];
    bool valid[9];
};

void neighbor_heights(uint32_t seed, ColumnPos c, NeighborHeights& out) {
    for (int nz = -1; nz <= 1; ++nz)
        for (int nx = -1; nx <= 1; ++nx) {
            const auto i = static_cast<std::size_t>((nx + 1) + (nz + 1) * 3);
            out.valid[i] = in_supported_range(c.x + nx) && in_supported_range(c.z + nz);
            if (out.valid[i]) height_rect(seed, (c.x + nx) * CHUNK, (c.z + nz) * CHUNK, CHUNK, out.h[i]);
        }
}

}  // namespace

uint32_t hash2(uint32_t seed, int x, int z) {
    uint32_t h = static_cast<uint32_t>(x) * 374761393u + static_cast<uint32_t>(z) * 668265263u + seed * 2246822519u;
    h ^= h >> 16;
    h *= 0x7FEB352Du;
    h ^= h >> 15;
    h *= 0x846CA68Bu;
    h ^= h >> 16;
    return h;
}

float value_noise2(uint32_t seed, float x, float z) {
    const float fx = std::floor(x), fz = std::floor(z);
    const int ix = static_cast<int>(fx), iz = static_cast<int>(fz);
    const float sx = smoothstep(x - fx), sz = smoothstep(z - fz);
    const float a = lattice(seed, ix, iz), b = lattice(seed, ix + 1, iz);
    const float c = lattice(seed, ix, iz + 1), d = lattice(seed, ix + 1, iz + 1);
    const float near_row = a + (b - a) * sx;
    const float far_row = c + (d - c) * sx;
    // Rounding in the interpolation can step one ulp past an end of [0,1].
    return std::clamp(near_row + (far_row - near_row) * sz, 0.0f, 1.0f);
}

float fbm2(uint32_t seed, float x, float z, int octaves) {
    float sum = 0.0f, weights = 0.0f, amp = 1.0f, freq = 1.0f;
    uint32_t octave_seed = seed;
    for (int i = 0; i < octaves; ++i) {
        sum += amp * value_noise2(octave_seed, x * freq, z * freq);
        weights += amp;
        amp *= 0.5f;
        freq *= 2.0f;
        octave_seed += 0x9E3779B9u;
    }
    return sum / weights;
}

float value_noise3(uint32_t seed, float x, float y, float z) {
    const float fx = std::floor(x), fy = std::floor(y), fz = std::floor(z);
    const int ix = static_cast<int>(fx), iy = static_cast<int>(fy), iz = static_cast<int>(fz);
    const float sx = smoothstep(x - fx), sy = smoothstep(y - fy), sz = smoothstep(z - fz);
    auto layer = [&](int ly) {  // bilinear value of the lattice plane at y = ly
        const float a = lattice3(seed, ix, ly, iz), b = lattice3(seed, ix + 1, ly, iz);
        const float c = lattice3(seed, ix, ly, iz + 1), d = lattice3(seed, ix + 1, ly, iz + 1);
        const float near_row = a + (b - a) * sx;
        const float far_row = c + (d - c) * sx;
        return near_row + (far_row - near_row) * sz;
    };
    const float lower = layer(iy), upper = layer(iy + 1);
    // Rounding in the interpolation can step one ulp past an end of [0,1].
    return std::clamp(lower + (upper - lower) * sy, 0.0f, 1.0f);
}

float fbm3(uint32_t seed, float x, float y, float z, int octaves) {
    float sum = 0.0f, weights = 0.0f, amp = 1.0f, freq = 1.0f;
    uint32_t octave_seed = seed;
    for (int i = 0; i < octaves; ++i) {
        sum += amp * value_noise3(octave_seed, x * freq, y * freq, z * freq);
        weights += amp;
        amp *= 0.5f;
        freq *= 2.0f;
        octave_seed += 0x9E3779B9u;
    }
    return sum / weights;
}

bool is_cave(uint32_t seed, int x, int y, int z, int surface_h) {
    if (y < CAVE_MIN_Y || y > surface_h - CAVE_ROOF) return false;
    const float fx = static_cast<float>(x) / 48.0f, fy = static_cast<float>(y) / 32.0f,
                fz = static_cast<float>(z) / 48.0f;
    if (std::abs(fbm3(seed ^ CAVE_SALT_A, fx, fy, fz, 3) - 0.5f) >= CAVE_BAND) return false;
    return std::abs(fbm3(seed ^ CAVE_SALT_B, fx, fy, fz, 3) - 0.5f) < CAVE_BAND;
}

std::vector<Ravine> ravines_near(uint32_t seed, ColumnPos c) {
    const int gx = floor_div(c.x * CHUNK, RAVINE_CELL), gz = floor_div(c.z * CHUNK, RAVINE_CELL);
    std::vector<Ravine> out;
    for (int cz = gz - RAVINE_REACH_CELLS; cz <= gz + RAVINE_REACH_CELLS; ++cz)
        for (int cx = gx - RAVINE_REACH_CELLS; cx <= gx + RAVINE_REACH_CELLS; ++cx) {
            if (hash2(seed ^ RAVINE_SALT, cx, cz) % RAVINE_CHANCE != 0) continue;
            // In 64 bits: at the edge of the supported range a neighbor cell's origin lies past
            // the int range, and such a ravine cannot reach any column in range anyway.
            const int64_t ox = static_cast<int64_t>(cx) * RAVINE_CELL + hash2(seed ^ RAVINE_X_SALT, cx, cz) % RAVINE_CELL;
            const int64_t oz = static_cast<int64_t>(cz) * RAVINE_CELL + hash2(seed ^ RAVINE_Z_SALT, cx, cz) % RAVINE_CELL;
            if (ox < INT_MIN || ox > INT_MAX || oz < INT_MIN || oz > INT_MAX) continue;
            Ravine r{};
            r.x0 = static_cast<int>(ox);
            r.z0 = static_cast<int>(oz);
            r.angle = static_cast<float>(hash2(seed ^ RAVINE_ANGLE_SALT, cx, cz) % 4096u) / 4096.0f;
            r.length = 48 + static_cast<int>(hash2(seed ^ RAVINE_LENGTH_SALT, cx, cz) % 64u);
            r.depth = 20 + static_cast<int>(hash2(seed ^ RAVINE_DEPTH_SALT, cx, cz) % 11u);
            r.width = RAVINE_WIDTH;
            out.push_back(r);
        }
    return out;
}

float ravine_width(float t) {
    return 2.0f + 8.0f * t * (1.0f - t);
}

const std::array<OreRule, 5> ORE_RULES{{
    {blk::COAL_ORE, 5, 140, 20, 8},
    {blk::COPPER_ORE, 20, 100, 10, 6},
    {blk::TIN_ORE, 10, 70, 7, 5},
    {blk::IRON_ORE, 5, 60, 8, 6},
    {blk::SILVER_ORE, 5, 30, 2, 4},
}};

namespace {

// place_ores with the 3x3 heights around c already computed (generate_column shares them with
// the features).
void place_ores_with(Column& col, ColumnPos c, uint32_t seed, const NeighborHeights& nh) {
    // Coordinates are local to column c, so the walk never overflows at the edge of the range.
    auto blob = [&](int ox, int y, int oz, BlockId ore) {
        for (int dy = 0; dy <= 1; ++dy)
            for (int dz = 0; dz <= 1; ++dz)
                for (int dx = 0; dx <= 1; ++dx) {
                    const int lx = ox + dx, lz = oz + dz;
                    if (lx < 0 || lx >= CHUNK || lz < 0 || lz >= CHUNK) continue;
                    BlockId& b = cell(col, lx, y + dy, lz);
                    if (b == blk::STONE) b = ore;
                }
    };

    for (std::size_t r = 0; r < ORE_RULES.size(); ++r) {
        const OreRule& rule = ORE_RULES[r];
        for (int nz = -1; nz <= 1; ++nz)
            for (int nx = -1; nx <= 1; ++nx) {
                const auto n = static_cast<std::size_t>((nx + 1) + (nz + 1) * 3);
                if (!nh.valid[n]) continue;
                const int* h = nh.h[n];
                for (int v = 0; v < rule.veins_per_column; ++v) {
                    uint32_t s = hash2((seed ^ ORE_SALT) + static_cast<uint32_t>(r) * ORE_RULE_STRIDE +
                                           static_cast<uint32_t>(v),
                                       c.x + nx, c.z + nz);
                    auto next = [&s]() {
                        s += ORE_STEP;
                        return hash2(s, 0, 0);
                    };
                    const int hx = static_cast<int>(s % CHUNK), hz = static_cast<int>((s / CHUNK) % CHUNK);
                    const int top = std::min(rule.y_max - 1, h[hx + hz * CHUNK] - SURFACE_LAYERS);
                    if (top < rule.y_min) continue;
                    int x = nx * CHUNK + hx, z = nz * CHUNK + hz;
                    int y = rule.y_min + static_cast<int>(next() % static_cast<uint32_t>(top - rule.y_min + 1));
                    for (int step = 0; step < rule.vein_size; ++step) {
                        blob(x, y, z, rule.ore);
                        switch (next() % 6) {
                            case 0: ++x; break;
                            case 1: --x; break;
                            case 2: y = std::min(y + 1, rule.y_max - 1); break;
                            case 3: y = std::max(y - 1, rule.y_min); break;
                            case 4: ++z; break;
                            default: --z; break;
                        }
                    }
                }
            }
    }
}

// features_near with the 3x3 heights around c already computed.
std::vector<Feature> features_with(uint32_t seed, ColumnPos c, int margin, const NeighborHeights& nh) {
    margin = std::clamp(margin, 0, CHUNK);
    const int x0 = c.x * CHUNK, z0 = c.z * CHUNK;
    std::vector<RavineLine> ravines[9];
    bool ravines_ready[9] = {};
    std::vector<Feature> out;
    // x-major, z-minor: the output is sorted by (x, z), one feature per cell at most.
    for (int lx = -margin; lx < CHUNK + margin; ++lx)
        for (int lz = -margin; lz < CHUNK + margin; ++lz) {
            const int nx = floor_div(lx, CHUNK), nz = floor_div(lz, CHUNK);
            const auto n = static_cast<std::size_t>((nx + 1) + (nz + 1) * 3);
            if (!nh.valid[n]) continue;
            const int x = x0 + lx, z = z0 + lz;  // in int: the neighbor is in the supported range
            const int h = nh.h[n][(lx - nx * CHUNK) + (lz - nz * CHUNK) * CHUNK];
            if (h < SEA_LEVEL) continue;  // under water
            const Biome biome = biome_at(seed, x, z);
            const BlockId top = surface_block(seed, biome, h, x, z);
            const bool grass = top == blk::GRASS;
            auto r = [&](uint32_t salt) { return hash2(seed ^ salt, x, z); };
            Feature f{x, h + 1, z, FeatureType::OakTree, OAK_TRUNK};
            bool hit = false;
            switch (biome) {
                case Biome::Forest:
                    hit = grass && r(OAK_SALT) % 12 == 0;
                    break;
                case Biome::Meadow:
                    if (!grass) break;
                    hit = true;
                    if (r(OAK_SALT) % 200 == 0) break;
                    f.size = 1;
                    if (r(FLOWER_SALT) % 24 == 0) {
                        f.type = FeatureType::Flower;
                    } else if (r(TALL_GRASS_SALT) % 4 == 0) {
                        f.type = FeatureType::TallGrass;
                    } else {
                        hit = false;
                    }
                    break;
                case Biome::Taiga:
                case Biome::Snowfield: {
                    const uint32_t p = r(PINE_SALT);
                    hit = (grass || top == blk::SNOW) && p % (biome == Biome::Taiga ? 14u : 60u) == 0;
                    f.type = FeatureType::PineTree;
                    f.size = static_cast<uint8_t>(6 + (p >> 8) % 3);
                    break;
                }
                case Biome::Desert: {
                    const uint32_t k = r(CACTUS_SALT);
                    hit = top == blk::SAND && k % 150 == 0;
                    f.type = FeatureType::Cactus;
                    f.size = static_cast<uint8_t>(1 + (k >> 8) % 3);
                    break;
                }
                case Biome::Highlands:
                    hit = grass && h < HIGHLAND_OAK_MAX && r(OAK_SALT) % 80 == 0;
                    break;
                default:  // Ocean
                    break;
            }
            if (!hit) continue;
            // A ravine that cuts this cell's surface leaves no ground for the feature.
            if (!ravines_ready[n]) {
                ravines[n] = ravine_lines(seed, ColumnPos{c.x + nx, c.z + nz});
                ravines_ready[n] = true;
            }
            bool cut = false;
            for (const RavineLine& l : ravines[n]) cut = cut || ravine_covers(l, x, z);
            if (!cut) out.push_back(f);
        }
    return out;
}

}  // namespace

void place_ores(Column& col, ColumnPos c, uint32_t seed) {
    NeighborHeights nh;
    neighbor_heights(seed, c, nh);
    place_ores_with(col, c, seed, nh);
}

std::vector<Feature> features_near(uint32_t seed, ColumnPos c, int margin) {
    NeighborHeights nh;
    neighbor_heights(seed, c, nh);
    return features_with(seed, c, margin, nh);
}

void place_feature(Column& col, ColumnPos c, const Feature& f) {
    // In 64 bits: a feature far from the column reaches none of it.
    const int64_t fx = static_cast<int64_t>(f.x) - static_cast<int64_t>(c.x) * CHUNK;
    const int64_t fz = static_cast<int64_t>(f.z) - static_cast<int64_t>(c.z) * CHUNK;
    if (fx < -FEATURE_MARGIN || fx >= CHUNK + FEATURE_MARGIN || fz < -FEATURE_MARGIN || fz >= CHUNK + FEATURE_MARGIN)
        return;
    const int ox = static_cast<int>(fx), oz = static_cast<int>(fz);
    auto put = [&](int dx, int y, int dz, BlockId b, bool overwrite) {
        const int lx = ox + dx, lz = oz + dz;
        if (lx < 0 || lx >= CHUNK || lz < 0 || lz >= CHUNK || y < 0 || y >= WORLD_HEIGHT) return;
        BlockId& cur = cell(col, lx, y, lz);
        if (overwrite || cur == blk::AIR) cur = b;
    };
    const int top = f.y + f.size;  // first layer above the trunk
    switch (f.type) {
        case FeatureType::OakTree:
            for (int y = f.y; y < top; ++y) put(0, y, 0, blk::LOG, true);
            for (int y = top - 2; y <= top + 1; ++y) {
                const int r = y < top ? 2 : 1;
                for (int dz = -r; dz <= r; ++dz)
                    for (int dx = -r; dx <= r; ++dx)
                        if (r == 1 || std::abs(dx) != 2 || std::abs(dz) != 2) put(dx, y, dz, blk::LEAVES, false);
            }
            break;
        case FeatureType::PineTree:
            for (int y = f.y; y < top; ++y) put(0, y, 0, blk::PINE_LOG, true);
            for (int i = 0; i < 5; ++i) {
                const int y = top - 4 + i, r = (5 - i) / 2;  // radius 2, 2, 1, 1, 0 going up
                for (int dz = -r; dz <= r; ++dz)
                    for (int dx = -r; dx <= r; ++dx)
                        if (dx * dx + dz * dz <= r * r + r) put(dx, y, dz, blk::PINE_LEAVES, false);
            }
            break;
        case FeatureType::Cactus:
            for (int y = f.y; y < top; ++y) put(0, y, 0, blk::CACTUS, false);
            break;
        case FeatureType::TallGrass:
            put(0, f.y, 0, blk::TALL_GRASS, false);
            break;
        case FeatureType::Flower:
            put(0, f.y, 0, blk::FLOWER, false);
            break;
    }
}

std::vector<Geode> geodes_near(uint32_t seed, ColumnPos c) {
    const int gx = floor_div(c.x, GEODE_CELL / CHUNK), gz = floor_div(c.z, GEODE_CELL / CHUNK);
    std::vector<Geode> out;
    for (int cz = gz - 1; cz <= gz + 1; ++cz)
        for (int cx = gx - 1; cx <= gx + 1; ++cx) {
            if (hash2(seed ^ GEODE_SALT, cx, cz) % GEODE_CHANCE != 0) continue;
            // In 64 bits: past the edge of the supported range a neighbor cell lies outside int,
            // and its geode cannot reach any column in range.
            const int64_t x = static_cast<int64_t>(cx) * GEODE_CELL + hash2(seed ^ GEODE_X_SALT, cx, cz) % GEODE_CELL;
            const int64_t z = static_cast<int64_t>(cz) * GEODE_CELL + hash2(seed ^ GEODE_Z_SALT, cx, cz) % GEODE_CELL;
            if (x < INT_MIN || x > INT_MAX || z < INT_MIN || z > INT_MAX) continue;
            const int y = GEODE_Y_MIN + static_cast<int>(hash2(seed ^ GEODE_Y_SALT, cx, cz) % GEODE_Y_SPAN);
            out.push_back(Geode{static_cast<int>(x), y, static_cast<int>(z)});
        }
    return out;
}

void place_geodes(Column& col, ColumnPos c, uint32_t seed) {
    const int x0 = c.x * CHUNK, z0 = c.z * CHUNK;
    for (const Geode& g : geodes_near(seed, c)) {
        const int gx = g.x - x0, gz = g.z - z0;  // centre relative to the column
        for (int dy = -GEODE_R; dy <= GEODE_R; ++dy)
            for (int dz = -GEODE_R; dz <= GEODE_R; ++dz)
                for (int dx = -GEODE_R; dx <= GEODE_R; ++dx) {
                    const int d2 = dx * dx + dy * dy + dz * dz;
                    const int lx = gx + dx, lz = gz + dz;
                    if (d2 > GEODE_R2 || lx < 0 || lx >= CHUNK || lz < 0 || lz >= CHUNK) continue;
                    BlockId& b = cell(col, lx, g.y + dy, lz);
                    if (b == blk::STONE) b = d2 > GEODE_INNER_R2 ? blk::QUARTZ : blk::AIR;
                }
    }
}

Climate climate_at(uint32_t seed, int x, int z) {
    return Climate{temperature_of(seed, x, z), humidity_of(seed, x, z), continental_of(seed, x, z),
                   mountain_of(seed, x, z)};
}

Biome biome_from_climate(const Climate& c) {
    if (c.continental < 0.35f) return Biome::Ocean;
    if (c.mountain > 0.7f) return Biome::Highlands;
    if (c.temperature < 0.3f) return Biome::Snowfield;
    if (c.temperature < 0.45f) return Biome::Taiga;
    if (c.temperature > 0.7f && c.humidity < 0.35f) return Biome::Desert;
    if (c.humidity > 0.5f) return Biome::Forest;
    return Biome::Meadow;
}

Biome biome_at(uint32_t seed, int x, int z) {
    // The biome_from_climate table, reading each field only when a rule needs it.
    if (continental_of(seed, x, z) < 0.35f) return Biome::Ocean;
    if (mountain_of(seed, x, z) > 0.7f) return Biome::Highlands;
    const float t = temperature_of(seed, x, z);
    if (t < 0.3f) return Biome::Snowfield;
    if (t < 0.45f) return Biome::Taiga;
    const float h = humidity_of(seed, x, z);
    if (t > 0.7f && h < 0.35f) return Biome::Desert;
    if (h > 0.5f) return Biome::Forest;
    return Biome::Meadow;
}

int terrain_height(uint32_t seed, int x, int z) {
    int h = 0;
    height_rect(seed, x, z, 1, &h);
    return h;
}

Column generate_column(uint32_t seed, ColumnPos pos, GenOptions opt) {
    Column col;
    for (int cy = 0; cy < SECTIONS; ++cy) {
        col[static_cast<std::size_t>(cy)] = std::make_unique<Chunk>();
        col[static_cast<std::size_t>(cy)]->pos = ChunkPos{pos.x, cy, pos.z};
    }
    const int x0 = pos.x * CHUNK, z0 = pos.z * CHUNK;

    // The 3x3 heights serve the layers (centre), the ore veins, and the features.
    NeighborHeights nh;
    neighbor_heights(seed, pos, nh);
    const int* heights = nh.h[4];
    for (int lz = 0; lz < CHUNK; ++lz) {
        for (int lx = 0; lx < CHUNK; ++lx) {
            const int x = x0 + lx, z = z0 + lz;
            const int h = heights[lx + lz * CHUNK];
            const Biome biome = biome_at(seed, x, z);
            const BlockId top = surface_block(seed, biome, h, x, z);
            auto fill = [&](int y_from, int y_to, BlockId b) {
                for (int y = y_from; y <= y_to; ++y) cell(col, lx, y, lz) = b;
            };
            cell(col, lx, 0, lz) = blk::DEEPSTONE;
            fill(1, h, blk::STONE);
            if (biome == Biome::Desert) {
                fill(h - 7, h - 4, blk::SANDSTONE);
                fill(h - 3, h, blk::SAND);
            } else if (biome == Biome::Ocean || top == blk::SAND) {
                fill(h - 3, h, top);
            } else if (top != blk::STONE) {  // GRASS or SNOW over 3 DIRT
                fill(h - 3, h - 1, blk::DIRT);
                cell(col, lx, h, lz) = top;
            }
            fill(h + 1, SEA_LEVEL, blk::WATER);
            if (biome == Biome::Snowfield && h < SEA_LEVEL) cell(col, lx, SEA_LEVEL, lz) = blk::ICE;
        }
    }

    if (opt.caves) {
        for (int lz = 0; lz < CHUNK; ++lz)
            for (int lx = 0; lx < CHUNK; ++lx) {
                const int h = heights[lx + lz * CHUNK];
                for (int y = CAVE_MIN_Y; y <= h - CAVE_ROOF; ++y)
                    if (is_cave(seed, x0 + lx, y, z0 + lz, h)) carve(col, lx, y, lz);
            }
    }

    if (opt.ravines) {
        for (const RavineLine& l : ravine_lines(seed, pos))
            for (int lz = 0; lz < CHUNK; ++lz)
                for (int lx = 0; lx < CHUNK; ++lx) {
                    if (!ravine_covers(l, x0 + lx, z0 + lz)) continue;
                    const int h = heights[lx + lz * CHUNK];
                    for (int y = h; y >= std::max(1, h - l.r.depth); --y) carve(col, lx, y, lz);
                }
    }

    if (opt.geodes) place_geodes(col, pos, seed);
    if (opt.ores) place_ores_with(col, pos, seed, nh);
    if (opt.features)
        for (const Feature& f : features_with(seed, pos, FEATURE_MARGIN, nh)) place_feature(col, pos, f);
    return col;
}
