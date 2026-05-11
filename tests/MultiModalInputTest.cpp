#include "engine/MultiModalInput.h"
#include "engine/PatchStruct.h"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <vector>

using namespace agentic_synth::engine;
using Catch::Approx;

TEST_CASE("MultiModalInput: analyzeHum with silence returns bounded profile", "[multimodal]") {
    MultiModalInput input;
    std::vector<float> silence(1024, 0.0f);
    auto profile = input.analyzeHum(silence.data(), static_cast<int>(silence.size()), 44100);

    CHECK(profile.centroidHz >= 0.0f);
    CHECK(profile.brightness >= 0.0f);
    CHECK(profile.brightness <= 1.0f);
    CHECK(profile.roughness >= 0.0f);
    CHECK(profile.roughness <= 1.0f);
}

TEST_CASE("MultiModalInput: analyzeHum bright sine raises brightness", "[multimodal]") {
    MultiModalInput input;
    constexpr int kN = 1024;
    constexpr int kSR = 44100;
    // High-frequency sine → high spectral centroid → high brightness
    std::vector<float> highFreq(kN);
    for (int i = 0; i < kN; ++i)
        highFreq[i] = std::sin(2.0f * 3.14159f * 8000.0f * i / kSR);
    auto bright = input.analyzeHum(highFreq.data(), kN, kSR);

    std::vector<float> lowFreq(kN);
    for (int i = 0; i < kN; ++i)
        lowFreq[i] = std::sin(2.0f * 3.14159f * 100.0f * i / kSR);
    auto dark = input.analyzeHum(lowFreq.data(), kN, kSR);

    CHECK(bright.brightness > dark.brightness);
}

TEST_CASE("MultiModalInput: spectralToPatch bright profile sets high cutoff", "[multimodal]") {
    MultiModalInput input;
    MultiModalInput::SpectralProfile bright{};
    bright.brightness = 0.8f;
    bright.roughness = 0.1f;

    PatchStruct patch = input.spectralToPatch(bright);
    CHECK(patch.filter.cutoff_hz > 1000.0f);
    CHECK(patch.filter.resonance >= 0.0f);
    CHECK(patch.filter.resonance <= 1.0f);
    CHECK(patch.amp_env.attack_s > 0.0f);
    CHECK(patch.amp_env.release_s > 0.0f);
}

TEST_CASE("MultiModalInput: spectralToPatch dark profile sets low cutoff", "[multimodal]") {
    MultiModalInput input;
    MultiModalInput::SpectralProfile dark{};
    dark.brightness = 0.1f;
    dark.roughness = 0.2f;

    PatchStruct patch = input.spectralToPatch(dark);
    CHECK(patch.filter.cutoff_hz <= 1200.0f);
}

TEST_CASE("MultiModalInput: spectralToPatch uses valid osc indices", "[multimodal]") {
    MultiModalInput input;
    // All three brightness ranges should write only to osc[0..2]
    for (float brightness : {0.1f, 0.5f, 0.8f}) {
        MultiModalInput::SpectralProfile p{};
        p.brightness = brightness;
        p.roughness = 0.0f;
        PatchStruct patch = input.spectralToPatch(p);
        // All osc volumes should be in [0,1]
        for (int i = 0; i < agentic_synth::kMaxOscillators; ++i) {
            CHECK(patch.osc[i].volume >= 0.0f);
            CHECK(patch.osc[i].volume <= 1.0f);
        }
    }
}

TEST_CASE("MultiModalInput: applyTempoToPatch sets LFO rate proportional to BPM", "[multimodal]") {
    MultiModalInput input;
    PatchStruct patch{};

    input.applyTempoToPatch(120.0f, patch);
    float rate120 = patch.lfo[0].rate_hz;

    input.applyTempoToPatch(60.0f, patch);
    float rate60 = patch.lfo[0].rate_hz;

    CHECK(rate120 == Approx(2.0f)); // 120 bpm / 60 = 2 Hz
    CHECK(rate60 == Approx(1.0f));  // 60 bpm / 60 = 1 Hz
    CHECK(patch.lfo[0].depth > 0.0f);
    CHECK(patch.amp_env.attack_s > 0.0f);
}

TEST_CASE("MultiModalInput: applyTempoToPatch ignores zero/negative BPM", "[multimodal]") {
    MultiModalInput input;
    PatchStruct patch{};
    patch.lfo[0].rate_hz = 999.0f;

    input.applyTempoToPatch(0.0f, patch);
    CHECK(patch.lfo[0].rate_hz == Approx(999.0f)); // unchanged

    input.applyTempoToPatch(-1.0f, patch);
    CHECK(patch.lfo[0].rate_hz == Approx(999.0f)); // unchanged
}

TEST_CASE("MultiModalInput: detectTapTempo returns 0 for silence", "[multimodal]") {
    MultiModalInput input;
    std::vector<float> silence(44100, 0.0f);
    float bpm = input.detectTapTempo(silence.data(), static_cast<int>(silence.size()), 44100);
    CHECK(bpm == Approx(0.0f));
}
