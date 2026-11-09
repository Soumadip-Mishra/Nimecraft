#include "synth.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace {

constexpr double TWO_PI = 6.283185307179586;

double time_or_zero(float t) {
    return std::isfinite(t) && t > 0.f ? static_cast<double>(t) : 0.0;
}

double finite_or_zero(float v) {
    return std::isfinite(v) ? static_cast<double>(v) : 0.0;
}

double unit_or_zero(float v) {
    return std::isfinite(v) ? std::clamp(static_cast<double>(v), 0.0, 1.0) : 0.0;
}

// xorshift32: a uniform value in [-1, 1].
struct Noise {
    uint32_t s;
    explicit Noise(uint32_t seed) : s(seed != 0 ? seed : 0x9E3779B9u) {}
    double next() {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        return static_cast<double>(s) / 2147483647.5 - 1.0;
    }
};

}  // namespace

std::vector<float> synth(const SfxParams& p, int sample_rate) {
    std::vector<float> out;
    if (sample_rate <= 0) return out;
    const double attack = time_or_zero(p.attack), sustain = time_or_zero(p.sustain), decay = time_or_zero(p.decay);
    const double rate = static_cast<double>(sample_rate);
    const long n = std::lround((attack + sustain + decay) * rate);
    if (n <= 0) return out;
    out.resize(static_cast<std::size_t>(n));

    const double base = finite_or_zero(p.base_freq), slide = finite_or_zero(p.freq_slide);
    const double volume = unit_or_zero(p.volume), mix = unit_or_zero(p.noise_mix);
    Noise noise(p.seed);
    double held = noise.next();  // Noise: one value per period
    double phase = 0.0;
    for (long i = 0; i < n; ++i) {
        const double t = static_cast<double>(i) / rate;
        double env = 1.0;
        if (t < attack) env = t / attack;
        else if (t >= attack + sustain && decay > 0.0) env = std::max(0.0, 1.0 - (t - attack - sustain) / decay);

        double wave = 0.0;
        switch (p.wave) {
        case Waveform::Square: wave = phase < 0.5 ? 1.0 : -1.0; break;
        case Waveform::Saw: wave = 2.0 * phase - 1.0; break;
        case Waveform::Sine: wave = std::sin(TWO_PI * phase); break;
        case Waveform::Noise: wave = held; break;
        }
        if (mix > 0.0) wave = wave * (1.0 - mix) + noise.next() * mix;
        out[static_cast<std::size_t>(i)] = static_cast<float>(std::clamp(wave * env * volume, -1.0, 1.0));

        const double freq = std::clamp(base + slide * t, 0.0, rate);  // above the rate is aliasing anyway
        phase += freq / rate;
        if (phase >= 1.0) {
            phase -= std::floor(phase);
            held = noise.next();
        }
    }
    return out;
}

namespace {

// Lengths stay well under 2 s; volumes leave headroom for several sounds at once.
const std::array<SfxParams, static_cast<std::size_t>(Sfx::Count)> SFX = {{
    //  wave         freq   slide   attack  sustain decay  volume mix   seed
    {Waveform::Noise, 1200.f, -2000.f, 0.005f, 0.020f, 0.080f, 0.35f, 0.0f, 11u},   // StepGrass: soft rustle
    {Waveform::Noise, 2200.f, -4000.f, 0.002f, 0.015f, 0.060f, 0.40f, 0.0f, 12u},   // StepStone: hard tick
    {Waveform::Noise, 3000.f, -1000.f, 0.010f, 0.030f, 0.080f, 0.30f, 0.0f, 13u},   // StepSand: hiss
    {Waveform::Square, 180.f, -300.f, 0.002f, 0.020f, 0.070f, 0.30f, 0.4f, 14u},    // StepWood: hollow knock
    {Waveform::Noise, 900.f, -500.f, 0.010f, 0.040f, 0.100f, 0.30f, 0.0f, 15u},     // StepSnow: muffled crunch
    {Waveform::Noise, 600.f, -800.f, 0.005f, 0.050f, 0.120f, 0.45f, 0.0f, 16u},     // Dig
    {Waveform::Square, 220.f, -400.f, 0.002f, 0.030f, 0.080f, 0.35f, 0.3f, 17u},    // Place
    {Waveform::Noise, 400.f, -600.f, 0.003f, 0.080f, 0.200f, 0.50f, 0.0f, 18u},     // Break
    {Waveform::Saw, 320.f, -600.f, 0.005f, 0.080f, 0.150f, 0.45f, 0.2f, 19u},       // Hurt
    {Waveform::Noise, 1500.f, -2000.f, 0.010f, 0.050f, 0.100f, 0.35f, 0.0f, 20u},   // Eat
    {Waveform::Noise, 2500.f, -3000.f, 0.020f, 0.100f, 0.300f, 0.40f, 0.0f, 21u},   // Splash
    {Waveform::Square, 900.f, 0.f, 0.001f, 0.015f, 0.020f, 0.30f, 0.0f, 22u},       // Click
    {Waveform::Saw, 110.f, -60.f, 0.020f, 0.150f, 0.200f, 0.45f, 0.25f, 23u},       // TuskerGrunt
    {Waveform::Square, 420.f, -80.f, 0.030f, 0.250f, 0.200f, 0.35f, 0.1f, 24u},     // FluffhornBleat
    {Waveform::Sine, 1400.f, 1200.f, 0.010f, 0.060f, 0.060f, 0.45f, 0.0f, 25u},     // HopperSqueak
    {Waveform::Saw, 90.f, -30.f, 0.200f, 0.500f, 0.600f, 0.40f, 0.15f, 26u},        // GloomMoan
    {Waveform::Noise, 3500.f, -6000.f, 0.002f, 0.030f, 0.100f, 0.40f, 0.0f, 27u},   // ThornShoot
    {Waveform::Noise, 5000.f, -1000.f, 0.050f, 0.200f, 0.300f, 0.30f, 0.0f, 28u},   // CrawlerHiss
}};

}  // namespace

const SfxParams& sfx_params(Sfx s) {
    const auto i = static_cast<std::size_t>(s);
    return i < SFX.size() ? SFX[i] : SFX[static_cast<std::size_t>(Sfx::Click)];
}
