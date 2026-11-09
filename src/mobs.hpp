// Creatures: the definition table, the Mob record, and the MobManager that spawns, moves, removes,
// and ray-picks them. Logic only (raylib for Vector3). Main thread only (Global Constraints).
// Mob type ids are persisted (region v3): append only, never renumber or reuse.
#pragma once

#include "chunk.hpp"  // SavedMob, Column
#include "coords.hpp"
#include "items.hpp"
#include "terrain.hpp"  // Biome

#include "raylib.h"  // Vector3 only

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

class World;

enum class MobType : uint8_t { Tusker, Fluffhorn, Hopper, Gloomwalker, Thornspitter, CaveCrawler, COUNT };

struct LootEntry {
    ItemId item;
    uint8_t min, max;
    float chance;  // 0 < chance <= 1: the entry drops at all with this probability
};

struct MobDef {
    std::string_view name;
    float half_w, height, health, speed, damage, reach;
    bool hostile, night_creature;  // night_creature: the silver sword deals double damage
    std::vector<Biome> biomes;     // where it spawns; empty = any biome (light and depth decide)
    std::vector<LootEntry> loot;
};
// Types outside [0, COUNT) return the Tusker definition.
const MobDef& mob_def(MobType t);

enum class AiState : uint8_t { Idle, Wander, Flee, Charge, Chase, Kite, Fade };

struct Mob {
    uint32_t id;
    MobType type;
    Vector3 pos, vel;  // pos: feet center
    float yaw = 0, health, invuln = 0, timer = 0;
    AiState state = AiState::Idle;
    std::vector<BlockPos> path;
    std::size_t path_i = 0;
    uint32_t rng;  // xorshift32 state, never 0
    // AI bookkeeping (ai.*): the wander target, cooldowns, and the Tusker's one charge.
    Vector3 target{};
    float attack_cd = 0, shoot_cd = 0, repath = 0;
    bool charged = false;
    bool on_ground = false, blocked = false;  // from the last move: standing, stopped sideways
    float far_t = 0;  // seconds a hostile has stayed beyond the despawn linger distance (spawning.*)
    float hurt = 0;   // seconds left of the red hurt tint
    float knock = 0;  // seconds left in which a knockback, not the AI, sets the horizontal velocity
    float walk = 0;   // horizontal distance walked, for the leg swing (mob_models.*)
};

// What the AI sees each update (M6 Task 3 fills the behaviors in ai.*).
struct AiContext {
    Vector3 player_pos;
    bool player_alive;
    float daylight;      // daylight_at(day tick), 0.15..1
    const World* world;  // the world the mobs move in
    std::optional<uint32_t> last_attacker_hit_mob;  // the mob the player hit this frame, if any
    const std::vector<Mob>* mobs = nullptr;  // every mob, for herding; MobManager::update sets it
};

// Something a mob did during MobManager::update, for the game to apply (damage, projectiles).
struct MobEvent {
    enum Kind : uint8_t { Attack, Shoot } kind;
    uint32_t mob;
    Vector3 from;  // the mob's eye
};

constexpr float MOB_GRAVITY = 28.f, MOB_MAX_FALL = 50.f;
constexpr float MOB_STEP = 1.f / 60.f;  // update splits dt into steps of at most this
constexpr float MOB_MAX_DT = 1.f;
constexpr float MOB_JUMP_VEL = 9.f;
constexpr float MOB_INVULN_S = 0.5f, MOB_HURT_S = 0.3f, MOB_KNOCK_S = 0.3f;

// Appends each saved mob to the chunk holding its feet (y clamped to the world) and marks that
// chunk modified, so the column is saved.
void stash_mobs(Column& col, const std::vector<SavedMob>& mobs);

// Some hostile mob's feet are within r (inclusive) of p.
class MobManager;
bool hostiles_within(const MobManager& m, Vector3 p, float r);

class MobManager {
public:
    // Adds a mob with its definition's health, zero velocity, state Idle, and rng = seed (0 is
    // replaced by a fixed non-zero value). Returns its id: ids start at 1, increase, and are never
    // reused, even after remove.
    uint32_t spawn(MobType t, Vector3 pos, uint32_t seed);
    // Zero, negative, NaN, or infinite dt does nothing; dt above MOB_MAX_DT counts as MOB_MAX_DT.
    // dt runs in equal steps of at most MOB_STEP, so dt = 1 equals 60 updates of 1/60. Each step,
    // a mob whose feet column is unloaded is frozen (position kept, vel.y = 0), and a move that
    // would carry the feet into an unloaded column keeps only its vertical part; others run ai_step
    // (ctx.mobs set to all(); ctx.last_attacker_hit_mob only in the first step), take its move_dir
    // as their horizontal velocity, jump (MOB_JUMP_VEL) from the ground when it asks or when they
    // were stopped sideways while walking, fall under MOB_GRAVITY (at most MOB_MAX_FALL down), and
    // move through move_aabb. Attacks and shots are queued as events.
    void update(const World& w, const AiContext& ctx, float dt);
    std::vector<MobEvent> take_events();  // returns and clears the queued events
    void remove(uint32_t id);  // unknown ids do nothing
    // The mob whose box the ray enters nearest to origin, within max_dist (inclusive). An origin
    // inside a box hits it at distance 0. dir need not be normalized; a zero dir or a non-finite
    // max_dist misses.
    std::optional<uint32_t> raycast(Vector3 origin, Vector3 dir, float max_dist) const;
    std::vector<Mob>& all();
    const std::vector<Mob>& all() const;
    Mob* find(uint32_t id);  // nullptr if absent
    // Ignored (false) for an unknown id, a mob still invulnerable, or dmg not positive (or NaN).
    // Otherwise health drops by dmg (not below 0), invuln = MOB_INVULN_S, hurt = MOB_HURT_S, and the
    // mob is knocked back from `from` (apply_knockback) for MOB_KNOCK_S; returns true.
    bool damage(uint32_t id, float dmg, Vector3 from);
    std::vector<Mob> take_dead();  // removes and returns every mob with health <= 0
    // Region v3 persistence. A mob is in column c when its feet cell is.
    std::vector<SavedMob> saved_in(ColumnPos c) const;  // the passive mobs in c, as SavedMob, in id order
    std::vector<SavedMob> take_column(ColumnPos c);     // removes every mob in c; returns saved_in(c)
    // Spawns each saved mob of a passive type with its position, yaw, and health (capped at its
    // max); unknown or hostile types and health <= 0 are skipped. Seeds come from rng.
    void restore(const std::vector<SavedMob>& saved, uint32_t& rng);

private:
    std::vector<Mob> mobs_;
    std::vector<MobEvent> events_;
    uint32_t next_id_ = 1;
};
