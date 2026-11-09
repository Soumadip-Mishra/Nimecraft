// Tests for src/ai.*: passive wandering, fleeing, the Tusker charge and herd, hostile chase and
// melee, Thornspitter kiting, and the Gloomwalker fading at dawn.
#include "ai.hpp"
#include "block.hpp"
#include "chunk.hpp"
#include "coords.hpp"
#include "mobs.hpp"
#include "test.hpp"
#include "world.hpp"

#include <cmath>
#include <cstddef>
#include <memory>
#include <vector>

namespace {

// Columns (-3..2, -3..2) with STONE at y = 0..69: the floor top is y = 70, x and z in [-48, 48).
// Every cell has light 0.
World flat() {
    World w(1);
    for (int cx = -3; cx <= 2; ++cx)
        for (int cz = -3; cz <= 2; ++cz) {
            Column col;
            for (std::size_t i = 0; i < col.size(); ++i) col[i] = std::make_unique<Chunk>();
            for (int y = 0; y < 70; ++y)
                for (int lz = 0; lz < CHUNK; ++lz)
                    for (int lx = 0; lx < CHUNK; ++lx)
                        col[static_cast<std::size_t>(y / CHUNK)]->blocks[static_cast<std::size_t>(
                            Chunk::index(lx, y % CHUNK, lz))] = blk::STONE;
            w.insert_column(ColumnPos{cx, cz}, std::move(col));
        }
    return w;
}

void fill(World& w, int x0, int y0, int z0, int x1, int y1, int z1, BlockId b) {
    for (int x = x0; x <= x1; ++x)
        for (int y = y0; y <= y1; ++y)
            for (int z = z0; z <= z1; ++z)
                w.chunk(chunk_of(x, y, z))->set(floor_mod(x, CHUNK), floor_mod(y, CHUNK), floor_mod(z, CHUNK), b);
}

void set_sky(World& w, BlockPos p, uint8_t sky) {
    Chunk* c = w.chunk(chunk_of(p.x, p.y, p.z));
    set_sky_light(*c, Chunk::index(floor_mod(p.x, CHUNK), floor_mod(p.y, CHUNK), floor_mod(p.z, CHUNK)), sky);
}

Mob make(MobType t, Vector3 pos, uint32_t id = 1, uint32_t rng = 12345u) {
    Mob m{};
    m.id = id;
    m.type = t;
    m.pos = pos;
    m.vel = Vector3{0, 0, 0};
    m.health = mob_def(t).health;
    m.rng = rng;
    return m;
}

AiContext ctx(const World& w, Vector3 player, bool alive = true, float day = 0.15f) {
    AiContext c{};
    c.player_pos = player;
    c.player_alive = alive;
    c.daylight = day;
    c.world = &w;
    return c;
}

float hlen(Vector3 v) {
    return std::sqrt(v.x * v.x + v.z * v.z);
}

// move_dir . (to - from), horizontal.
float toward(Vector3 move, Vector3 from, Vector3 to) {
    return move.x * (to.x - from.x) + move.z * (to.z - from.z);
}

float hdist(Vector3 a, Vector3 b) {
    return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.z - b.z) * (a.z - b.z));
}

}  // namespace

TEST(test_ai_passive_wanders) {
    const World w = flat();
    Mob m = make(MobType::Fluffhorn, Vector3{0.5f, 70.f, 0.5f});
    const AiContext c = ctx(w, Vector3{0.f, 70.f, 40.f}, false);
    const float dt = 0.125f;
    Vector3 last{1e9f, 0.f, 0.f};
    float last_change = -1.f;
    int changes = 0;
    bool moved = false;
    for (int i = 0; i < 480; ++i) {  // 60 s, the mob kept in place
        const float t = static_cast<float>(i) * dt;
        const AiOutput out = ai_step(m, c, dt);
        CHECK(out.next == AiState::Idle || out.next == AiState::Wander);
        CHECK(!out.attack && !out.shoot);
        m.state = out.next;
        if (hlen(out.move_dir) > 0.f) {
            moved = true;
            CHECK(std::fabs(hlen(out.move_dir) - 2.5f) < 1e-3f);  // Fluffhorn speed
            CHECK(toward(out.move_dir, m.pos, m.target) > 0.f);
        }
        if (m.target.x != last.x || m.target.z != last.z) {
            CHECK(hdist(m.target, m.pos) <= 8.f);
            if (last_change >= 0.f) {
                CHECK(t - last_change >= 4.f - 1e-4f);
                CHECK(t - last_change <= 10.f + dt);
            }
            last = m.target;
            last_change = t;
            ++changes;
        }
    }
    CHECK(changes >= 6 && changes <= 16);  // first pick at 0 s, then one every 4-10 s
    CHECK(moved);
}

TEST(test_ai_passive_idle_at_target) {
    // Standing within 0.5 of its target, a passive mob idles.
    const World w = flat();
    Mob m = make(MobType::Hopper, Vector3{0.5f, 70.f, 0.5f});
    const AiContext c = ctx(w, Vector3{0.f, 70.f, 40.f}, false);
    ai_step(m, c, 0.1f);  // picks a target
    m.pos = Vector3{m.target.x + 0.3f, 70.f, m.target.z};
    const AiOutput out = ai_step(m, c, 0.1f);
    CHECK(out.next == AiState::Idle);
    CHECK(hlen(out.move_dir) == 0.f);
}

TEST(test_ai_passive_flees_when_hit) {
    const World w = flat();
    Mob m = make(MobType::Fluffhorn, Vector3{5.5f, 70.f, 0.5f});
    const Vector3 player{2.5f, 70.f, 0.5f};
    AiContext hit = ctx(w, player);
    hit.last_attacker_hit_mob = m.id;
    const AiContext calm = ctx(w, player);
    const float dt = 0.25f;
    AiOutput out = ai_step(m, hit, dt);
    m.state = out.next;
    CHECK(out.next == AiState::Flee);
    CHECK(toward(out.move_dir, m.pos, player) < 0.f);
    CHECK(out.move_dir.x > 0.f);  // the player is at -X
    for (int i = 0; i < 15; ++i) {  // 3.75 s later: still fleeing
        out = ai_step(m, calm, dt);
        m.state = out.next;
        CHECK(out.next == AiState::Flee);
        CHECK(toward(out.move_dir, m.pos, player) < 0.f);
    }
    out = ai_step(m, calm, dt);  // 4.0 s
    CHECK(out.next != AiState::Flee);
    // A hit on another mob does not scare this one.
    Mob other = make(MobType::Fluffhorn, Vector3{5.5f, 70.f, 0.5f}, 2);
    AiContext not_me = ctx(w, player);
    not_me.last_attacker_hit_mob = 99u;
    CHECK(ai_step(other, not_me, dt).next != AiState::Flee);
}

TEST(test_ai_tusker_charges_once) {
    const World w = flat();
    Mob m = make(MobType::Tusker, Vector3{8.5f, 70.f, 0.5f});
    const Vector3 player{0.5f, 70.f, 0.5f};
    AiContext hit = ctx(w, player);
    hit.last_attacker_hit_mob = m.id;
    const AiContext calm = ctx(w, player);
    AiOutput out = ai_step(m, hit, 0.1f);
    m.state = out.next;
    CHECK(out.next == AiState::Charge);
    CHECK(std::fabs(hlen(out.move_dir) - 6.f) < 1e-3f);
    CHECK(toward(out.move_dir, m.pos, player) > 0.f);
    out = ai_step(m, calm, 0.1f);  // keeps charging
    m.state = out.next;
    CHECK(out.next == AiState::Charge && std::fabs(hlen(out.move_dir) - 6.f) < 1e-3f);
    // Reaching the player ends the charge with one attack, then it flees.
    m.pos = Vector3{1.5f, 70.f, 0.5f};
    out = ai_step(m, calm, 0.1f);
    m.state = out.next;
    CHECK(out.attack);
    CHECK(out.next == AiState::Flee);
    // Every later hit only makes it flee.
    for (int i = 0; i < 3; ++i) {
        out = ai_step(m, hit, 0.1f);
        m.state = out.next;
        CHECK(out.next == AiState::Flee && !out.attack);
        for (int k = 0; k < 50; ++k) {
            out = ai_step(m, calm, 0.1f);
            m.state = out.next;
            CHECK(out.next != AiState::Charge);
        }
    }
    // A second hit during the charge also turns it into a flee.
    Mob t = make(MobType::Tusker, Vector3{8.5f, 70.f, 0.5f}, 1);
    t.state = ai_step(t, hit, 0.1f).next;
    CHECK(t.state == AiState::Charge);
    CHECK(ai_step(t, hit, 0.1f).next == AiState::Flee);
}

TEST(test_ai_tusker_charge_times_out) {
    // A charge that never reaches the player ends after 3 s and turns into a flee.
    const World w = flat();
    Mob m = make(MobType::Tusker, Vector3{8.5f, 70.f, 0.5f});
    const Vector3 player{0.5f, 70.f, 0.5f};
    AiContext hit = ctx(w, player);
    hit.last_attacker_hit_mob = m.id;
    m.state = ai_step(m, hit, 0.25f).next;
    const AiContext calm = ctx(w, player);
    for (int i = 0; i < 11; ++i) m.state = ai_step(m, calm, 0.25f).next;  // 2.75 s
    CHECK(m.state == AiState::Charge);
    m.state = ai_step(m, calm, 0.25f).next;  // 3.0 s
    CHECK(m.state == AiState::Flee);
}

TEST(test_ai_tusker_herd) {
    const World w = flat();
    const AiContext base = ctx(w, Vector3{0.f, 70.f, 40.f}, false);
    {
        std::vector<Mob> herd{make(MobType::Tusker, Vector3{0.5f, 70.f, 0.5f}, 1),
                              make(MobType::Tusker, Vector3{10.5f, 70.f, 0.5f}, 2)};
        AiContext c = base;
        c.mobs = &herd;
        const AiOutput out = ai_step(herd[0], c, 0.1f);
        CHECK(out.next == AiState::Wander);
        CHECK(out.move_dir.x > 0.f && std::fabs(out.move_dir.z) < 1e-4f);
        CHECK(std::fabs(hlen(out.move_dir) - 3.f) < 1e-3f);
    }
    {
        // At 10.1 the other Tusker is ignored: the same output as a lone Tusker with the same rng.
        std::vector<Mob> herd{make(MobType::Tusker, Vector3{0.5f, 70.f, 0.5f}, 1),
                              make(MobType::Tusker, Vector3{10.6f, 70.f, 0.5f}, 2)};
        std::vector<Mob> alone{make(MobType::Tusker, Vector3{0.5f, 70.f, 0.5f}, 1)};
        AiContext c = base, solo = base;
        c.mobs = &herd;
        solo.mobs = &alone;
        for (int i = 0; i < 40; ++i) {
            const AiOutput a = ai_step(herd[0], c, 0.1f);
            const AiOutput b = ai_step(alone[0], solo, 0.1f);
            CHECK(a.move_dir.x == b.move_dir.x && a.move_dir.z == b.move_dir.z && a.next == b.next);
        }
    }
    {
        // Another type nearby is not herd; nor is a Tusker already close (within 3).
        std::vector<Mob> mixed{make(MobType::Tusker, Vector3{0.5f, 70.f, 0.5f}, 1),
                               make(MobType::Fluffhorn, Vector3{5.5f, 70.f, 0.5f}, 2)};
        std::vector<Mob> alone{make(MobType::Tusker, Vector3{0.5f, 70.f, 0.5f}, 1)};
        AiContext c = base, solo = base;
        c.mobs = &mixed;
        solo.mobs = &alone;
        const AiOutput a = ai_step(mixed[0], c, 0.1f);
        const AiOutput b = ai_step(alone[0], solo, 0.1f);
        CHECK(a.move_dir.x == b.move_dir.x && a.move_dir.z == b.move_dir.z);
        std::vector<Mob> close{make(MobType::Tusker, Vector3{0.5f, 70.f, 0.5f}, 1),
                               make(MobType::Tusker, Vector3{2.5f, 70.f, 0.5f}, 2)};
        std::vector<Mob> alone2{make(MobType::Tusker, Vector3{0.5f, 70.f, 0.5f}, 1)};
        c.mobs = &close;
        solo.mobs = &alone2;
        const AiOutput d = ai_step(close[0], c, 0.1f);
        const AiOutput e = ai_step(alone2[0], solo, 0.1f);
        CHECK(d.move_dir.x == e.move_dir.x && d.move_dir.z == e.move_dir.z);
    }
}

TEST(test_ai_tusker_herd_nearest) {
    // Two group members within 10: it heads for the nearer one (at -Z, 6 away), not the one at +X.
    const World w = flat();
    std::vector<Mob> herd{make(MobType::Tusker, Vector3{0.5f, 70.f, 0.5f}, 1),
                          make(MobType::Tusker, Vector3{9.5f, 70.f, 0.5f}, 2),
                          make(MobType::Tusker, Vector3{0.5f, 70.f, -5.5f}, 3)};
    AiContext c = ctx(w, Vector3{0.f, 70.f, 40.f}, false);
    c.mobs = &herd;
    const AiOutput out = ai_step(herd[0], c, 0.1f);
    CHECK(out.move_dir.z < 0.f && std::fabs(out.move_dir.x) < 1e-4f);
}

TEST(test_ai_hostile_chases_in_range) {
    const World w = flat();
    Mob m = make(MobType::Gloomwalker, Vector3{0.5f, 70.f, 0.5f});
    AiOutput out = ai_step(m, ctx(w, Vector3{24.5f, 70.f, 0.5f}), 0.1f);
    CHECK(out.next == AiState::Chase);
    CHECK(out.move_dir.x > 0.f);
    CHECK(std::fabs(hlen(out.move_dir) - 3.5f) < 1e-3f);
    CHECK(!out.attack);  // out of reach
    CHECK(!m.path.empty());  // it planned a route
    Mob far = make(MobType::Gloomwalker, Vector3{0.5f, 70.f, 0.5f});
    out = ai_step(far, ctx(w, Vector3{24.6f, 70.f, 0.5f}), 0.1f);
    CHECK(out.next != AiState::Chase);
    // Cave Crawlers chase too.
    Mob crawler = make(MobType::CaveCrawler, Vector3{-20.5f, 70.f, -20.5f});
    CHECK(ai_step(crawler, ctx(w, Vector3{-10.5f, 70.f, -20.5f}), 0.1f).next == AiState::Chase);
}

TEST(test_ai_chase_repaths_every_second) {
    const World w = flat();
    Mob m = make(MobType::Gloomwalker, Vector3{0.5f, 70.f, 0.5f});
    m.state = ai_step(m, ctx(w, Vector3{10.5f, 70.f, 0.5f}), 0.25f).next;
    CHECK(!m.path.empty() && m.path.back() == (BlockPos{10, 70, 0}));
    // The player moves; the old path stays until 1 s has passed since it was made.
    for (int i = 0; i < 3; ++i) m.state = ai_step(m, ctx(w, Vector3{10.5f, 70.f, 8.5f}), 0.25f).next;
    CHECK(m.path.back() == (BlockPos{10, 70, 0}));
    m.state = ai_step(m, ctx(w, Vector3{10.5f, 70.f, 8.5f}), 0.25f).next;
    CHECK(m.path.back() == (BlockPos{10, 70, 8}));
}

TEST(test_ai_chase_follows_path_around_wall) {
    // A wall at x = 3 with a gap at z = 6; the player is seen through the gap and the chase path
    // goes through it.
    World w = flat();
    fill(w, 3, 70, -48, 3, 72, 47, blk::STONE);
    fill(w, 3, 70, 6, 3, 72, 6, blk::AIR);
    Mob m = make(MobType::CaveCrawler, Vector3{0.5f, 70.f, 6.5f});
    const AiOutput out = ai_step(m, ctx(w, Vector3{8.5f, 70.f, 7.5f}), 0.1f);
    CHECK(out.next == AiState::Chase);
    CHECK(!m.path.empty());
    bool through_gap = false;
    for (const BlockPos& p : m.path) through_gap = through_gap || (p.x == 3 && p.z == 6);
    CHECK(through_gap);
    for (const BlockPos& p : m.path) CHECK(!(p.x == 3 && p.z != 6));
}

TEST(test_ai_hostile_needs_sight) {
    World w = flat();
    fill(w, 5, 70, -48, 5, 74, 47, blk::STONE);
    Mob m = make(MobType::Gloomwalker, Vector3{0.5f, 70.f, 0.5f});
    const AiOutput out = ai_step(m, ctx(w, Vector3{10.5f, 70.f, 0.5f}), 0.1f);
    CHECK(out.next != AiState::Chase);
    CHECK(!out.attack);
}

TEST(test_ai_hostile_player_dead) {
    const World w = flat();
    Mob m = make(MobType::Gloomwalker, Vector3{0.5f, 70.f, 0.5f});
    for (int i = 0; i < 20; ++i) {
        const AiOutput out = ai_step(m, ctx(w, Vector3{1.5f, 70.f, 0.5f}, false), 0.1f);
        m.state = out.next;
        CHECK(out.next != AiState::Chase);
        CHECK(!out.attack && !out.shoot);
    }
    Mob s = make(MobType::Thornspitter, Vector3{0.5f, 70.f, 0.5f});
    const AiOutput out = ai_step(s, ctx(w, Vector3{10.5f, 70.f, 0.5f}, false), 0.1f);
    CHECK(out.next != AiState::Kite && !out.shoot);
}

TEST(test_ai_no_world) {
    // Without a world nothing is visible: no chase, and wandering still works.
    Mob m = make(MobType::Gloomwalker, Vector3{0.5f, 70.f, 0.5f});
    AiContext c{};
    c.player_pos = Vector3{2.5f, 70.f, 0.5f};
    c.player_alive = true;
    c.daylight = 0.15f;
    c.world = nullptr;
    const AiOutput out = ai_step(m, c, 0.1f);
    CHECK(out.next != AiState::Chase && !out.attack);
}

TEST(test_ai_attack_reach_and_rate) {
    const World w = flat();
    {
        Mob m = make(MobType::Gloomwalker, Vector3{0.5f, 70.f, 0.5f});
        const AiOutput out = ai_step(m, ctx(w, Vector3{0.5f + 1.2f, 70.f, 0.5f}), 0.1f);
        CHECK(out.attack);
        CHECK(out.next == AiState::Chase);
    }
    {
        Mob m = make(MobType::Gloomwalker, Vector3{0.5f, 70.f, 0.5f});
        const AiOutput out = ai_step(m, ctx(w, Vector3{0.5f + 1.21f, 70.f, 0.5f}), 0.1f);
        CHECK(!out.attack);
    }
    {
        Mob m = make(MobType::CaveCrawler, Vector3{0.5f, 70.f, 0.5f});
        const AiContext c = ctx(w, Vector3{1.5f, 70.f, 0.5f});
        std::vector<float> times;
        for (int i = 0; i < 40; ++i) {  // 4 s
            const AiOutput out = ai_step(m, c, 0.1f);
            m.state = out.next;
            if (out.attack) times.push_back(static_cast<float>(i) * 0.1f);
        }
        CHECK(times.size() >= 3 && times.size() <= 4);
        CHECK(!times.empty() && times[0] == 0.f);  // the first attack comes at once
        for (std::size_t i = 1; i < times.size(); ++i) CHECK(times[i] - times[i - 1] >= 1.f - 1e-4f);
    }
    {
        // Passive mobs never attack a nearby player unless charging.
        Mob m = make(MobType::Fluffhorn, Vector3{0.5f, 70.f, 0.5f});
        CHECK(!ai_step(m, ctx(w, Vector3{1.f, 70.f, 0.5f}), 0.1f).attack);
    }
}

TEST(test_ai_kite_keeps_distance) {
    const World w = flat();
    const Vector3 home{0.5f, 70.f, 0.5f};
    {
        Mob m = make(MobType::Thornspitter, home);
        const Vector3 player{5.5f, 70.f, 0.5f};
        const AiOutput out = ai_step(m, ctx(w, player), 0.1f);
        CHECK(out.next == AiState::Kite);
        CHECK(toward(out.move_dir, m.pos, player) < 0.f);
        CHECK(!out.attack);
    }
    {
        Mob m = make(MobType::Thornspitter, home);
        const AiContext c = ctx(w, Vector3{10.5f, 70.f, 0.5f});
        std::vector<float> shots;
        for (int i = 0; i < 50; ++i) {  // 5 s
            const AiOutput out = ai_step(m, c, 0.1f);
            m.state = out.next;
            CHECK(out.next == AiState::Kite);
            CHECK(hlen(out.move_dir) == 0.f);  // holds its ground
            if (out.shoot) shots.push_back(static_cast<float>(i) * 0.1f);
        }
        CHECK(shots.size() == 3);  // 0, 2, 4 s
        for (std::size_t i = 1; i < shots.size(); ++i) CHECK(shots[i] - shots[i - 1] >= 2.f - 1e-4f);
    }
    {
        // Band edges: 8 and 12 hold.
        Mob a = make(MobType::Thornspitter, home), b = make(MobType::Thornspitter, home);
        CHECK(hlen(ai_step(a, ctx(w, Vector3{8.5f, 70.f, 0.5f}), 0.1f).move_dir) == 0.f);
        CHECK(hlen(ai_step(b, ctx(w, Vector3{12.5f, 70.f, 0.5f}), 0.1f).move_dir) == 0.f);
    }
    {
        Mob m = make(MobType::Thornspitter, home);
        const Vector3 player{15.5f, 70.f, 0.5f};
        const AiOutput out = ai_step(m, ctx(w, player), 0.1f);
        CHECK(out.next == AiState::Kite);
        CHECK(toward(out.move_dir, m.pos, player) > 0.f);
    }
    {
        World walled = flat();
        fill(walled, 5, 70, -48, 5, 74, 47, blk::STONE);
        Mob m = make(MobType::Thornspitter, home);
        for (int i = 0; i < 30; ++i) {
            const AiOutput out = ai_step(m, ctx(walled, Vector3{10.5f, 70.f, 0.5f}), 0.1f);
            m.state = out.next;
            CHECK(!out.shoot);
        }
    }
}

TEST(test_ai_gloomwalker_fades_at_dawn) {
    {
        World w = flat();
        set_sky(w, BlockPos{0, 70, 0}, 12);
        Mob m = make(MobType::Gloomwalker, Vector3{0.5f, 70.f, 0.5f});
        const AiContext c = ctx(w, Vector3{0.f, 70.f, 40.f}, false, 0.6f);
        for (int i = 0; i < 4; ++i) {  // 1 s
            const AiOutput out = ai_step(m, c, 0.25f);
            m.state = out.next;
            CHECK(out.next == AiState::Fade);
        }
        CHECK_NEAR(m.health, 19.0, 1e-4);
        // Fading wins over chasing a visible player.
        Mob n = make(MobType::Gloomwalker, Vector3{0.5f, 70.f, 0.5f});
        CHECK(ai_step(n, ctx(w, Vector3{3.5f, 70.f, 0.5f}, true, 0.6f), 0.25f).next == AiState::Fade);
    }
    {
        World w = flat();
        set_sky(w, BlockPos{0, 70, 0}, 11);
        Mob m = make(MobType::Gloomwalker, Vector3{0.5f, 70.f, 0.5f});
        const AiOutput out = ai_step(m, ctx(w, Vector3{0.f, 70.f, 40.f}, false, 0.6f), 0.25f);
        CHECK(out.next != AiState::Fade);
        CHECK(m.health == 20.f);
    }
    {
        World w = flat();
        set_sky(w, BlockPos{0, 70, 0}, 12);
        Mob m = make(MobType::Gloomwalker, Vector3{0.5f, 70.f, 0.5f});
        const AiOutput out = ai_step(m, ctx(w, Vector3{0.f, 70.f, 40.f}, false, 0.5f), 0.25f);
        CHECK(out.next != AiState::Fade);
        CHECK(m.health == 20.f);
    }
    {
        // Other hostiles do not fade.
        World w = flat();
        set_sky(w, BlockPos{0, 70, 0}, 15);
        Mob m = make(MobType::CaveCrawler, Vector3{0.5f, 70.f, 0.5f});
        CHECK(ai_step(m, ctx(w, Vector3{0.f, 70.f, 40.f}, false, 1.f), 0.25f).next != AiState::Fade);
    }
}

TEST(test_ai_gloomwalker_avoids_light) {
    // The chase path skips a cell lit to 10 (block light) and uses the detour.
    World w = flat();
    fill(w, -2, 70, -2, 6, 71, 4, blk::STONE);
    fill(w, 0, 70, 0, 4, 71, 0, blk::AIR);
    fill(w, 0, 70, 2, 4, 71, 2, blk::AIR);
    fill(w, 0, 70, 1, 0, 71, 1, blk::AIR);
    fill(w, 4, 70, 1, 4, 71, 1, blk::AIR);
    Chunk* c = w.chunk(chunk_of(2, 70, 0));
    set_block_light(*c, Chunk::index(2, 70 % CHUNK, 0), 10);
    Mob m = make(MobType::Gloomwalker, Vector3{0.5f, 70.f, 0.5f});
    const AiOutput out = ai_step(m, ctx(w, Vector3{4.5f, 70.f, 0.5f}), 0.1f);
    CHECK(out.next == AiState::Chase);
    CHECK(m.path.size() == 9);
    Mob crawler = make(MobType::CaveCrawler, Vector3{0.5f, 70.f, 0.5f});
    ai_step(crawler, ctx(w, Vector3{4.5f, 70.f, 0.5f}), 0.1f);
    CHECK(crawler.path.size() == 5);  // crawlers do not mind light
}

TEST(test_ai_deterministic) {
    const World w = flat();
    Mob a = make(MobType::Hopper, Vector3{0.5f, 70.f, 0.5f}, 1, 777u);
    Mob b = make(MobType::Hopper, Vector3{0.5f, 70.f, 0.5f}, 1, 777u);
    const AiContext c = ctx(w, Vector3{0.f, 70.f, 40.f}, false);
    for (int i = 0; i < 200; ++i) {
        const AiOutput x = ai_step(a, c, 0.05f), y = ai_step(b, c, 0.05f);
        CHECK(x.move_dir.x == y.move_dir.x && x.move_dir.z == y.move_dir.z && x.next == y.next);
        a.state = x.next;
        b.state = y.next;
        // Let them walk the same way.
        a.pos.x += x.move_dir.x * 0.05f;
        a.pos.z += x.move_dir.z * 0.05f;
        b.pos.x += y.move_dir.x * 0.05f;
        b.pos.z += y.move_dir.z * 0.05f;
    }
    CHECK(a.rng == b.rng && a.target.x == b.target.x);
    // A different seed wanders elsewhere.
    Mob d = make(MobType::Hopper, Vector3{0.5f, 70.f, 0.5f}, 1, 778u);
    Mob e = make(MobType::Hopper, Vector3{0.5f, 70.f, 0.5f}, 1, 777u);
    ai_step(d, c, 0.05f);
    ai_step(e, c, 0.05f);
    CHECK(d.target.x != e.target.x || d.target.z != e.target.z);
}

TEST(test_ai_bad_dt) {
    // NaN or negative dt changes no timers; the output is still well formed.
    const World w = flat();
    Mob m = make(MobType::Fluffhorn, Vector3{0.5f, 70.f, 0.5f});
    AiContext hit = ctx(w, Vector3{2.5f, 70.f, 0.5f});
    hit.last_attacker_hit_mob = m.id;
    m.state = ai_step(m, hit, 0.1f).next;
    const float t = m.timer;
    const AiOutput out = ai_step(m, ctx(w, Vector3{2.5f, 70.f, 0.5f}), std::nanf(""));
    CHECK(m.timer == t);
    CHECK(out.next == AiState::Flee);
    CHECK(std::isfinite(out.move_dir.x) && std::isfinite(out.move_dir.z));
    ai_step(m, ctx(w, Vector3{2.5f, 70.f, 0.5f}), -1.f);
    CHECK(m.timer == t);
}

TEST(test_mob_manager_runs_ai) {
    // A Gloomwalker driven by MobManager walks to a visible player and records one attack event
    // per second once in reach.
    const World w = flat();
    MobManager mm;
    const uint32_t id = mm.spawn(MobType::Gloomwalker, Vector3{0.5f, 70.f, 0.5f}, 3u);
    AiContext c = ctx(w, Vector3{8.5f, 70.f, 0.5f});
    for (int i = 0; i < 240; ++i) mm.update(w, c, 1.f / 60.f);  // 4 s
    const Mob* m = mm.find(id);
    CHECK(m != nullptr);
    if (m == nullptr) return;
    CHECK(hdist(m->pos, c.player_pos) <= 1.3f);
    const std::vector<MobEvent> ev = mm.take_events();
    int attacks = 0;
    for (const MobEvent& e : ev)
        if (e.kind == MobEvent::Attack && e.mob == id) ++attacks;
    CHECK(attacks >= 1 && attacks <= 3);
    CHECK(mm.take_events().empty());  // taken once
}

TEST(test_mob_manager_jumps_steps) {
    // A chasing mob climbs a 1-block step on its way.
    World w = flat();
    fill(w, 4, 70, -48, 47, 70, 47, blk::STONE);
    MobManager mm;
    const uint32_t id = mm.spawn(MobType::Gloomwalker, Vector3{0.5f, 70.f, 0.5f}, 3u);
    const AiContext c = ctx(w, Vector3{8.5f, 71.f, 0.5f});
    for (int i = 0; i < 300; ++i) mm.update(w, c, 1.f / 60.f);
    const Mob* m = mm.find(id);
    CHECK(m != nullptr && m->pos.y >= 71.f - 1e-3f && m->pos.x > 4.f);
}

TEST(test_mob_manager_shoot_events) {
    const World w = flat();
    MobManager mm;
    const uint32_t id = mm.spawn(MobType::Thornspitter, Vector3{0.5f, 70.f, 0.5f}, 3u);
    const AiContext c = ctx(w, Vector3{10.5f, 70.f, 0.5f});
    for (int i = 0; i < 60; ++i) mm.update(w, c, 1.f / 60.f);
    int shots = 0;
    for (const MobEvent& e : mm.take_events())
        if (e.kind == MobEvent::Shoot && e.mob == id) ++shots;
    CHECK(shots == 1);
}
