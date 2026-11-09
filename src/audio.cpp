#include "audio.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>

namespace {

bool finite3(Vector3 v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

uint32_t xorshift(uint32_t& s) {
    if (s == 0) s = 0x9E3779B9u;
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s;
}

}  // namespace

Spatial spatialize(Vector3 listener, float listener_yaw, Vector3 source, float max_dist) {
    const float dx = source.x - listener.x, dy = source.y - listener.y, dz = source.z - listener.z;
    const bool same = finite3(listener) && finite3(source) && dx == 0.f && dy == 0.f && dz == 0.f;
    if (same) return Spatial{1.f, 0.f};
    if (!finite3(listener) || !finite3(source) || !std::isfinite(listener_yaw) || !std::isfinite(max_dist) ||
        max_dist <= 0.f)
        return Spatial{0.f, 0.f};
    const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
    const float volume = std::clamp(1.f - dist / max_dist, 0.f, 1.f);
    const float h = std::sqrt(dx * dx + dz * dz);
    float pan = 0.f;
    if (h > 1.0e-6f) {
        // Right of the facing (cos yaw, 0, sin yaw) is (-sin yaw, 0, cos yaw), as player.cpp walks.
        pan = std::clamp((-dx * std::sin(listener_yaw) + dz * std::cos(listener_yaw)) / h, -1.f, 1.f);
    }
    return Spatial{volume, pan};
}

bool SfxLimiter::allow(Sfx s, double now) {
    const auto i = static_cast<std::size_t>(s);
    if (i >= last.size() || !std::isfinite(now)) return false;
    if (now - last[i] < SFX_COOLDOWN_S) return false;
    last[i] = now;
    return true;
}

float footstep_interval(bool sprinting) {
    return sprinting ? STEP_SPRINT_M : STEP_WALK_M;
}

std::optional<Sfx> step_sfx(BlockId below) {
    switch (below) {
    case blk::GRASS:
    case blk::DIRT:
    case blk::CLAY:
    case blk::LEAVES:
    case blk::PINE_LEAVES: return Sfx::StepGrass;
    case blk::SAND:
    case blk::GRAVEL: return Sfx::StepSand;
    case blk::LOG:
    case blk::PINE_LOG:
    case blk::PLANKS:
    case blk::WORKBENCH:
    case blk::CRATE: return Sfx::StepWood;
    case blk::SNOW: return Sfx::StepSnow;
    default: break;
    }
    if (below >= blk::COUNT || !block_def(below).solid) return std::nullopt;
    return Sfx::StepStone;
}

bool Footsteps::step(float moved_m, bool sprinting, bool on_ground) {
    if (!on_ground || !std::isfinite(moved_m) || moved_m <= 0.f) return false;
    walked += moved_m;
    const float interval = footstep_interval(sprinting);
    if (walked < interval) return false;
    walked = std::fmod(walked, interval);
    return true;
}

Sfx mob_call(MobType t) {
    switch (t) {
    case MobType::Tusker: return Sfx::TuskerGrunt;
    case MobType::Fluffhorn: return Sfx::FluffhornBleat;
    case MobType::Hopper: return Sfx::HopperSqueak;
    case MobType::Gloomwalker: return Sfx::GloomMoan;
    case MobType::Thornspitter: return Sfx::ThornShoot;
    case MobType::CaveCrawler: return Sfx::CrawlerHiss;
    default: return Sfx::Click;
    }
}

double next_music_delay(uint32_t& rng) {
    const double u = static_cast<double>(xorshift(rng)) / 4294967295.0;  // 0..1
    return MUSIC_GAP_MIN_S + (MUSIC_GAP_MAX_S - MUSIC_GAP_MIN_S) * u;
}

int pick_track(uint32_t& rng, int count) {
    if (count <= 0) return -1;
    return static_cast<int>(xorshift(rng) % static_cast<uint32_t>(count));
}

std::vector<std::filesystem::path> find_music(const std::filesystem::path& dir) {
    std::vector<std::filesystem::path> out;
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec)) return out;
    for (std::filesystem::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code fec;
        if (!it->is_regular_file(fec)) continue;
        // u8string never throws on a name the Windows code page cannot show (string() can).
        std::u8string ext = it->path().extension().u8string();
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](char8_t c) { return c >= u8'A' && c <= u8'Z' ? static_cast<char8_t>(c - u8'A' + u8'a') : c; });
        if (ext == u8".ogg") out.push_back(it->path());
    }
    std::sort(out.begin(), out.end());
    return out;
}

// ---- Device half: raylib audio. ----

Audio::~Audio() {
    shutdown();
}

void Audio::init() {
    if (ready_) return;
    InitAudioDevice();
    if (!IsAudioDeviceReady()) return;  // no device: the game runs silent
    ready_ = true;
    for (int s = 0; s < static_cast<int>(Sfx::Count); ++s) {
        std::vector<float> samples = synth(sfx_params(static_cast<Sfx>(s)));
        Wave w{};
        w.frameCount = static_cast<unsigned int>(samples.size());
        w.sampleRate = 44100;
        w.sampleSize = 32;
        w.channels = 1;
        w.data = samples.data();  // LoadSoundFromWave copies the samples into its own buffer
        auto& voices = sounds_[static_cast<std::size_t>(s)];
        voices[0] = LoadSoundFromWave(w);
        for (int v = 1; v < VOICES; ++v) voices[static_cast<std::size_t>(v)] = LoadSoundAlias(voices[0]);
    }
    const std::filesystem::path app = std::filesystem::path(
        std::u8string(reinterpret_cast<const char8_t*>(GetApplicationDirectory())));
    tracks_ = find_music(app / "assets" / "music");
    music_wait_ = next_music_delay(music_rng_);
    SetMasterVolume(master_);
}

void Audio::set_listener(Vector3 pos, float yaw) {
    listener_ = pos;
    listener_yaw_ = yaw;
}

void Audio::play(Sfx s, Vector3 at) {
    if (!ready_ || !limiter_.allow(s, clock_)) return;
    const Spatial sp = spatialize(listener_, listener_yaw_, at);
    if (sp.volume <= 0.f || sfx_ <= 0.f) return;
    const auto i = static_cast<std::size_t>(s);
    Sound& snd = sounds_[i][static_cast<std::size_t>(next_voice_[i])];
    next_voice_[i] = (next_voice_[i] + 1) % VOICES;
    SetSoundVolume(snd, sp.volume * sfx_);
    SetSoundPan(snd, sp.pan);
    PlaySound(snd);
}

void Audio::play_ui(Sfx s) {
    if (!ready_ || !limiter_.allow(s, clock_) || sfx_ <= 0.f) return;
    const auto i = static_cast<std::size_t>(s);
    Sound& snd = sounds_[i][static_cast<std::size_t>(next_voice_[i])];
    next_voice_[i] = (next_voice_[i] + 1) % VOICES;
    SetSoundVolume(snd, sfx_);
    SetSoundPan(snd, 0.f);
    PlaySound(snd);
}

void Audio::start_track() {
    const int t = pick_track(music_rng_, static_cast<int>(tracks_.size()));
    if (t < 0) return;
    std::ifstream in(tracks_[static_cast<std::size_t>(t)], std::ios::binary);
    track_bytes_.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    if (!in.good() && !in.eof()) track_bytes_.clear();
    if (track_bytes_.empty()) return;
    music_stream_ = LoadMusicStreamFromMemory(".ogg", track_bytes_.data(), static_cast<int>(track_bytes_.size()));
    if (!IsMusicValid(music_stream_)) {
        track_bytes_.clear();
        return;
    }
    music_stream_.looping = false;
    SetMusicVolume(music_stream_, music_);
    PlayMusicStream(music_stream_);
    music_playing_ = true;
}

void Audio::stop_track() {
    if (!music_playing_) return;
    StopMusicStream(music_stream_);
    UnloadMusicStream(music_stream_);
    music_stream_ = Music{};
    track_bytes_.clear();
    music_playing_ = false;
}

void Audio::update(float dt) {
    if (std::isfinite(dt) && dt > 0.f) clock_ += dt;
    if (!ready_) return;
    if (music_playing_) {
        UpdateMusicStream(music_stream_);
        if (!IsMusicStreamPlaying(music_stream_)) {  // the track ended
            stop_track();
            music_wait_ = next_music_delay(music_rng_);
        }
        return;
    }
    if (tracks_.empty()) return;  // no music folder: silently skipped
    if (std::isfinite(dt) && dt > 0.f) music_wait_ -= dt;
    if (music_wait_ > 0.0) return;
    start_track();
    if (!music_playing_) music_wait_ = next_music_delay(music_rng_);  // unreadable file: try later
}

void Audio::set_volumes(float master, float sfx, float music) {
    const auto unit = [](float v) { return std::isfinite(v) ? std::clamp(v, 0.f, 1.f) : 0.f; };
    master_ = unit(master);
    sfx_ = unit(sfx);
    music_ = unit(music);
    if (!ready_) return;
    SetMasterVolume(master_);
    if (music_playing_) SetMusicVolume(music_stream_, music_);
}

void Audio::shutdown() {
    if (!ready_) return;
    stop_track();
    for (auto& voices : sounds_) {
        for (int v = VOICES - 1; v >= 1; --v) UnloadSoundAlias(voices[static_cast<std::size_t>(v)]);
        UnloadSound(voices[0]);
    }
    sounds_ = {};
    CloseAudioDevice();
    ready_ = false;
}
