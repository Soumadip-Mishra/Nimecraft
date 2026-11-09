// Sound playback: the synthesized effects as raylib Sounds, positional volume and pan, a per-sound
// cooldown, footsteps, creature calls, and optional music from assets/music/*.ogg next to the
// executable. The free functions and small structs are window-free rules (tests/test_audio_logic.cpp);
// the Audio class is the device half (audio device, sounds, music streams), covered by --smoke and
// the Manual checks.
#pragma once

#include "block.hpp"
#include "mobs.hpp"
#include "synth.hpp"

#include "raylib.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

// Volume 1 at the listener falling linearly to 0 at max_dist (3D distance; 0 beyond it), and pan
// -1 (left) .. 1 (right) from the horizontal angle between the listener's facing, (cos yaw, 0,
// sin yaw) as player_look, and the source: 0 straight ahead, behind, or directly above. Non-finite
// input or max_dist <= 0 gives volume 1 at the listener's exact position and 0 elsewhere, pan 0.
struct Spatial {
    float volume, pan;
};
Spatial spatialize(Vector3 listener, float listener_yaw, Vector3 source, float max_dist = 16);

constexpr double SFX_COOLDOWN_S = 0.08;

// Each Sfx plays at most once per SFX_COOLDOWN_S: allow() is true when the last allowed play of the
// same sound was at least that long ago (a denied call does not restart the wait). Count or any
// out-of-range value, and a non-finite `now`, are denied.
struct SfxLimiter {
    std::array<double, static_cast<std::size_t>(Sfx::Count)> last;
    SfxLimiter() { last.fill(-1.0e9); }
    bool allow(Sfx s, double now);
};

constexpr float STEP_WALK_M = 0.45f, STEP_SPRINT_M = 0.35f;
float footstep_interval(bool sprinting);  // STEP_SPRINT_M or STEP_WALK_M

// The footstep sound of walking on block `below`: grass, dirt, clay, and leaves rustle; sand and
// gravel hiss; wood blocks knock; snow crunches; every other solid block (stone, ores, ice, glass)
// clicks like stone. None for AIR, WATER, and non-solid blocks (plants, torches) or unknown ids.
std::optional<Sfx> step_sfx(BlockId below);

// Distance walked on the ground since the last footstep. step() adds `moved_m` while on_ground and
// returns true once the total reaches footstep_interval(sprinting), keeping the remainder below one
// interval (a long frame plays one step, not many). Off the ground the walk pauses; zero, negative,
// or non-finite distances add nothing.
struct Footsteps {
    float walked = 0.f;
    bool step(float moved_m, bool sprinting, bool on_ground);
};

// The call each creature makes: Tusker grunt, Fluffhorn bleat, Hopper squeak, Gloomwalker moan,
// Thornspitter shot sound, Cave Crawler hiss. COUNT or unknown: Click.
Sfx mob_call(MobType t);

// Music: the seconds to wait after a track (or the start) before the next one, uniform in
// MUSIC_GAP_MIN_S..MUSIC_GAP_MAX_S from an xorshift32 state (0 is treated as a fixed non-zero seed),
// and a random track index in 0..count-1 (-1 when count <= 0).
constexpr double MUSIC_GAP_MIN_S = 300.0, MUSIC_GAP_MAX_S = 600.0;
double next_music_delay(uint32_t& rng);
int pick_track(uint32_t& rng, int count);

// The .ogg files (any case of the extension) directly inside dir, sorted by path; empty when dir
// is missing or not a folder. Never throws.
std::vector<std::filesystem::path> find_music(const std::filesystem::path& dir);

class Audio {
public:
    Audio() = default;
    ~Audio();
    Audio(const Audio&) = delete;
    Audio& operator=(const Audio&) = delete;

    // Opens the audio device and builds every Sfx; without a device every call below does nothing.
    // Music comes from GetApplicationDirectory()/assets/music.
    void init();
    void set_listener(Vector3 pos, float yaw);
    void play(Sfx s, Vector3 at);  // positional, within 16 blocks of the listener
    void play_ui(Sfx s);           // centered, full volume
    void update(float dt);         // music timing and streaming
    void set_volumes(float master, float sfx, float music);  // each clamped to 0..1
    void shutdown();

private:
    static constexpr int VOICES = 4;  // copies of each sound, so overlapping plays do not cut off

    void start_track();
    void stop_track();

    bool ready_ = false;
    std::array<std::array<Sound, VOICES>, static_cast<std::size_t>(Sfx::Count)> sounds_{};
    std::array<int, static_cast<std::size_t>(Sfx::Count)> next_voice_{};
    SfxLimiter limiter_;
    double clock_ = 0.0;
    Vector3 listener_{};
    float listener_yaw_ = 0.f;
    float master_ = 0.8f, sfx_ = 1.f, music_ = 0.5f;

    std::vector<std::filesystem::path> tracks_;
    std::vector<unsigned char> track_bytes_;  // the playing file; the stream reads from it
    Music music_stream_{};
    bool music_playing_ = false;
    double music_wait_ = 0.0;
    uint32_t music_rng_ = 0x6C8E9CF5u;
};
