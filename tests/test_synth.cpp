// Tests for src/synth.* (M8 Task 1): procedural sound synthesis.
#include "synth.hpp"
#include "test.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace {

SfxParams tone(Waveform w, float freq, float a, float s, float d) {
    return SfxParams{w, freq, 0.f, a, s, d, 1.f, 0.f, 1u};
}

double rms(const std::vector<float>& v) {
    double sum = 0;
    for (float x : v) sum += static_cast<double>(x) * x;
    return v.empty() ? 0.0 : std::sqrt(sum / static_cast<double>(v.size()));
}

int sign_changes(const std::vector<float>& v) {
    int n = 0;
    for (std::size_t i = 1; i < v.size(); ++i)
        if ((v[i] >= 0.f) != (v[i - 1] >= 0.f)) ++n;
    return n;
}

}  // namespace

TEST(test_synth_length) {
    // samples == round((attack + sustain + decay) * rate)
    CHECK(synth(tone(Waveform::Square, 440, 0.1f, 0.2f, 0.3f), 44100).size() == 26460);
    CHECK(synth(tone(Waveform::Square, 440, 0.1f, 0.2f, 0.3f), 22050).size() == 13230);
    CHECK(synth(tone(Waveform::Saw, 300, 0.01f, 0.f, 0.05f), 44100).size() == 2646);  // sustain 0
    CHECK(synth(tone(Waveform::Saw, 300, 0.01f, 0.f, 0.05f), 22050).size() == 1323);
    CHECK(synth(tone(Waveform::Noise, 900, 0.0123f, 0.f, 0.f), 44100).size() == 542);  // 542.43 rounds down
    CHECK(synth(tone(Waveform::Noise, 900, 0.0123f, 0.f, 0.f), 22050).size() == 271);  // 271.215
    CHECK(synth(tone(Waveform::Sine, 500, 0.f, 0.f, 0.0001f), 44100).size() == 4);     // 4.41
    CHECK(synth(tone(Waveform::Sine, 500, 0.f, 1.f, 0.f), 44100).size() == 44100);     // default rate too
    CHECK(synth(tone(Waveform::Sine, 500, 0.f, 1.f, 0.f)).size() == 44100);
}

TEST(test_synth_zero_length) {
    CHECK(synth(tone(Waveform::Square, 440, 0.f, 0.f, 0.f), 44100).empty());
    CHECK(synth(tone(Waveform::Noise, 440, 0.f, 0.f, 0.f), 22050).empty());
    CHECK(synth(tone(Waveform::Sine, 440, 0.f, 0.f, 0.000001f), 44100).empty());  // 0.044 samples
}

TEST(test_synth_invalid_input) {
    const float nan = std::numeric_limits<float>::quiet_NaN(), inf = std::numeric_limits<float>::infinity();
    CHECK(synth(tone(Waveform::Square, 440, 0.1f, 0.1f, 0.1f), 0).empty());   // rate 0
    CHECK(synth(tone(Waveform::Square, 440, 0.1f, 0.1f, 0.1f), -8).empty());  // negative rate
    CHECK(synth(tone(Waveform::Square, 440, -1.f, 0.1f, 0.f), 1000).size() == 100);  // negative attack = 0
    CHECK(synth(tone(Waveform::Square, 440, nan, 0.1f, inf), 1000).size() == 100);   // non-finite = 0
    CHECK(synth(tone(Waveform::Square, 440, 0.f, -2.f, -3.f), 1000).empty());

    // Out-of-range volume, mix, and frequencies still give finite samples in [-1, 1].
    for (const SfxParams& p : {SfxParams{Waveform::Square, 440, 0, 0, 0.1f, 0, 5.f, 3.f, 7u},
                               SfxParams{Waveform::Saw, nan, inf, 0, 0.1f, 0, nan, nan, 7u},
                               SfxParams{Waveform::Noise, -inf, -1.0e9f, 0, 0.1f, 0, -1.f, -1.f, 0u},
                               SfxParams{Waveform::Sine, 1.0e9f, 1.0e9f, 0, 0.1f, 0, 1.f, 0.5f, 7u}}) {
        const std::vector<float> v = synth(p, 8000);
        CHECK(v.size() == 800);
        for (float x : v) CHECK(std::isfinite(x) && x >= -1.f && x <= 1.f);
    }
    // Volume 5 is clamped to 1: a full-volume square sustains at exactly +-1.
    for (float x : synth(SfxParams{Waveform::Square, 100, 0, 0, 0.1f, 0, 5.f, 0.f, 1u}, 8000)) CHECK(std::fabs(x) == 1.f);
    // Volume below 0 (or NaN) is silence.
    for (float x : synth(SfxParams{Waveform::Square, 100, 0, 0, 0.1f, 0, -1.f, 0.f, 1u}, 8000)) CHECK(x == 0.f);
}

TEST(test_synth_deterministic) {
    for (int s = 0; s < static_cast<int>(Sfx::Count); ++s) {
        const SfxParams& p = sfx_params(static_cast<Sfx>(s));
        CHECK(synth(p) == synth(p));
    }
    SfxParams a{Waveform::Noise, 2000, -500, 0.01f, 0.05f, 0.05f, 0.8f, 0.f, 1u};
    SfxParams b = a;
    b.seed = 2u;
    CHECK(synth(a) == synth(a));
    CHECK(synth(a) != synth(b));
    // noise_mix is seeded too.
    SfxParams c{Waveform::Sine, 300, 0, 0.f, 0.1f, 0.f, 1.f, 0.5f, 9u};
    SfxParams d = c;
    d.seed = 10u;
    CHECK(synth(c) == synth(c));
    CHECK(synth(c) != synth(d));
}

TEST(test_synth_envelope) {
    // Square at full volume: |sample| is the envelope. Attack 0.1, sustain 0.1, decay 0.1 at 1000 Hz rate.
    const std::vector<float> v = synth(tone(Waveform::Square, 10, 0.1f, 0.1f, 0.1f), 1000);
    CHECK(v.size() == 300);
    CHECK(v[0] == 0.f);                          // starts silent
    CHECK_NEAR(std::fabs(v[50]), 0.5, 1e-4);     // halfway up the attack
    CHECK_NEAR(std::fabs(v[100]), 1.0, 1e-4);    // sustain
    CHECK_NEAR(std::fabs(v[150]), 1.0, 1e-4);
    CHECK_NEAR(std::fabs(v[250]), 0.5, 1e-4);    // halfway down the decay
    CHECK(std::fabs(v[299]) < 0.02f);            // nearly silent at the end
    // No attack: the first sample is already at full level.
    CHECK_NEAR(std::fabs(synth(tone(Waveform::Square, 10, 0.f, 0.1f, 0.f), 1000)[0]), 1.0, 1e-4);
    // Volume scales everything.
    SfxParams q = tone(Waveform::Square, 10, 0.f, 0.1f, 0.f);
    q.volume = 0.25f;
    CHECK_NEAR(std::fabs(synth(q, 1000)[10]), 0.25, 1e-4);
}

TEST(test_synth_waves) {
    // 100 Hz for 1 s: a square or a saw crosses zero twice per period, a sine too.
    CHECK(std::abs(sign_changes(synth(tone(Waveform::Square, 100, 0, 1, 0), 44100)) - 200) <= 2);
    CHECK(std::abs(sign_changes(synth(tone(Waveform::Saw, 100, 0, 1, 0), 44100)) - 200) <= 2);
    CHECK(std::abs(sign_changes(synth(tone(Waveform::Sine, 100, 0, 1, 0), 44100)) - 200) <= 2);
    // Sliding 100 -> 200 Hz over 1 s averages 150 Hz: about 300 crossings.
    SfxParams slide = tone(Waveform::Square, 100, 0, 1, 0);
    slide.freq_slide = 100;
    CHECK(std::abs(sign_changes(synth(slide, 44100)) - 300) <= 3);
    // A slide below 0 Hz stops at 0: 100 Hz falling 400 Hz/s reaches 0 at 0.25 s, averaging 50 Hz on
    // the way (12.5 periods, about 25 crossings), then holds still instead of running backwards.
    SfxParams down = tone(Waveform::Square, 100, 0, 1, 0);
    down.freq_slide = -400;
    CHECK(std::abs(sign_changes(synth(down, 44100)) - 25) <= 2);

    // Shapes at a quarter period (rate 400, 100 Hz: 4 samples per period).
    const std::vector<float> sq = synth(tone(Waveform::Square, 100, 0, 0.01f, 0), 400);
    CHECK(sq[0] == 1.f && sq[1] == 1.f && sq[2] == -1.f && sq[3] == -1.f);
    const std::vector<float> saw = synth(tone(Waveform::Saw, 100, 0, 0.01f, 0), 400);
    CHECK_NEAR(saw[0], -1.0, 1e-5);
    CHECK_NEAR(saw[1], -0.5, 1e-5);
    CHECK_NEAR(saw[2], 0.0, 1e-5);
    CHECK_NEAR(saw[3], 0.5, 1e-5);
    const std::vector<float> sine = synth(tone(Waveform::Sine, 100, 0, 0.01f, 0), 400);
    CHECK_NEAR(sine[0], 0.0, 1e-5);
    CHECK_NEAR(sine[1], 1.0, 1e-5);
    CHECK_NEAR(sine[2], 0.0, 1e-5);
    CHECK_NEAR(sine[3], -1.0, 1e-5);

    // Noise holds one value per period: at 100 Hz and rate 400, samples come in runs of 4.
    const std::vector<float> nz = synth(tone(Waveform::Noise, 100, 0, 0.1f, 0), 400);
    for (std::size_t i = 0; i + 1 < nz.size(); ++i)
        if ((i + 1) % 4 != 0) CHECK(nz[i] == nz[i + 1]);
    int distinct = 0;
    for (std::size_t i = 4; i < nz.size(); i += 4) distinct += nz[i] != nz[i - 4] ? 1 : 0;
    CHECK(distinct >= 8);  // 9 new values, nearly always all different
    CHECK(rms(nz) > 0.3);  // uniform in [-1, 1]: about 0.58

    // noise_mix 1 turns a square into noise; 0 keeps the pure wave.
    SfxParams mixed = tone(Waveform::Square, 100, 0, 0.1f, 0);
    mixed.noise_mix = 1.f;
    int not_unit = 0;
    for (float x : synth(mixed, 8000)) not_unit += std::fabs(x) != 1.f ? 1 : 0;
    CHECK(not_unit > 700);
    SfxParams half = mixed;
    half.noise_mix = 0.5f;
    for (float x : synth(half, 8000)) CHECK(std::fabs(x) >= 0.f && std::fabs(x) <= 1.f);
    CHECK(synth(half, 8000) != synth(mixed, 8000));
}

TEST(test_synth_no_clipping) {
    for (int s = 0; s < static_cast<int>(Sfx::Count); ++s)
        for (int rate : {44100, 22050})
            for (float x : synth(sfx_params(static_cast<Sfx>(s)), rate)) CHECK(!std::isnan(x) && x >= -1.f && x <= 1.f);
}

TEST(test_all_sfx_defined) {
    CHECK(static_cast<int>(Sfx::Count) == 18);
    for (int s = 0; s < static_cast<int>(Sfx::Count); ++s) {
        const SfxParams& p = sfx_params(static_cast<Sfx>(s));
        const double len = static_cast<double>(p.attack) + p.sustain + p.decay;
        CHECK(len > 0.0 && len <= 2.0);
        CHECK(rms(synth(p)) > 0.01);
    }
    // Out of range falls back to Click.
    const SfxParams& click = sfx_params(Sfx::Click);
    const SfxParams& count = sfx_params(Sfx::Count);
    CHECK(&count == &click);
    CHECK(&sfx_params(static_cast<Sfx>(200)) == &click);
    // Every sound is different from every other.
    for (int a = 0; a < static_cast<int>(Sfx::Count); ++a)
        for (int b = a + 1; b < static_cast<int>(Sfx::Count); ++b)
            CHECK(synth(sfx_params(static_cast<Sfx>(a)), 8000) != synth(sfx_params(static_cast<Sfx>(b)), 8000));
}
