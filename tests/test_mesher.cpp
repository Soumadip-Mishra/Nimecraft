// Tests for src/mesher.*: padded snapshots and face-culled chunk meshes.
#include "block.hpp"
#include "chunk.hpp"
#include "coords.hpp"
#include "light.hpp"
#include "mesher.hpp"
#include "test.hpp"
#include "world.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <memory>
#include <thread>
#include <vector>

namespace {

// All-AIR columns (-1..1, -1..1), as the task's test setup describes.
World air_world() {
    World w(1);
    for (int cx = -1; cx <= 1; ++cx)
        for (int cz = -1; cz <= 1; ++cz) w.insert_column(ColumnPos{cx, cz}, Column{});
    return w;
}

ChunkMesh layers_of(const World& w, ChunkPos p) {
    return build_mesh(snapshot(w, p), p);
}

// The opaque layer, which holds everything the M1 tests build from opaque cubes.
MeshData mesh_of(const World& w, ChunkPos p) {
    return layers_of(w, p).opaque;
}

struct V3 {
    float x, y, z;
};

V3 pos_at(const MeshData& m, std::size_t i) {
    return V3{m.positions[i * 3], m.positions[i * 3 + 1], m.positions[i * 3 + 2]};
}

V3 normal_at(const MeshData& m, std::size_t i) {
    return V3{m.normals[i * 3], m.normals[i * 3 + 1], m.normals[i * 3 + 2]};
}

bool indices_in_range(const MeshData& m) {
    for (uint32_t i : m.indices)
        if (i >= m.vertex_count()) return false;
    return true;
}

float max_y(const MeshData& m) {
    float y = -1e9f;
    for (std::size_t i = 0; i < m.vertex_count(); ++i) y = pos_at(m, i).y > y ? pos_at(m, i).y : y;
    return y;
}

std::size_t count_y(const MeshData& m, float y) {
    std::size_t n = 0;
    for (std::size_t i = 0; i < m.vertex_count(); ++i) n += pos_at(m, i).y == y ? 1 : 0;
    return n;
}

// Vertices whose normal points along +X / -X (axis-aligned faces only).
std::size_t count_normal_x(const MeshData& m, float nx) {
    std::size_t n = 0;
    for (std::size_t i = 0; i < m.vertex_count(); ++i) n += normal_at(m, i).x == nx ? 1 : 0;
    return n;
}

// Face of a unit normal, by the Face enum order PX, NX, PY, NY, PZ, NZ.
int face_of(V3 n) {
    if (n.x > 0.5f) return PX;
    if (n.x < -0.5f) return NX;
    if (n.y > 0.5f) return PY;
    if (n.y < -0.5f) return NY;
    if (n.z > 0.5f) return PZ;
    return NZ;
}

// ---- M3 Task 3 helpers ----

using Gen = std::function<BlockId(int x, int y, int z)>;

// Columns (-1..1, -1..1) built from gen, lit as ChunkManager lights them (compute, then stitch).
World lit_world(const Gen& gen) {
    World w(1);
    for (int cx = -1; cx <= 1; ++cx)
        for (int cz = -1; cz <= 1; ++cz) {
            Column col;
            for (int cy = 0; cy < SECTIONS; ++cy) {
                auto ch = std::make_unique<Chunk>();
                for (int ly = 0; ly < CHUNK; ++ly)
                    for (int lz = 0; lz < CHUNK; ++lz)
                        for (int lx = 0; lx < CHUNK; ++lx)
                            ch->blocks[static_cast<std::size_t>(Chunk::index(lx, ly, lz))] =
                                gen(cx * CHUNK + lx, cy * CHUNK + ly, cz * CHUNK + lz);
                col[static_cast<std::size_t>(cy)] = std::move(ch);
            }
            compute_column_light(col);
            w.insert_column(ColumnPos{cx, cz}, std::move(col));
            stitch_column_light(w, ColumnPos{cx, cz});
        }
    return w;
}

BlockId flat_ground(int, int y, int) {
    return y <= 64 ? blk::STONE : blk::AIR;
}

// Index of the vertex at (x, y, z) whose normal is n, or SIZE_MAX.
std::size_t vertex_at(const MeshData& m, V3 p, V3 n) {
    for (std::size_t i = 0; i < m.vertex_count(); ++i) {
        const V3 q = pos_at(m, i), nn = normal_at(m, i);
        if (q.x == p.x && q.y == p.y && q.z == p.z && nn.x == n.x && nn.y == n.y && nn.z == n.z) return i;
    }
    return SIZE_MAX;
}

uint8_t channel(const MeshData& m, std::size_t v, int c) {
    return v == SIZE_MAX ? 0 : m.colors[v * 4 + static_cast<std::size_t>(c)];
}

constexpr V3 UP{0.f, 1.f, 0.f};

}  // namespace

TEST(test_mesh_single_block) {
    World w = air_world();
    w.set(1, 70, 1, blk::STONE);
    const MeshData m = mesh_of(w, ChunkPos{0, 4, 0});
    CHECK(m.vertex_count() == 24);
    CHECK(m.indices.size() == 36);
    CHECK(m.positions.size() == 72);
    CHECK(m.texcoords.size() == 48);
    CHECK(m.normals.size() == 72);
    CHECK(m.colors.size() == 96);
}

TEST(test_mesh_adjacent_cull) {
    World w = air_world();
    w.set(1, 70, 1, blk::STONE);
    w.set(2, 70, 1, blk::STONE);
    const MeshData m = mesh_of(w, ChunkPos{0, 4, 0});
    CHECK(m.vertex_count() == 40);
    CHECK(m.indices.size() == 60);
}

TEST(test_mesh_cull_across_chunks) {
    World w = air_world();
    w.set(15, 70, 1, blk::STONE);
    w.set(16, 70, 1, blk::STONE);
    CHECK(mesh_of(w, ChunkPos{0, 4, 0}).vertex_count() == 20);
    CHECK(mesh_of(w, ChunkPos{1, 4, 0}).vertex_count() == 20);

    World w2 = air_world();
    w2.set(-1, 70, 1, blk::STONE);
    w2.set(0, 70, 1, blk::STONE);
    CHECK(mesh_of(w2, ChunkPos{-1, 4, 0}).vertex_count() == 20);
    CHECK(mesh_of(w2, ChunkPos{0, 4, 0}).vertex_count() == 20);
}

TEST(test_mesh_cull_across_vertical_chunks) {
    World w = air_world();
    w.set(1, 79, 1, blk::STONE);
    w.set(1, 80, 1, blk::STONE);
    CHECK(mesh_of(w, ChunkPos{0, 4, 0}).vertex_count() == 20);
    CHECK(mesh_of(w, ChunkPos{0, 5, 0}).vertex_count() == 20);
}

TEST(test_mesh_cull_across_z_chunks) {
    World w = air_world();
    w.set(1, 70, 15, blk::STONE);
    w.set(1, 70, 16, blk::STONE);
    CHECK(mesh_of(w, ChunkPos{0, 4, 0}).vertex_count() == 20);
    CHECK(mesh_of(w, ChunkPos{0, 4, 1}).vertex_count() == 20);

    World w2 = air_world();
    w2.set(1, 70, -1, blk::STONE);
    w2.set(1, 70, 0, blk::STONE);
    CHECK(mesh_of(w2, ChunkPos{0, 4, -1}).vertex_count() == 20);
    CHECK(mesh_of(w2, ChunkPos{0, 4, 0}).vertex_count() == 20);
}

TEST(test_mesh_world_bottom_hidden) {
    World w = air_world();
    w.set(1, 0, 1, blk::STONE);
    CHECK(mesh_of(w, ChunkPos{0, 0, 0}).vertex_count() == 20);
}

TEST(test_mesh_world_top) {
    World w = air_world();
    w.set(1, 255, 1, blk::STONE);
    const MeshData m = mesh_of(w, ChunkPos{0, 15, 0});
    CHECK(m.vertex_count() == 24);
    float max_y = 0.f;
    for (std::size_t i = 0; i < m.vertex_count(); ++i)
        if (pos_at(m, i).y > max_y) max_y = pos_at(m, i).y;
    CHECK(max_y == 256.f);
}

TEST(test_mesh_empty) {
    World w = air_world();
    const MeshData m = mesh_of(w, ChunkPos{0, 10, 0});
    CHECK(m.vertex_count() == 0);
    CHECK(m.indices.empty());
    CHECK(m.colors.empty());
}

TEST(test_mesh_winding) {
    World w = air_world();
    w.set(1, 70, 1, blk::STONE);
    const MeshData m = mesh_of(w, ChunkPos{0, 4, 0});
    CHECK(m.indices.size() == 36);
    for (std::size_t t = 0; t + 2 < m.indices.size(); t += 3) {
        const V3 a = pos_at(m, m.indices[t]), b = pos_at(m, m.indices[t + 1]),
                 c = pos_at(m, m.indices[t + 2]);
        const V3 n = normal_at(m, m.indices[t]);
        const V3 e1{b.x - a.x, b.y - a.y, b.z - a.z}, e2{c.x - a.x, c.y - a.y, c.z - a.z};
        const V3 cr{e1.y * e2.z - e1.z * e2.y, e1.z * e2.x - e1.x * e2.z, e1.x * e2.y - e1.y * e2.x};
        CHECK(cr.x * n.x + cr.y * n.y + cr.z * n.z > 0.f);
    }
}

TEST(test_mesh_normals_unit_and_outward) {
    World w = air_world();
    w.set(1, 70, 1, blk::STONE);
    const MeshData m = mesh_of(w, ChunkPos{0, 4, 0});
    int per_face[6] = {0, 0, 0, 0, 0, 0};
    for (std::size_t i = 0; i < m.vertex_count(); ++i) {
        const V3 n = normal_at(m, i), p = pos_at(m, i);
        CHECK(n.x * n.x + n.y * n.y + n.z * n.z == 1.f);
        // The vertex lies on the block face the normal points out of.
        const int f = face_of(n);
        ++per_face[f];
        if (f == PX) CHECK(p.x == 2.f);
        if (f == NX) CHECK(p.x == 1.f);
        if (f == PY) CHECK(p.y == 71.f);
        if (f == NY) CHECK(p.y == 70.f);
        if (f == PZ) CHECK(p.z == 2.f);
        if (f == NZ) CHECK(p.z == 1.f);
    }
    for (int f = 0; f < 6; ++f) CHECK(per_face[f] == 4);
}

TEST(test_mesh_shade_and_uv) {
    World w = air_world();
    w.set(1, 70, 1, blk::GRASS);
    const MeshData m = mesh_of(w, ChunkPos{0, 4, 0});
    CHECK(m.vertex_count() == 24);
    int top = 0, bottom = 0;
    for (std::size_t i = 0; i < m.vertex_count(); ++i) {
        const V3 n = normal_at(m, i);
        const uint8_t r = m.colors[i * 4], g = m.colors[i * 4 + 1], shade = m.colors[i * 4 + 2],
                      a = m.colors[i * 4 + 3];
        // M3 Task 3: R and G carry sky and block light (none in this unlit world), B the face
        // shade times ambient occlusion (no occluders here).
        CHECK(r == 0);
        CHECK(g == 0);
        CHECK(a == 255);
        const float u = m.texcoords[i * 2], v = m.texcoords[i * 2 + 1];
        if (n.y > 0.5f) {
            ++top;
            CHECK(shade == 255);
            CHECK(u >= 0.f && u <= 0.125f);
            CHECK(v >= 0.f && v <= 0.125f);
        } else if (n.y < -0.5f) {
            ++bottom;
            CHECK(shade == 128);
        } else if (n.x != 0.f) {
            CHECK(shade == 204);
        } else {
            CHECK(shade == 166);
        }
    }
    CHECK(top == 4);
    CHECK(bottom == 4);
}

TEST(test_mesh_positions_world_space) {
    World w = air_world();
    w.set(-1, 70, 1, blk::STONE);
    const MeshData m = mesh_of(w, ChunkPos{-1, 4, 0});
    CHECK(m.vertex_count() == 24);
    for (std::size_t i = 0; i < m.vertex_count(); ++i) {
        const V3 p = pos_at(m, i);
        CHECK(p.x >= -1.f && p.x <= 0.f);
        CHECK(p.y >= 70.f && p.y <= 71.f);
        CHECK(p.z >= 1.f && p.z <= 2.f);
    }
}

TEST(test_mesh_far_coordinates) {
    // Column x = 62500 starts at block x = 1,000,000; its neighbors are unloaded (AIR).
    World w(1);
    w.insert_column(ColumnPos{62500, -62500}, Column{});
    w.set(1000000, 70, -1000000, blk::STONE);
    const MeshData m = mesh_of(w, ChunkPos{62500, 4, -62500});
    CHECK(m.vertex_count() == 24);
    for (std::size_t i = 0; i < m.vertex_count(); ++i) {
        const V3 p = pos_at(m, i);
        CHECK(p.x == 1000000.f || p.x == 1000001.f);
        CHECK(p.z == -1000000.f || p.z == -999999.f);
    }
}

TEST(test_mesh_unloaded_neighbor_shows_face) {
    // Column 2 is unloaded, so the block at x = 31 faces AIR on +X and keeps all 6 faces.
    World w = air_world();
    w.set(31, 70, 1, blk::STONE);
    CHECK(mesh_of(w, ChunkPos{1, 4, 0}).vertex_count() == 24);
}

TEST(test_mesh_full_chunk) {
    World w = air_world();
    for (int y = 64; y < 80; ++y)
        for (int z = 0; z < 16; ++z)
            for (int x = 0; x < 16; ++x) w.set(x, y, z, blk::STONE);
    const MeshData m = mesh_of(w, ChunkPos{0, 4, 0});
    CHECK(m.vertex_count() == 6144);
    CHECK(m.indices.size() == 9216);
    CHECK(indices_in_range(m));
}

TEST(test_mesh_checkerboard_worst_case) {
    World w = air_world();
    for (int y = 64; y < 80; ++y)
        for (int z = 0; z < 16; ++z)
            for (int x = 0; x < 16; ++x)
                if ((x + y + z) % 2 == 0) w.set(x, y, z, blk::STONE);
    const MeshData m = mesh_of(w, ChunkPos{0, 4, 0});
    CHECK(m.vertex_count() == 49152);
    CHECK(m.indices.size() == 73728);
    CHECK(indices_in_range(m));
}

TEST(test_mesh_uv_every_tile) {
    for (BlockId id = 1; id < blk::COUNT; ++id) {
        if (block_def(id).shape == Shape::Cross) continue;  // test_mesh_cross_plant covers these
        World w = air_world();
        w.set(1, 70, 1, id);
        const ChunkMesh cm = layers_of(w, ChunkPos{0, 4, 0});
        const MeshData& m = block_def(id).layer == Layer::Opaque
                                ? cm.opaque
                                : (block_def(id).layer == Layer::Cutout ? cm.cutout : cm.translucent);
        CHECK(m.vertex_count() == 24);
        for (std::size_t i = 0; i < m.vertex_count(); ++i) {
            const int t = block_def(id).tiles[static_cast<std::size_t>(face_of(normal_at(m, i)))];
            const float u0 = static_cast<float>(t % 8) / 8.f, v0 = static_cast<float>(t / 8) / 8.f;
            const float u = m.texcoords[i * 2], v = m.texcoords[i * 2 + 1];
            CHECK(u >= u0 && u <= u0 + 0.125f);
            CHECK(v >= v0 && v <= v0 + 0.125f);
        }
    }
}

TEST(test_mesh_uv_covers_whole_tile) {
    // Each face maps the full tile: its 4 UVs reach both edges of the rect in u and in v.
    World w = air_world();
    w.set(1, 70, 1, blk::LOG);
    const MeshData m = mesh_of(w, ChunkPos{0, 4, 0});
    for (std::size_t f = 0; f + 3 < m.vertex_count(); f += 4) {
        float umin = 1.f, umax = 0.f, vmin = 1.f, vmax = 0.f;
        for (std::size_t i = f; i < f + 4; ++i) {
            const float u = m.texcoords[i * 2], v = m.texcoords[i * 2 + 1];
            umin = u < umin ? u : umin;
            umax = u > umax ? u : umax;
            vmin = v < vmin ? v : vmin;
            vmax = v > vmax ? v : vmax;
        }
        CHECK_NEAR(umax - umin, 0.125, 1e-6);
        CHECK_NEAR(vmax - vmin, 0.125, 1e-6);
    }
}

TEST(test_mesh_side_texture_upright) {
    // On side faces the top edge of the block uses the top edge of the tile (smaller v).
    World w = air_world();
    w.set(1, 70, 1, blk::GRASS);
    const MeshData m = mesh_of(w, ChunkPos{0, 4, 0});
    const float v_top = 0.f, v_bottom = 0.125f;  // T_GRASS_SIDE = 1 sits in row 0
    for (std::size_t i = 0; i < m.vertex_count(); ++i) {
        const V3 n = normal_at(m, i);
        if (n.y != 0.f) continue;
        const float v = m.texcoords[i * 2 + 1];
        if (pos_at(m, i).y == 71.f) CHECK(v == v_top);
        else CHECK(v == v_bottom);
    }
}

TEST(test_padded_snapshot_borders) {
    World w = air_world();
    w.set(-1, 70, 0, blk::SAND);    // x neighbor of chunk {0,4,0}
    w.set(16, 79, 15, blk::PLANKS);  // +X +Y corner side
    w.set(5, 80, 5, blk::LOG);       // +Y neighbor
    w.set(5, 63, 5, blk::DIRT);      // -Y neighbor
    w.set(3, 70, 4, blk::STONE);     // inside
    w.set(7, 72, -1, blk::LEAVES);   // -Z neighbor
    w.set(8, 73, 16, blk::GRASS);    // +Z neighbor
    const Padded pad = snapshot(w, ChunkPos{0, 4, 0});
    CHECK(pad.at(7, 8, -1) == blk::LEAVES);
    CHECK(pad.at(8, 9, 16) == blk::GRASS);
    CHECK(pad.at(-1, 6, 0) == blk::SAND);
    CHECK(pad.at(16, 15, 15) == blk::PLANKS);
    CHECK(pad.at(5, 16, 5) == blk::LOG);
    CHECK(pad.at(5, -1, 5) == blk::DIRT);
    CHECK(pad.at(3, 6, 4) == blk::STONE);
    CHECK(pad.at(0, 0, 0) == blk::AIR);
}

TEST(test_padded_snapshot_world_limits) {
    World w = air_world();
    const Padded bottom = snapshot(w, ChunkPos{0, 0, 0});
    CHECK(bottom.at(4, -1, 4) == blk::DEEPSTONE);  // below the world reads as DEEPSTONE
    const Padded top = snapshot(w, ChunkPos{0, 15, 0});
    CHECK(top.at(4, 16, 4) == blk::AIR);           // above the world reads as AIR
    const Padded edge = snapshot(w, ChunkPos{1, 4, 0});
    CHECK(edge.at(16, 4, 4) == blk::AIR);          // unloaded column reads as AIR
}

TEST(test_mesh_unknown_id_is_air) {
    Padded pad;
    pad.b[static_cast<std::size_t>(1 + 1 * 18 + 1 * 324)] = 500;  // local (0,0,0), id >= COUNT
    CHECK(pad.at(0, 0, 0) == 500);
    CHECK(build_mesh(pad, ChunkPos{0, 4, 0}).empty());
}

TEST(test_mesh_deterministic) {
    // Every layer and shape: leaves (Cutout cube), stone (Opaque), water (Liquid), grass (Cross).
    const BlockId kinds[4] = {blk::LEAVES, blk::STONE, blk::WATER, blk::TALL_GRASS};
    World w = air_world();
    for (int i = 0; i < 40; ++i)
        w.set((i * 7) % 16, 64 + (i * 5) % 16, (i * 3) % 16, kinds[i % 4]);
    const Padded pad = snapshot(w, ChunkPos{0, 4, 0});
    const ChunkMesh a = build_mesh(pad, ChunkPos{0, 4, 0});
    const ChunkMesh b = build_mesh(pad, ChunkPos{0, 4, 0});
    const MeshData* la[3] = {&a.opaque, &a.cutout, &a.translucent};
    const MeshData* lb[3] = {&b.opaque, &b.cutout, &b.translucent};
    for (int l = 0; l < 3; ++l) {
        CHECK(la[l]->vertex_count() > 0);
        CHECK(la[l]->positions == lb[l]->positions);
        CHECK(la[l]->texcoords == lb[l]->texcoords);
        CHECK(la[l]->normals == lb[l]->normals);
        CHECK(la[l]->colors == lb[l]->colors);
        CHECK(la[l]->indices == lb[l]->indices);
    }
}

TEST(test_mesh_concurrent_build) {
    World w = air_world();
    w.set(1, 70, 1, blk::STONE);
    w.set(9, 75, 9, blk::SAND);
    const Padded pad = snapshot(w, ChunkPos{0, 4, 0});
    std::vector<std::size_t> counts(4, 0);
    std::vector<std::thread> threads;
    for (std::size_t t = 0; t < counts.size(); ++t)
        threads.emplace_back([&pad, &counts, t] {
            counts[t] = build_mesh(pad, ChunkPos{0, 4, 0}).opaque.vertex_count();
        });
    for (auto& th : threads) th.join();
    for (std::size_t c : counts) CHECK(c == 48);
}

// ---- M2 Task 1: render layers and block shapes ----

TEST(test_mesh_leaves_adjacent_both_drawn) {
    World w = air_world();
    w.set(1, 70, 1, blk::LEAVES);
    w.set(2, 70, 1, blk::LEAVES);
    const ChunkMesh m = layers_of(w, ChunkPos{0, 4, 0});
    CHECK(m.cutout.vertex_count() == 48);
    CHECK(m.opaque.vertex_count() == 0);
    CHECK(m.translucent.vertex_count() == 0);
}

TEST(test_mesh_water_same_id_culled) {
    World w = air_world();
    w.set(1, 70, 1, blk::WATER);
    w.set(2, 70, 1, blk::WATER);
    const ChunkMesh m = layers_of(w, ChunkPos{0, 4, 0});
    CHECK(m.translucent.vertex_count() == 40);
    CHECK(m.opaque.vertex_count() == 0);
    CHECK(m.cutout.vertex_count() == 0);
    // Two top faces of 4 vertices, plus the top corners of the 6 side faces, all at 70.875.
    std::size_t top = 0;
    for (std::size_t i = 0; i < m.translucent.vertex_count(); ++i)
        if (normal_at(m.translucent, i).y > 0.5f) {
            ++top;
            CHECK(pos_at(m.translucent, i).y == 70.875f);
        }
    CHECK(top == 8);
    CHECK(max_y(m.translucent) == 70.875f);
    CHECK(count_y(m.translucent, 70.875f) == 8 + 12);
}

TEST(test_mesh_water_against_stone) {
    // The water face toward the stone is culled (Translucent skips opaque neighbors). The stone
    // face toward the water stays: Opaque skips only opaque neighbors, and the floor must show
    // through water (M2 Task 6 manual check).
    World w = air_world();
    w.set(1, 70, 1, blk::WATER);
    w.set(2, 70, 1, blk::STONE);
    const ChunkMesh m = layers_of(w, ChunkPos{0, 4, 0});
    CHECK(m.translucent.vertex_count() == 20);
    CHECK(count_normal_x(m.translucent, 1.f) == 0);  // no water face on the shared plane x = 2
    CHECK(m.opaque.vertex_count() == 24);
    CHECK(count_normal_x(m.opaque, -1.f) == 4);      // the stone's -X face, toward the water
}

TEST(test_mesh_cross_plant) {
    World w = air_world();
    w.set(1, 70, 1, blk::TALL_GRASS);
    const ChunkMesh m = layers_of(w, ChunkPos{0, 4, 0});
    CHECK(m.cutout.vertex_count() == 16);
    CHECK(m.cutout.indices.size() == 24);
    CHECK(m.opaque.vertex_count() == 0);
    CHECK(m.translucent.vertex_count() == 0);
    CHECK(indices_in_range(m.cutout));
    // Diagonal quads span the block: every vertex is on a corner column of the block, and each
    // quad joins two opposite corners (x - 1 == z - 1 or x - 1 == 2 - z).
    for (std::size_t i = 0; i < m.cutout.vertex_count(); ++i) {
        const V3 p = pos_at(m.cutout, i);
        CHECK(p.x == 1.f || p.x == 2.f);
        CHECK(p.z == 1.f || p.z == 2.f);
        CHECK(p.y == 70.f || p.y == 71.f);
        const V3 n = normal_at(m.cutout, i);
        CHECK(n.y == 0.f);
        CHECK_NEAR(n.x * n.x + n.z * n.z, 1.0, 1e-6);
        // Every vertex samples the tall grass tile (column t % 8, row t / 8 of the 8x8 atlas).
        const float u0 = static_cast<float>(T_TALL_GRASS % 8) / 8.f;
        const float v0 = static_cast<float>(T_TALL_GRASS / 8) / 8.f;
        const float u = m.cutout.texcoords[i * 2], v = m.cutout.texcoords[i * 2 + 1];
        CHECK(u >= u0 && u <= u0 + 0.125f);
        CHECK(v >= v0 && v <= v0 + 0.125f);
    }
    // Double-sided: the 4 quads have 4 distinct normals, in 2 opposite pairs.
    V3 normals[4];
    for (std::size_t q = 0; q < 4; ++q) normals[q] = normal_at(m.cutout, q * 4);
    int opposite_pairs = 0;
    for (int a = 0; a < 4; ++a)
        for (int b = a + 1; b < 4; ++b)
            if (normals[a].x == -normals[b].x && normals[a].z == -normals[b].z) ++opposite_pairs;
    CHECK(opposite_pairs == 2);
    // Each triangle is counter-clockwise seen from the side its normal points to.
    for (std::size_t t = 0; t + 2 < m.cutout.indices.size(); t += 3) {
        const V3 a = pos_at(m.cutout, m.cutout.indices[t]), b = pos_at(m.cutout, m.cutout.indices[t + 1]),
                 c = pos_at(m.cutout, m.cutout.indices[t + 2]);
        const V3 n = normal_at(m.cutout, m.cutout.indices[t]);
        const V3 e1{b.x - a.x, b.y - a.y, b.z - a.z}, e2{c.x - a.x, c.y - a.y, c.z - a.z};
        const V3 cr{e1.y * e2.z - e1.z * e2.y, e1.z * e2.x - e1.x * e2.z, e1.x * e2.y - e1.y * e2.x};
        CHECK(cr.x * n.x + cr.y * n.y + cr.z * n.z > 0.f);
    }
}

TEST(test_mesh_flower_cross) {
    World w = air_world();
    w.set(1, 70, 1, blk::FLOWER);
    const ChunkMesh m = layers_of(w, ChunkPos{0, 4, 0});
    CHECK(m.cutout.vertex_count() == 16);
    // Every vertex samples the flower tile (T_FLOWER: column t % 8, row t / 8 of the 8x8 atlas).
    const float u0 = static_cast<float>(T_FLOWER % 8) / 8.f, v0 = static_cast<float>(T_FLOWER / 8) / 8.f;
    for (std::size_t i = 0; i < m.cutout.vertex_count(); ++i) {
        const float u = m.cutout.texcoords[i * 2], v = m.cutout.texcoords[i * 2 + 1];
        CHECK(u >= u0 && u <= u0 + 0.125f);
        CHECK(v >= v0 && v <= v0 + 0.125f);
    }
}

TEST(test_mesh_plant_does_not_hide_neighbor) {
    World w = air_world();
    w.set(1, 70, 1, blk::STONE);
    w.set(2, 70, 1, blk::TALL_GRASS);
    const ChunkMesh m = layers_of(w, ChunkPos{0, 4, 0});
    CHECK(m.opaque.vertex_count() == 24);
    CHECK(count_normal_x(m.opaque, 1.f) == 4);  // the face toward the plant
    CHECK(m.cutout.vertex_count() == 16);
}

TEST(test_mesh_plant_surrounded_still_drawn) {
    // A cross plant has no faces to cull: even boxed in by stone it emits its 16 vertices.
    World w = air_world();
    w.set(5, 70, 5, blk::FLOWER);
    for (int d = -1; d <= 1; d += 2) {
        w.set(5 + d, 70, 5, blk::STONE);
        w.set(5, 70 + d, 5, blk::STONE);
        w.set(5, 70, 5 + d, blk::STONE);
    }
    const ChunkMesh m = layers_of(w, ChunkPos{0, 4, 0});
    CHECK(m.cutout.vertex_count() == 16);
    CHECK(m.opaque.vertex_count() == 6 * 24);  // plants hide nothing
}

TEST(test_mesh_translucent_different_ids) {
    World w = air_world();
    w.set(1, 70, 1, blk::ICE);
    w.set(2, 70, 1, blk::WATER);
    const ChunkMesh m = layers_of(w, ChunkPos{0, 4, 0});
    CHECK(m.translucent.vertex_count() == 48);
    CHECK(m.opaque.vertex_count() == 0);
}

TEST(test_mesh_ice_same_id_culled) {
    // ICE is a Translucent Cube: same-id neighbors cull, and no face is lowered.
    World w = air_world();
    w.set(1, 70, 1, blk::ICE);
    w.set(1, 71, 1, blk::ICE);
    const ChunkMesh m = layers_of(w, ChunkPos{0, 4, 0});
    CHECK(m.translucent.vertex_count() == 40);
    CHECK(max_y(m.translucent) == 72.f);
}

TEST(test_mesh_leaves_hidden_by_stone) {
    // Cutout faces skip opaque neighbors: the leaves' face toward the stone is gone.
    World w = air_world();
    w.set(1, 70, 1, blk::LEAVES);
    w.set(2, 70, 1, blk::STONE);
    const ChunkMesh m = layers_of(w, ChunkPos{0, 4, 0});
    CHECK(m.cutout.vertex_count() == 20);
    CHECK(count_normal_x(m.cutout, 1.f) == 0);
    CHECK(m.opaque.vertex_count() == 24);  // leaves hide nothing
}

TEST(test_mesh_leaves_beside_water) {
    // Cutout and Translucent are both non-opaque, so each draws its face toward the other.
    World w = air_world();
    w.set(1, 70, 1, blk::LEAVES);
    w.set(2, 70, 1, blk::WATER);
    const ChunkMesh m = layers_of(w, ChunkPos{0, 4, 0});
    CHECK(m.cutout.vertex_count() == 24);
    CHECK(m.translucent.vertex_count() == 24);
}

TEST(test_mesh_water_column) {
    World w = air_world();
    w.set(1, 70, 1, blk::WATER);
    w.set(1, 71, 1, blk::WATER);
    const ChunkMesh m = layers_of(w, ChunkPos{0, 4, 0});
    // 8 side faces, the top face, and the bottom face; nothing between the two blocks.
    CHECK(m.translucent.vertex_count() == 40);
    std::size_t up = 0, down = 0;
    for (std::size_t i = 0; i < m.translucent.vertex_count(); ++i) {
        const V3 n = normal_at(m.translucent, i), p = pos_at(m.translucent, i);
        if (n.y > 0.5f) {
            ++up;
            CHECK(p.y == 71.875f);
        }
        if (n.y < -0.5f) {
            ++down;
            CHECK(p.y == 70.f);
        }
    }
    CHECK(up == 4);
    CHECK(down == 4);
    CHECK(max_y(m.translucent) == 71.875f);
    CHECK(count_y(m.translucent, 70.875f) == 0);  // the lower block is full height
    CHECK(count_y(m.translucent, 71.f) == 16);    // lower sides' tops and upper sides' bottoms
}

TEST(test_mesh_water_under_ice_lowered) {
    // ICE is not the same liquid, so the water below it keeps a lowered top face.
    World w = air_world();
    w.set(1, 70, 1, blk::WATER);
    w.set(1, 71, 1, blk::ICE);
    const ChunkMesh m = layers_of(w, ChunkPos{0, 4, 0});
    CHECK(m.translucent.vertex_count() == 48);
    CHECK(count_y(m.translucent, 70.875f) == 4 + 4 * 2);  // water top face + its 4 sides' tops
}

TEST(test_mesh_water_world_top) {
    // Above the world reads as AIR, so water at y = 255 is lowered to 255.875.
    World w = air_world();
    w.set(1, 255, 1, blk::WATER);
    const ChunkMesh m = layers_of(w, ChunkPos{0, 15, 0});
    CHECK(m.translucent.vertex_count() == 24);
    CHECK(max_y(m.translucent) == 255.875f);
}

TEST(test_mesh_water_negative_border) {
    // Same-id culling works across the chunk border at x = -1 / 0.
    World w = air_world();
    w.set(-1, 70, 1, blk::WATER);
    w.set(0, 70, 1, blk::WATER);
    CHECK(layers_of(w, ChunkPos{-1, 4, 0}).translucent.vertex_count() == 20);
    CHECK(layers_of(w, ChunkPos{0, 4, 0}).translucent.vertex_count() == 20);
}

TEST(test_mesh_water_unloaded_neighbor) {
    // Column 2 is unloaded (AIR), so the water at x = 31 keeps its +X face.
    World w = air_world();
    w.set(31, 70, 1, blk::WATER);
    CHECK(layers_of(w, ChunkPos{1, 4, 0}).translucent.vertex_count() == 24);
}

TEST(test_mesh_layers_border) {
    World w = air_world();
    w.set(15, 70, 1, blk::LEAVES);
    w.set(16, 70, 1, blk::LEAVES);
    const ChunkMesh a = layers_of(w, ChunkPos{0, 4, 0});
    const ChunkMesh b = layers_of(w, ChunkPos{1, 4, 0});
    CHECK(a.cutout.vertex_count() == 24);
    CHECK(b.cutout.vertex_count() == 24);
    CHECK(count_normal_x(a.cutout, 1.f) == 4);   // x = 15's face toward x = 16
    CHECK(count_normal_x(b.cutout, -1.f) == 4);  // x = 16's face toward x = 15
    CHECK(a.opaque.vertex_count() == 0);
    CHECK(b.opaque.vertex_count() == 0);
}

TEST(test_chunk_mesh_empty) {
    World w = air_world();
    CHECK(layers_of(w, ChunkPos{0, 4, 0}).empty());
    // Any one non-empty layer makes the chunk mesh non-empty.
    const BlockId one_per_layer[3] = {blk::STONE, blk::LEAVES, blk::WATER};
    for (BlockId id : one_per_layer) {
        World w2 = air_world();
        w2.set(1, 70, 1, id);
        CHECK(!layers_of(w2, ChunkPos{0, 4, 0}).empty());
    }
}

TEST(test_mesh_full_leaves_chunk) {
    // Leaves never cull each other: 4096 blocks x 6 faces x 4 = 98,304 vertices, more than a
    // 16-bit index reaches, so indices are 32-bit and split_mesh cuts the layer for the GPU.
    World w = air_world();
    for (int y = 64; y < 80; ++y)
        for (int z = 0; z < 16; ++z)
            for (int x = 0; x < 16; ++x) w.set(x, y, z, blk::LEAVES);
    const ChunkMesh m = layers_of(w, ChunkPos{0, 4, 0});
    CHECK(m.cutout.vertex_count() == 98304);
    CHECK(m.cutout.indices.size() == 147456);
    CHECK(indices_in_range(m.cutout));
    const std::vector<MeshData> parts = split_mesh(m.cutout);
    CHECK(parts.size() == 2);
    CHECK(parts[0].vertex_count() == 65536);
    CHECK(parts[1].vertex_count() == 32768);
    for (const MeshData& part : parts) {
        CHECK(indices_in_range(part));
        CHECK(part.indices.size() == part.vertex_count() / 4 * 6);
        CHECK(part.texcoords.size() == part.vertex_count() * 2);
        CHECK(part.normals.size() == part.vertex_count() * 3);
        CHECK(part.colors.size() == part.vertex_count() * 4);
    }
    // The second part starts with the original's vertex 65,536, its first index points at it.
    CHECK(parts[1].positions[0] == m.cutout.positions[65536 * 3]);
    CHECK(parts[1].indices[0] == m.cutout.indices[16384 * 6] - 65536);
}

TEST(test_split_mesh_boundaries) {
    // Built from one face per stone block in a row: n faces = 4n vertices.
    auto faces = [](int n) {
        World w = air_world();
        for (int i = 0; i < n; ++i) w.set(1 + 2 * i, 70, 1, blk::STONE);  // separated: 6 faces each
        return mesh_of(w, ChunkPos{0, 4, 0});
    };
    CHECK(split_mesh(MeshData{}).empty());
    const MeshData one = faces(1);  // 24 vertices
    const std::vector<MeshData> whole = split_mesh(one, 24);
    CHECK(whole.size() == 1);  // exactly at the limit: one part, unchanged
    CHECK(whole[0].positions == one.positions);
    CHECK(whole[0].indices == one.indices);
    const std::vector<MeshData> cut = split_mesh(one, 20);  // just below: 20 + 4
    CHECK(cut.size() == 2);
    CHECK(cut[0].vertex_count() == 20);
    CHECK(cut[1].vertex_count() == 4);
    CHECK(cut[1].indices.size() == 6);
    CHECK(indices_in_range(cut[0]));
    CHECK(indices_in_range(cut[1]));
    CHECK(split_mesh(one, 28).size() == 1);  // just above
    // Invalid limits: not a multiple of 4 rounds down (6 -> 4), below 4 still takes one quad.
    for (std::size_t bad : {std::size_t{6}, std::size_t{1}}) {
        const std::vector<MeshData> rounded = split_mesh(one, bad);
        CHECK(rounded.size() == 6);
        for (const MeshData& q : rounded) {
            CHECK(q.vertex_count() == 4);
            CHECK(indices_in_range(q));
        }
    }
    const std::vector<MeshData> quads = split_mesh(faces(2), 4);  // one quad per part
    CHECK(quads.size() == 12);
    for (const MeshData& q : quads) {
        CHECK(q.vertex_count() == 4);
        CHECK(q.indices == (std::vector<uint32_t>{0, 1, 2, 0, 2, 3}));
    }
}

// ---- M3 Task 3: smooth light and ambient occlusion ----

TEST(test_mesh_vertex_light_channels) {
    {
        // A fully sky-lit flat top: every vertex R 255, G 0.
        const World w = lit_world(flat_ground);
        const MeshData m = mesh_of(w, ChunkPos{0, 4, 0});
        int tops = 0;
        for (std::size_t i = 0; i < m.vertex_count(); ++i) {
            if (normal_at(m, i).y < 0.5f) continue;
            ++tops;
            CHECK(m.colors[i * 4] == 255);
            CHECK(m.colors[i * 4 + 1] == 0);
            CHECK(m.colors[i * 4 + 2] == 255);
            CHECK(m.colors[i * 4 + 3] == 255);
        }
        CHECK(tops == 256 * 4);
    }
    {
        // The same top in a dark room (air x 6..10, y 65..67, z 6..10 in solid STONE) lit only by
        // a torch 2 cells above block (8, 64, 8): around each vertex of its top the 4 cells hold
        // 13, 12, 12, 11 -> G = 12 * 17; no sky light -> R = 0.
        World w = lit_world([](int x, int y, int z) {
            const bool room = x >= 6 && x <= 10 && y >= 65 && y <= 67 && z >= 6 && z <= 10;
            return y <= 80 && !room ? blk::STONE : blk::AIR;
        });
        CHECK(w.set(8, 66, 8, blk::TORCH));
        const MeshData m = mesh_of(w, ChunkPos{0, 4, 0});
        for (V3 p : {V3{8, 65, 8}, V3{9, 65, 8}, V3{8, 65, 9}, V3{9, 65, 9}}) {
            const std::size_t v = vertex_at(m, p, UP);
            CHECK(v != SIZE_MAX);
            CHECK(channel(m, v, 0) == 0);
            CHECK(channel(m, v, 1) == 12 * 17);
            CHECK(channel(m, v, 2) == 255);
        }
    }
}

TEST(test_mesh_light_ignores_opaque_cells) {
    // Vertex (9, 65, 9) of the top of (8, 64, 8): cell (9, 65, 9) is STONE, the other 3 have sky
    // 15 -> R 255, not 3 * 255 / 4.
    World w = lit_world(flat_ground);
    CHECK(w.set(9, 65, 9, blk::STONE));
    const MeshData m = mesh_of(w, ChunkPos{0, 4, 0});
    const std::size_t v = vertex_at(m, V3{9, 65, 9}, UP);
    CHECK(v != SIZE_MAX);
    // The vertex is shared by 3 tops; each samples the same 4 cells.
    for (std::size_t i = 0; i < m.vertex_count(); ++i) {
        const V3 p = pos_at(m, i);
        if (p.x == 9.f && p.y == 65.f && p.z == 9.f && normal_at(m, i).y > 0.5f) CHECK(m.colors[i * 4] == 255);
    }
    CHECK(channel(m, v, 0) == 255);
}

TEST(test_mesh_ao_levels) {
    // The top of a lone block at (8, 70, 8); its vertex (9, 71, 9) has sides (9, 71, 8) and
    // (8, 71, 9) and corner (9, 71, 9). B = 255 * {0.55, 0.7, 0.85, 1.0}[level], rounded.
    struct Case {
        std::vector<V3> occluders;
        int b;
    };
    const Case cases[] = {
        {{}, 255},
        {{V3{9, 71, 9}}, 217},                            // corner only: 0.85 * 255 = 216.75
        {{V3{9, 71, 8}}, 217},                            // one side only
        {{V3{8, 71, 9}}, 217},                            // the other side only
        {{V3{9, 71, 8}, V3{9, 71, 9}}, 179},              // one side + corner: 0.7 * 255 = 178.5
        {{V3{9, 71, 8}, V3{8, 71, 9}}, 140},              // two sides: 0.55 * 255 = 140.25
        {{V3{9, 71, 8}, V3{8, 71, 9}, V3{9, 71, 9}}, 140},  // two sides + corner
    };
    for (const Case& c : cases) {
        World w = air_world();
        w.set(8, 70, 8, blk::STONE);
        for (V3 o : c.occluders) w.set(static_cast<int>(o.x), static_cast<int>(o.y), static_cast<int>(o.z), blk::STONE);
        const MeshData m = mesh_of(w, ChunkPos{0, 4, 0});
        const std::size_t v = vertex_at(m, V3{9, 71, 9}, UP);
        CHECK(v != SIZE_MAX);
        CHECK(channel(m, v, 2) == c.b);
        // The opposite vertex (8, 71, 8) sees none of these occluders.
        CHECK(channel(m, vertex_at(m, V3{8, 71, 8}, UP), 2) == 255);
    }
    {
        // Non-opaque neighbors (leaves, a plant, water) do not occlude.
        World w = air_world();
        w.set(8, 70, 8, blk::STONE);
        w.set(9, 71, 8, blk::LEAVES);
        w.set(8, 71, 9, blk::TALL_GRASS);
        w.set(9, 71, 9, blk::WATER);
        const MeshData m = mesh_of(w, ChunkPos{0, 4, 0});
        CHECK(channel(m, vertex_at(m, V3{9, 71, 9}, UP), 2) == 255);
    }
    {
        // Side faces scale their own shade. The +X face of the block has face cell (9, 70, 8); its
        // vertex (9, 71, 9) has sides (9, 71, 8) and (9, 70, 9) and corner (9, 71, 9). STONE at
        // (9, 71, 8) is one side: 0.85 * 204 = 173.4 -> 173. Vertex (9, 70, 9) sees nothing.
        World w = air_world();
        w.set(8, 70, 8, blk::STONE);
        w.set(9, 71, 8, blk::STONE);
        const MeshData m = mesh_of(w, ChunkPos{0, 4, 0});
        const std::size_t v = vertex_at(m, V3{9, 71, 9}, V3{1, 0, 0});
        CHECK(v != SIZE_MAX);
        CHECK(channel(m, v, 2) == 173);
        CHECK(channel(m, vertex_at(m, V3{9, 70, 9}, V3{1, 0, 0}), 2) == 204);
    }
}

TEST(test_mesh_ao_across_chunk_border) {
    {
        // Block (15, 70, 8) in chunk {0,4,0}; its top vertex (16, 71, 9) has side (16, 71, 8) in
        // chunk {1,4,0}.
        World w = air_world();
        w.set(15, 70, 8, blk::STONE);
        const MeshData m0 = mesh_of(w, ChunkPos{0, 4, 0});
        CHECK(channel(m0, vertex_at(m0, V3{16, 71, 9}, UP), 2) == 255);
        w.set(16, 71, 8, blk::STONE);
        const MeshData m = mesh_of(w, ChunkPos{0, 4, 0});
        CHECK(channel(m, vertex_at(m, V3{16, 71, 9}, UP), 2) == 217);
    }
    {
        // The diagonal chunk {1,4,1}: corner (16, 71, 16) of block (15, 70, 15).
        World w = air_world();
        w.set(15, 70, 15, blk::STONE);
        w.set(16, 71, 16, blk::STONE);
        const MeshData m = mesh_of(w, ChunkPos{0, 4, 0});
        CHECK(channel(m, vertex_at(m, V3{16, 71, 16}, UP), 2) == 217);
    }
    {
        // Vertically: block (8, 79, 8) at the top of section 4, occluder (9, 80, 8) in section 5.
        World w = air_world();
        w.set(8, 79, 8, blk::STONE);
        w.set(9, 80, 8, blk::STONE);
        const MeshData m = mesh_of(w, ChunkPos{0, 4, 0});
        CHECK(channel(m, vertex_at(m, V3{9, 80, 9}, UP), 2) == 217);
    }
    {
        // Negative side: block (0, 70, 0); occluder (-1, 71, -1) in chunk {-1,4,-1}.
        World w = air_world();
        w.set(0, 70, 0, blk::STONE);
        w.set(-1, 71, -1, blk::STONE);
        const MeshData m = mesh_of(w, ChunkPos{0, 4, 0});
        CHECK(channel(m, vertex_at(m, V3{0, 71, 0}, UP), 2) == 217);
    }
}

TEST(test_mesh_quad_flip) {
    // The top of (8, 70, 8) lists v0 (8,71,8), v1 (8,71,9), v2 (9,71,9), v3 (9,71,8). Corner
    // occluders at (7, 71, 9) and (9, 71, 7) darken v1 and v3 only: a0 + a2 = 6 > a1 + a3 = 4, so
    // the quad splits along v1-v3. The mirror pattern, (7, 71, 7) and (9, 71, 9), darkens v0 and
    // v2 and keeps the v0-v2 split.
    auto shared_edge = [](std::initializer_list<V3> occluders) {
        World w = air_world();
        w.set(8, 70, 8, blk::STONE);
        for (V3 o : occluders) w.set(static_cast<int>(o.x), static_cast<int>(o.y), static_cast<int>(o.z), blk::STONE);
        const MeshData m = mesh_of(w, ChunkPos{0, 4, 0});
        const std::size_t v0 = vertex_at(m, V3{8, 71, 8}, UP);
        CHECK(v0 != SIZE_MAX && v0 % 4 == 0);
        const std::size_t q = v0 / 4;
        // The two corners both triangles use.
        int uses[4] = {};
        for (std::size_t k = 0; k < 6; ++k) ++uses[m.indices[q * 6 + k] - v0];
        int edge = 0;
        for (int c = 0; c < 4; ++c)
            if (uses[c] == 2) edge |= 1 << c;
        // The triangles stay counter-clockwise from above (+Y).
        for (std::size_t t = 0; t < 2; ++t) {
            const V3 a = pos_at(m, m.indices[q * 6 + t * 3]), b = pos_at(m, m.indices[q * 6 + t * 3 + 1]),
                     c = pos_at(m, m.indices[q * 6 + t * 3 + 2]);
            const float cy = (b.z - a.z) * (c.x - a.x) - (b.x - a.x) * (c.z - a.z);
            CHECK(cy > 0.f);
        }
        return edge;
    };
    CHECK(shared_edge({}) == 0b0101);                                // v0-v2: the default split
    CHECK(shared_edge({V3{7, 71, 9}, V3{9, 71, 7}}) == 0b1010);      // flipped to v1-v3
    CHECK(shared_edge({V3{7, 71, 7}, V3{9, 71, 9}}) == 0b0101);      // mirror: not flipped
    CHECK(shared_edge({V3{7, 71, 9}}) == 0b1010);                    // a0 + a2 = 6 > 5
    CHECK(shared_edge({V3{7, 71, 7}}) == 0b0101);                    // 5 > 6 is false
}

// ---- M3 Task 3: Coverage Checklist additions ----

TEST(test_padded_snapshot_light) {
    // The snapshot copies light (high nibble sky, low nibble block) from the chunk and its border;
    // above the world reads sky 15, below it and unloaded columns read 0.
    World w = lit_world(flat_ground);
    CHECK(w.set(15, 70, 15, blk::TORCH));
    const Padded pad = snapshot(w, ChunkPos{0, 4, 0});
    CHECK(pad.light_at(15, 6, 15) == 0xFE);             // sky 15, torch 14
    CHECK(pad.light_at(16, 6, 16) == 0xFC);             // diagonal chunk {1,4,1}: d = 2 -> 12
    CHECK(pad.light_at(15, 0, 15) == 0x00);             // y 64: STONE
    CHECK(pad.light_at(-1, 16, -1) == 0xF0);            // y 80 in chunk {-1,5,-1}: open sky
    const Padded top = snapshot(w, ChunkPos{0, 15, 0});
    CHECK(top.light_at(4, 16, 4) == 0xF0);               // above the world
    const Padded bottom = snapshot(w, ChunkPos{0, 0, 0});
    CHECK(bottom.light_at(4, -1, 4) == 0x00);            // below the world
    const Padded edge = snapshot(w, ChunkPos{1, 4, 0});
    CHECK(edge.light_at(16, 4, 4) == 0x00);              // unloaded column (2, 0)
}

TEST(test_mesh_cross_plant_light) {
    // A plant takes the light of its own cell at every vertex, and the plain SHADE_X (no AO).
    World w = lit_world(flat_ground);
    CHECK(w.set(8, 65, 8, blk::TALL_GRASS));
    CHECK(w.set(9, 65, 8, blk::STONE));  // would occlude a cube face
    const MeshData m = layers_of(w, ChunkPos{0, 4, 0}).cutout;
    CHECK(m.vertex_count() == 16);
    for (std::size_t i = 0; i < m.vertex_count(); ++i) {
        CHECK(m.colors[i * 4] == 255);
        CHECK(m.colors[i * 4 + 1] == 0);
        CHECK(m.colors[i * 4 + 2] == SHADE_X);
    }
}

TEST(test_mesh_water_top_light) {
    // A liquid's lowered top samples the cells above it like a cube top: sky 15 -> R 255.
    World w = lit_world(flat_ground);
    CHECK(w.set(8, 65, 8, blk::WATER));
    const MeshData m = layers_of(w, ChunkPos{0, 4, 0}).translucent;
    const std::size_t v = vertex_at(m, V3{9, 65.875f, 9}, UP);
    CHECK(v != SIZE_MAX);
    CHECK(channel(m, v, 0) == 255);
    CHECK(channel(m, v, 2) == 255);
}

TEST(test_mesh_light_average_rounds) {
    // Sky 15, 15, 15, 14 around a vertex averages 14.75 -> 14.75 * 17 = 250.75, rounded to 251.
    World w = lit_world(flat_ground);
    CHECK(w.set(9, 66, 9, blk::STONE));  // shades (9, 65, 9) to 14 from the side
    const MeshData m = mesh_of(w, ChunkPos{0, 4, 0});
    CHECK(channel(m, vertex_at(m, V3{9, 65, 9}, UP), 0) == 251);
}
