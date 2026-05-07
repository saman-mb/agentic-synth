#include <catch2/catch_test_macros.hpp>

#include "engine/WavetableOscillator.h"

#include <cmath>
#include <numbers>
#include <vector>

using namespace agentic_synth::engine;

static double midiToHz(int note) { return 440.0 * std::pow(2.0, (note - 69) / 12.0); }

// Measure frequency via positive-going zero crossings over a rendered buffer.
// Averaging many periods suppresses the ±0.5-sample quantisation error.
static double measureFrequency(float* buf, int numSamples, double sampleRate, double approxHz) {
    const int skip = static_cast<int>(sampleRate / approxHz * 2.0); // skip 2 startup cycles

    int crossings = 0;
    int first = -1, last = -1;
    for (int i = skip + 1; i < numSamples; ++i) {
        if (buf[i - 1] <= 0.0f && buf[i] > 0.0f) {
            if (first < 0) first = i;
            last = i;
            ++crossings;
        }
    }
    if (crossings < 2 || last == first) return 0.0;
    return static_cast<double>(crossings - 1) * sampleRate / static_cast<double>(last - first);
}

TEST_CASE("WavetableOscillator pitch accuracy ±1 cent, MIDI 24..108", "[wavetable][pitch]") {
    constexpr double kSampleRate = 44100.0;
    // 2 s gives ≥65 cycles at MIDI 24 (32.7 Hz); averaging suppresses quantisation error to <0.03 cents.
    constexpr int kBufSize = static_cast<int>(kSampleRate * 2.0);

    WavetableOscillator osc;
    osc.setSampleRate(kSampleRate);

    std::vector<float> buf(kBufSize);

    for (int midi = 24; midi <= 108; ++midi) {
        const double expected = midiToHz(midi);
        osc.setFrequency(expected);
        osc.reset();
        osc.processBlock(buf.data(), kBufSize);

        const double measured = measureFrequency(buf.data(), kBufSize, kSampleRate, expected);
        REQUIRE(measured > 0.0);

        const double cents = std::abs(1200.0 * std::log2(measured / expected));
        INFO("MIDI " << midi << "  expected=" << expected << " Hz  measured=" << measured
                     << " Hz  error=" << cents << " cents");
        REQUIRE(cents < 1.0);
    }
}

TEST_CASE("WavetableOscillator morphs linearly between frames", "[wavetable][morph]") {
    // Frame 0 = DC +1, frame 1 = DC -1; morph 0.5 must give DC 0.
    std::vector<float> frames(2 * kWavetableSize);
    for (int i = 0; i < kWavetableSize; ++i) frames[i] = 1.0f;
    for (int i = 0; i < kWavetableSize; ++i) frames[kWavetableSize + i] = -1.0f;

    WavetableOscillator osc;
    osc.setSampleRate(44100.0);
    osc.setFrequency(1.0); // very slow: stays near phase 0 → reads index 0
    osc.loadFromFrames(frames.data(), 2);

    auto sample = [&](float morph) {
        osc.setMorphPosition(morph);
        osc.reset();
        return osc.processSample();
    };

    REQUIRE(std::abs(sample(0.0f) - 1.0f) < 0.01f);
    REQUIRE(std::abs(sample(0.5f) - 0.0f) < 0.01f);
    REQUIRE(std::abs(sample(1.0f) - (-1.0f)) < 0.01f);
}

TEST_CASE("WavetableOscillator default sine output stays in [-1, 1]", "[wavetable][sine]") {
    WavetableOscillator osc;
    osc.setSampleRate(44100.0);
    osc.setFrequency(440.0);

    for (int i = 0; i < 1024; ++i) {
        const float s = osc.processSample();
        REQUIRE(s >= -1.0f);
        REQUIRE(s <= 1.0f);
    }
}

TEST_CASE("WavetableOscillator reset restarts phase", "[wavetable][reset]") {
    WavetableOscillator osc;
    osc.setSampleRate(44100.0);
    osc.setFrequency(440.0);

    const float first = osc.processSample();
    for (int i = 0; i < 100; ++i) osc.processSample();

    osc.reset();
    const float afterReset = osc.processSample();
    REQUIRE(std::abs(afterReset - first) < 1e-5f);
}
