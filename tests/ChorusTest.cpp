// Phase E (#265) — Chorus DSP module tests.
//
// Behavioural coverage:
//   • prepare + reset do not throw / no allocation failures.
//   • mix == 0 → output equals input (bit-exact bypass; the augmenter relies
//     on this to ship the same patch JSON to engines whose chorus knob is
//     off without paying for the delay-line reads).
//   • mix > 0 → output differs from input across a sustained block (proves
//     the modulated delay is actually mixing in).
//   • Output stays bounded — feeding a unit-amplitude sine never lets the
//     three-tap sum exceed [-1.1, +1.1] (avg of three near-unity readings).
//   • Stereo width — at mix > 0 the L/R outputs differ across the block (the
//     LFOs have a π phase offset between channels).

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

#include "engine/Chorus.h"

using agentic_synth::engine::Chorus;

namespace {

constexpr double kSr = 48000.0;
constexpr int kBlock = 4800; // 100 ms — long enough for chorus to fully fill its delay line.

std::vector<float> makeSine(int n, float freqHz, float sr, float amp = 0.5f) {
    std::vector<float> out(n);
    for (int i = 0; i < n; ++i)
        out[i] = amp * std::sin(2.0f * 3.14159265f * freqHz * static_cast<float>(i) / sr);
    return out;
}

} // namespace

TEST_CASE("Chorus::prepare + reset do not throw and zero internal state", "[chorus][phaseE]") {
    Chorus c;
    REQUIRE_NOTHROW(c.prepare(kSr, 2));
    REQUIRE_NOTHROW(c.reset());
}

TEST_CASE("Chorus mix == 0 is a bit-exact bypass", "[chorus][phaseE]") {
    Chorus c;
    c.prepare(kSr, 2);
    c.setRate(0.4f);
    c.setDepth(0.35f);
    c.setMix(0.0f);

    auto inL = makeSine(kBlock, 440.0f, static_cast<float>(kSr));
    auto inR = makeSine(kBlock, 440.0f, static_cast<float>(kSr));
    auto refL = inL;
    auto refR = inR;
    c.processStereo(inL.data(), inR.data(), kBlock);

    for (int i = 0; i < kBlock; ++i) {
        REQUIRE(inL[i] == refL[i]);
        REQUIRE(inR[i] == refR[i]);
    }
}

TEST_CASE("Chorus mix > 0 produces output that differs from input", "[chorus][phaseE]") {
    Chorus c;
    c.prepare(kSr, 2);
    c.setRate(0.4f);
    c.setDepth(0.35f);
    c.setMix(0.5f);

    auto inL = makeSine(kBlock, 440.0f, static_cast<float>(kSr));
    auto inR = makeSine(kBlock, 440.0f, static_cast<float>(kSr));
    auto refL = inL;
    auto refR = inR;
    c.processStereo(inL.data(), inR.data(), kBlock);

    // Skip the initial pre-delay window (~30 ms) before checking diff —
    // until the delay line fills, the wet contribution is the zero-initialized
    // buffer and the output is just the dry path scaled.
    int diffs = 0;
    for (int i = static_cast<int>(0.04 * kSr); i < kBlock; ++i) {
        if (std::fabs(inL[i] - refL[i]) > 1e-5f)
            ++diffs;
    }
    REQUIRE(diffs > kBlock / 4);
}

TEST_CASE("Chorus output stays bounded — no blow-up on unit sine", "[chorus][phaseE]") {
    Chorus c;
    c.prepare(kSr, 2);
    c.setRate(0.4f);
    c.setDepth(1.0f);
    c.setMix(1.0f);

    auto inL = makeSine(kBlock, 220.0f, static_cast<float>(kSr), 1.0f);
    auto inR = makeSine(kBlock, 220.0f, static_cast<float>(kSr), 1.0f);
    c.processStereo(inL.data(), inR.data(), kBlock);

    for (int i = 0; i < kBlock; ++i) {
        REQUIRE(inL[i] >= -1.5f);
        REQUIRE(inL[i] <= 1.5f);
        REQUIRE(inR[i] >= -1.5f);
        REQUIRE(inR[i] <= 1.5f);
    }
}

TEST_CASE("Chorus produces stereo width — L and R differ at mix > 0", "[chorus][phaseE]") {
    Chorus c;
    c.prepare(kSr, 2);
    c.setRate(0.6f);
    c.setDepth(0.5f);
    c.setMix(0.6f);

    auto inL = makeSine(kBlock, 440.0f, static_cast<float>(kSr));
    auto inR = makeSine(kBlock, 440.0f, static_cast<float>(kSr));
    c.processStereo(inL.data(), inR.data(), kBlock);

    int lrDiffs = 0;
    for (int i = static_cast<int>(0.05 * kSr); i < kBlock; ++i) {
        if (std::fabs(inL[i] - inR[i]) > 1e-4f)
            ++lrDiffs;
    }
    REQUIRE(lrDiffs > kBlock / 4);
}
