// Procedural sound effects: a small oscillator + envelope synthesizer and the parameters of every
// game sound. Logic only (plain float samples); audio.* wraps the samples in raylib Waves.
#pragma once

#include <cstdint>
#include <vector>

// The plan calls this `Wave`; raylib.h already declares `struct Wave`, so it is Waveform here.
enum class Waveform : uint8_t { Square, Saw, Sine, Noise };

// One sound: a wave at base_freq Hz sliding by freq_slide Hz per second (the frequency never goes
// below 0), shaped by a linear envelope that rises over `attack` seconds, holds for `sustain`, and
// falls to 0 over `decay`; noise_mix (0..1) blends per-sample white noise into the wave, and the
// result is scaled by volume (0..1). Noise holds one random value per period of the frequency.
// `seed` drives the noise, so the same parameters always give the same samples.
struct SfxParams {
    Waveform wave;
    float base_freq, freq_slide, attack, sustain, decay, volume, noise_mix;
    uint32_t seed;
};

// Mono samples in [-1, 1]: round((attack + sustain + decay) * sample_rate) of them. Negative or
// non-finite times count as 0, volume and noise_mix are clamped to 0..1 (non-finite: 0), a
// non-finite frequency or slide counts as 0, and a sample_rate <= 0 gives no samples.
std::vector<float> synth(const SfxParams& p, int sample_rate = 44100);

enum class Sfx : uint8_t {
    StepGrass, StepStone, StepSand, StepWood, StepSnow, Dig, Place, Break, Hurt, Eat, Splash, Click,
    TuskerGrunt, FluffhornBleat, HopperSqueak, GloomMoan, ThornShoot, CrawlerHiss, Count
};

// The parameters of a game sound; every Sfx below Count is 0..2 s long and audible. Count (or any
// out-of-range value) gives the Click sound.
const SfxParams& sfx_params(Sfx s);
