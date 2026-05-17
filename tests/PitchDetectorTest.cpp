// Phase G / #247 — pitch detector tests. Synthesize pure tones at common
// reference frequencies and assert the detector locks within ±1 semitone
// with confidence > 0.7. Noise input must report low confidence so the
// "did you mean B♭3?" hint stays silent on rumble.

#include "agent/PitchDetector.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <cstdint>
#include <random>
#include <vector>

using agentic_synth::agent::PitchDetector;

namespace {

constexpr int kSampleRate = 16000;
constexpr float kPi = 3.14159265358979323846f;

std::vector<std::int16_t> makeSine(float freqHz, int sampleRate, int numSamples, float amp = 0.5f) {
    std::vector<std::int16_t> out;
    out.reserve(static_cast<size_t>(numSamples));
    const float omega = 2.0f * kPi * freqHz / static_cast<float>(sampleRate);
    for (int i = 0; i < numSamples; ++i) {
        const float s = amp * std::sin(omega * static_cast<float>(i));
        out.push_back(static_cast<std::int16_t>(s * 32767.0f));
    }
    return out;
}

std::vector<std::int16_t> makeNoise(int numSamples, float amp = 0.3f) {
    std::vector<std::int16_t> out;
    out.reserve(static_cast<size_t>(numSamples));
    std::mt19937 rng{42};
    std::uniform_real_distribution<float> dist(-amp, amp);
    for (int i = 0; i < numSamples; ++i)
        out.push_back(static_cast<std::int16_t>(dist(rng) * 32767.0f));
    return out;
}

} // namespace

TEST_CASE("PitchDetector: 440 Hz sine locks to A4 (midi 69)", "[pitch]") {
    auto buf = makeSine(440.0f, kSampleRate, kSampleRate); // 1 second
    auto res = PitchDetector::detect(buf.data(), static_cast<int>(buf.size()), kSampleRate);
    REQUIRE(res.midi_note >= 0);
    CHECK(std::abs(res.midi_note - 69) <= 1);
    CHECK(res.confidence > 0.7f);
}

TEST_CASE("PitchDetector: 220 Hz sine locks to A3 (midi 57)", "[pitch]") {
    auto buf = makeSine(220.0f, kSampleRate, kSampleRate);
    auto res = PitchDetector::detect(buf.data(), static_cast<int>(buf.size()), kSampleRate);
    REQUIRE(res.midi_note >= 0);
    CHECK(std::abs(res.midi_note - 57) <= 1);
    CHECK(res.confidence > 0.7f);
}

TEST_CASE("PitchDetector: 880 Hz sine locks to A5 (midi 81)", "[pitch]") {
    auto buf = makeSine(880.0f, kSampleRate, kSampleRate);
    auto res = PitchDetector::detect(buf.data(), static_cast<int>(buf.size()), kSampleRate);
    REQUIRE(res.midi_note >= 0);
    CHECK(std::abs(res.midi_note - 81) <= 1);
    CHECK(res.confidence > 0.7f);
}

TEST_CASE("PitchDetector: noise reports low confidence", "[pitch]") {
    auto buf = makeNoise(kSampleRate);
    auto res = PitchDetector::detect(buf.data(), static_cast<int>(buf.size()), kSampleRate);
    // Either no detection (midi == -1) or confidence below the
    // "show the hint" threshold of 0.7. Both outcomes keep the UI quiet.
    if (res.midi_note >= 0)
        CHECK(res.confidence < 0.7f);
}

TEST_CASE("PitchDetector: silence reports no pitch", "[pitch]") {
    std::vector<std::int16_t> buf(kSampleRate, 0);
    auto res = PitchDetector::detect(buf.data(), static_cast<int>(buf.size()), kSampleRate);
    CHECK(res.midi_note == -1);
}

TEST_CASE("PitchDetector: frequencyToMidi covers reference notes", "[pitch]") {
    CHECK(PitchDetector::frequencyToMidi(440.0f) == 69); // A4
    CHECK(PitchDetector::frequencyToMidi(261.626f) == 60); // C4
    CHECK(PitchDetector::frequencyToMidi(880.0f) == 81); // A5
    CHECK(PitchDetector::frequencyToMidi(0.0f) == -1);
}
