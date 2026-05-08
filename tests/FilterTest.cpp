#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "engine/Filter.h"

#include <cmath>
#include <limits>

using namespace agentic_synth::engine;

// Drives a sine at `freq` Hz through the filter for `cycles` full periods and
// returns the peak output amplitude measured over the final cycle.
static float measureGain(Filter& filter, float freq, float sampleRate, int cycles = 200) {
    const float omega = 2.0f * static_cast<float>(M_PI) * freq / sampleRate;
    const int totalSamples = static_cast<int>(sampleRate / freq * cycles);
    const int measureStart = static_cast<int>(sampleRate / freq * (cycles - 1));
    float peak = 0.0f;
    for (int i = 0; i < totalSamples; ++i) {
        float out = filter.process(std::sin(omega * static_cast<float>(i)));
        if (i >= measureStart)
            peak = std::max(peak, std::abs(out));
    }
    return peak;
}

// ---------------------------------------------------------------------------
// MoogLadder tests
// ---------------------------------------------------------------------------

TEST_CASE("MoogLadder — passband gain near unity at f << cutoff", "[filter][moog]") {
    MoogLadder f;
    f.prepare(44100.0);
    f.setCutoff(2000.0f);
    f.setResonance(0.0f);
    float gain = measureGain(f, 100.0f, 44100.0f);
    // Passband of a LP should be very close to 1.0 when driving at 1/20 of cutoff
    REQUIRE_THAT(gain, Catch::Matchers::WithinAbs(1.0f, 0.02f));
}

TEST_CASE("MoogLadder — stopband attenuation at f >> cutoff", "[filter][moog]") {
    MoogLadder f;
    f.prepare(44100.0);
    f.setCutoff(200.0f);
    f.setResonance(0.0f);
    // Drive at 10× cutoff → 24 dB/oct → gain ≈ 1/(10^4) ~ −80 dB; expect < 0.001
    float gain = measureGain(f, 2000.0f, 44100.0f);
    REQUIRE(gain < 0.001f);
}

TEST_CASE("MoogLadder — resonance clamp prevents runaway gain", "[filter][moog][resonance]") {
    MoogLadder f;
    f.prepare(44100.0);
    f.setCutoff(1000.0f);
    f.setResonance(1.0f); // maximum resonance, should not self-oscillate
    const float sampleRate = 44100.0f;
    const float omega = 2.0f * static_cast<float>(M_PI) * 1000.0f / sampleRate;
    float maxOut = 0.0f;
    for (int i = 0; i < 44100; ++i) {
        float out = f.process(std::sin(omega * static_cast<float>(i)));
        REQUIRE(std::isfinite(out));
        maxOut = std::max(maxOut, std::abs(out));
    }
    // Gain must not diverge — bounded by a reasonable headroom factor
    REQUIRE(maxOut < 100.0f);
}

TEST_CASE("MoogLadder — reset clears state", "[filter][moog]") {
    MoogLadder f;
    f.prepare(44100.0);
    f.setCutoff(500.0f);
    f.setResonance(0.5f);
    for (int i = 0; i < 1000; ++i)
        f.process(1.0f);
    f.reset();
    float out = f.process(0.0f);
    REQUIRE_THAT(out, Catch::Matchers::WithinAbs(0.0f, 1e-6f));
}

// ---------------------------------------------------------------------------
// SVFilter tests
// ---------------------------------------------------------------------------

TEST_CASE("SVFilter LP — passband gain near unity at f << cutoff", "[filter][svf]") {
    SVFilter f(FilterMode::LP);
    f.prepare(44100.0);
    f.setCutoff(2000.0f);
    f.setResonance(0.0f);
    float gain = measureGain(f, 100.0f, 44100.0f);
    REQUIRE_THAT(gain, Catch::Matchers::WithinAbs(1.0f, 0.05f));
}

TEST_CASE("SVFilter LP — stopband attenuation at f >> cutoff", "[filter][svf]") {
    SVFilter f(FilterMode::LP);
    f.prepare(44100.0);
    f.setCutoff(200.0f);
    f.setResonance(0.0f);
    // 12 dB/oct at 10× cutoff → ~−40 dB; gain < 0.015
    float gain = measureGain(f, 2000.0f, 44100.0f);
    REQUIRE(gain < 0.015f);
}

TEST_CASE("SVFilter HP — passband gain near unity at f >> cutoff", "[filter][svf]") {
    SVFilter f(FilterMode::HP);
    f.prepare(44100.0);
    f.setCutoff(200.0f);
    f.setResonance(0.0f);
    float gain = measureGain(f, 4000.0f, 44100.0f);
    REQUIRE_THAT(gain, Catch::Matchers::WithinAbs(1.0f, 0.05f));
}

TEST_CASE("SVFilter HP — stopband attenuation at f << cutoff", "[filter][svf]") {
    SVFilter f(FilterMode::HP);
    f.prepare(44100.0);
    f.setCutoff(2000.0f);
    f.setResonance(0.0f);
    float gain = measureGain(f, 100.0f, 44100.0f);
    REQUIRE(gain < 0.015f);
}

TEST_CASE("SVFilter BP — peak near cutoff, attenuates far bands", "[filter][svf]") {
    SVFilter f(FilterMode::BP);
    f.prepare(44100.0);
    f.setCutoff(1000.0f);
    f.setResonance(0.5f); // Q ≈ 1.0 → selective enough for ratio tests
    float gainAtCutoff = measureGain(f, 1000.0f, 44100.0f);
    float gainFarBelow = measureGain(f, 50.0f, 44100.0f);
    float gainFarAbove = measureGain(f, 8000.0f, 44100.0f);
    REQUIRE(gainAtCutoff > gainFarBelow * 3.0f);
    REQUIRE(gainAtCutoff > gainFarAbove * 3.0f);
}

TEST_CASE("SVFilter Notch — null near cutoff, passes far bands", "[filter][svf]") {
    SVFilter f(FilterMode::Notch);
    f.prepare(44100.0);
    f.setCutoff(1000.0f);
    f.setResonance(0.5f);
    float gainAtCutoff = measureGain(f, 1000.0f, 44100.0f);
    float gainFarBelow = measureGain(f, 50.0f, 44100.0f);
    REQUIRE(gainAtCutoff < gainFarBelow * 0.5f);
}

TEST_CASE("SVFilter — resonance clamp prevents self-oscillation", "[filter][svf][resonance]") {
    SVFilter f(FilterMode::LP);
    f.prepare(44100.0);
    f.setCutoff(1000.0f);
    f.setResonance(1.0f); // maximum resonance
    const float sampleRate = 44100.0f;
    const float omega = 2.0f * static_cast<float>(M_PI) * 1000.0f / sampleRate;
    float maxOut = 0.0f;
    for (int i = 0; i < 44100; ++i) {
        float out = f.process(std::sin(omega * static_cast<float>(i)));
        REQUIRE(std::isfinite(out));
        maxOut = std::max(maxOut, std::abs(out));
    }
    REQUIRE(maxOut < 100.0f);
}

TEST_CASE("SVFilter — reset clears state", "[filter][svf]") {
    SVFilter f(FilterMode::LP);
    f.prepare(44100.0);
    f.setCutoff(500.0f);
    f.setResonance(0.5f);
    for (int i = 0; i < 1000; ++i)
        f.process(1.0f);
    f.reset();
    float out = f.process(0.0f);
    REQUIRE_THAT(out, Catch::Matchers::WithinAbs(0.0f, 1e-6f));
}

TEST_CASE("SVFilter — mode switching at runtime", "[filter][svf]") {
    SVFilter f(FilterMode::LP);
    f.prepare(44100.0);
    f.setCutoff(1000.0f);
    f.setResonance(0.0f);
    float lpGain = measureGain(f, 100.0f, 44100.0f);

    f.reset();
    f.setMode(FilterMode::HP);
    float hpGain = measureGain(f, 100.0f, 44100.0f);

    // LP passes low freq, HP attenuates it
    REQUIRE(lpGain > hpGain * 10.0f);
}
