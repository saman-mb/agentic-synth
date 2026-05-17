// Phase E (#265) — TubeSat DSP module tests.
//
// Coverage:
//   • prepare + reset clean.
//   • drive == 0 → bit-exact bypass.
//   • drive > 0 → output differs from input.
//   • Asymmetric character: peak positive output level differs from peak
//     negative output level (triode-style 2nd-harmonic emphasis).
//   • DC blocker: a constant-DC input decays toward 0 after the HPF settles.

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

#include "engine/TubeSat.h"

using agentic_synth::engine::TubeSat;

namespace {
constexpr double kSr = 48000.0;
constexpr int kBlock = 4800;

std::vector<float> makeSine(int n, float freqHz, float sr, float amp = 0.7f) {
    std::vector<float> out(n);
    for (int i = 0; i < n; ++i)
        out[i] = amp * std::sin(2.0f * 3.14159265f * freqHz * static_cast<float>(i) / sr);
    return out;
}
} // namespace

TEST_CASE("TubeSat::prepare + reset do not throw", "[tubesat][phaseE]") {
    TubeSat t;
    REQUIRE_NOTHROW(t.prepare(kSr, 2));
    REQUIRE_NOTHROW(t.reset());
}

TEST_CASE("TubeSat drive == 0 is a bit-exact bypass", "[tubesat][phaseE]") {
    TubeSat t;
    t.prepare(kSr, 2);
    t.setDrive(0.0f);
    t.setMix(1.0f);

    auto inL = makeSine(kBlock, 440.0f, static_cast<float>(kSr));
    auto inR = makeSine(kBlock, 440.0f, static_cast<float>(kSr));
    auto refL = inL;
    auto refR = inR;
    t.processStereo(inL.data(), inR.data(), kBlock);
    for (int i = 0; i < kBlock; ++i) {
        REQUIRE(inL[i] == refL[i]);
        REQUIRE(inR[i] == refR[i]);
    }
}

TEST_CASE("TubeSat drive > 0 produces output that differs from input", "[tubesat][phaseE]") {
    TubeSat t;
    t.prepare(kSr, 2);
    t.setDrive(0.3f);
    t.setMix(1.0f);

    auto inL = makeSine(kBlock, 440.0f, static_cast<float>(kSr), 1.0f);
    auto inR = makeSine(kBlock, 440.0f, static_cast<float>(kSr), 1.0f);
    auto refL = inL;
    auto refR = inR;
    t.processStereo(inL.data(), inR.data(), kBlock);

    int diffs = 0;
    for (int i = 200; i < kBlock; ++i) {
        if (std::fabs(inL[i] - refL[i]) > 1e-4f)
            ++diffs;
    }
    REQUIRE(diffs > kBlock / 2);
}

TEST_CASE("TubeSat asymmetric: shapes positive and negative half-cycles differently",
          "[tubesat][phaseE]") {
    // Probe at a mid-range input level where tanh hasn't saturated yet.
    // At very high drive both ±x→±1 (the normalisation reaches its asymptote),
    // so asymmetry only shows on the linear-to-soft-knee portion of the curve.
    // Drive=0.2 + input=±0.3 keeps us in that zone.
    TubeSat t;
    t.prepare(kSr, 2);
    t.setDrive(0.2f);
    t.setMix(1.0f);

    // Feed +0.3 — first sample's HPF y[0] = (sat - 0) + a*0 = sat (xPrev=0).
    std::vector<float> posL{0.3f};
    std::vector<float> posR{0.3f};
    t.reset();
    t.processStereo(posL.data(), posR.data(), 1);
    const float yPos = posL[0];

    std::vector<float> negL{-0.3f};
    std::vector<float> negR{-0.3f};
    t.reset();
    t.processStereo(negL.data(), negR.data(), 1);
    const float yNeg = negL[0];

    REQUIRE(yPos > 0.0f);
    REQUIRE(yNeg < 0.0f);
    // |yPos| != |yNeg| — the positive lobe has steeper drive than the
    // negative, so |y(+0.3)| > |y(-0.3)| in the soft-knee region.
    REQUIRE(std::fabs(std::fabs(yPos) - std::fabs(yNeg)) > 0.005f * std::fabs(yPos));
}

TEST_CASE("TubeSat DC blocker: a constant DC input decays toward zero", "[tubesat][phaseE]") {
    TubeSat t;
    t.prepare(kSr, 2);
    t.setDrive(0.3f);
    t.setMix(1.0f);

    // Feed 1.0 (DC) for ~500 ms. The 20 Hz HPF has a time constant ≈ 8 ms,
    // so by 500 ms the output should have decayed to a small fraction of the
    // initial transient.
    const int n = static_cast<int>(0.5 * kSr);
    std::vector<float> bufL(n, 1.0f);
    std::vector<float> bufR(n, 1.0f);
    t.processStereo(bufL.data(), bufR.data(), n);

    // Final sample should be near zero (DC has been blocked).
    REQUIRE(std::fabs(bufL[n - 1]) < 0.05f);
    REQUIRE(std::fabs(bufR[n - 1]) < 0.05f);
}
