#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "engine/Delay.h"

#include <cmath>
#include <limits>

using agentic_synth::engine::Delay;

namespace {
constexpr double kSR = 44100.0;
constexpr float kPi = 3.14159265358979323846f;
} // namespace

TEST_CASE("Delay: impulse appears at correct delay time") {
    Delay d;
    d.prepare(kSR);
    d.setTimeSeconds(0.1f);   // 4410 samples
    d.setFeedback(0.0f);
    d.setMix(1.0f);
    d.setStereo(0.0f);

    const int delaySamples = 4410;
    const int totalSamples = delaySamples + 100;

    float outL = 0.0f;
    float outR = 0.0f;

    // Sample 0: impulse on both channels (mono-ish).
    d.process(1.0f, 1.0f, outL, outR);
    REQUIRE(outL == 0.0f); // wet only, no delayed signal yet
    REQUIRE(outR == 0.0f);

    float peakL = 0.0f;
    int peakSample = -1;
    for (int n = 1; n < totalSamples; ++n) {
        d.process(0.0f, 0.0f, outL, outR);
        // Pre-delay should be zero.
        if (n < delaySamples) {
            REQUIRE(outL == 0.0f);
            REQUIRE(outR == 0.0f);
        }
        if (std::abs(outL) > peakL) {
            peakL = std::abs(outL);
            peakSample = n;
        }
    }

    REQUIRE(peakSample == delaySamples);
    REQUIRE(peakL > 0.5f);
}

TEST_CASE("Delay: feedback creates echoes") {
    Delay d;
    d.prepare(kSR);
    d.setTimeSeconds(0.1f); // 4410 samples to keep numbers tidy
    d.setFeedback(0.5f);
    d.setMix(1.0f);
    d.setStereo(0.0f);

    const int delaySamples = 4410;
    const int totalSamples = delaySamples * 3 + 100;

    float outL = 0.0f;
    float outR = 0.0f;
    d.process(1.0f, 1.0f, outL, outR);

    float ampAt1 = 0.0f;
    float ampAt2 = 0.0f;
    for (int n = 1; n < totalSamples; ++n) {
        d.process(0.0f, 0.0f, outL, outR);
        if (n == delaySamples) {
            ampAt1 = outL;
        } else if (n == 2 * delaySamples) {
            ampAt2 = outL;
        }
    }

    // First echo ~ input (mix=1) → ~1.0
    REQUIRE(std::abs(ampAt1 - 1.0f) < 1e-4f);
    // Second echo ~ feedback × first echo → ~0.5
    REQUIRE(std::abs(ampAt2 - 0.5f) < 1e-3f);
}

TEST_CASE("Delay: mix=0 passes dry unchanged") {
    Delay d;
    d.prepare(kSR);
    d.setTimeSeconds(0.05f);
    d.setFeedback(0.7f); // even with feedback, mix=0 should mute wet path
    d.setMix(0.0f);
    d.setStereo(0.5f);

    const int totalSamples = 1024;
    for (int n = 0; n < totalSamples; ++n) {
        const float t = static_cast<float>(n) / static_cast<float>(kSR);
        const float s = 0.4f * std::sin(2.0f * kPi * 440.0f * t);
        float outL = 0.0f;
        float outR = 0.0f;
        d.process(s, s, outL, outR);
        REQUIRE(std::abs(outL - s) < 1e-6f);
        REQUIRE(std::abs(outR - s) < 1e-6f);
    }
}

TEST_CASE("Delay: stereo=1 ping-pong") {
    Delay d;
    d.prepare(kSR);
    d.setTimeSeconds(0.1f);
    d.setFeedback(0.5f);
    d.setMix(1.0f);
    d.setStereo(1.0f);

    const int delaySamples = 4410;
    const int totalSamples = delaySamples * 3 + 100;

    float outL = 0.0f;
    float outR = 0.0f;
    // Impulse on L only.
    d.process(1.0f, 0.0f, outL, outR);

    float lAt1 = 0.0f;
    float rAt1 = 0.0f;
    float lAt2 = 0.0f;
    float rAt2 = 0.0f;
    for (int n = 1; n < totalSamples; ++n) {
        d.process(0.0f, 0.0f, outL, outR);
        if (n == delaySamples) {
            lAt1 = outL;
            rAt1 = outR;
        } else if (n == 2 * delaySamples) {
            lAt2 = outL;
            rAt2 = outR;
        }
    }

    // Per spec DSP: output taps are direct (mix * delayedL/R); only the feedback
    // path is cross-fed by `stereo`. So at t=1×delay the L impulse comes out on L
    // (R is still empty). The cross-feed routes it into bufR for the next pass.
    REQUIRE(std::abs(lAt1) > 0.5f);
    REQUIRE(std::abs(rAt1) < 1e-4f);

    // At t=2×delay: the cross-fed signal in bufR (= fb × 1.0) is now read out on R.
    // L's buffer was zero at that write step → L is silent.
    REQUIRE(std::abs(rAt2) > 0.2f);
    REQUIRE(std::abs(lAt2) < 1e-3f);
}

TEST_CASE("Delay: stability at max feedback") {
    Delay d;
    d.prepare(kSR);
    d.setTimeSeconds(0.05f);
    d.setFeedback(0.99f);
    d.setMix(0.5f);
    d.setStereo(0.3f);

    const int totalSamples = static_cast<int>(5.0 * kSR); // 5 seconds
    float maxAbs = 0.0f;

    for (int n = 0; n < totalSamples; ++n) {
        const float t = static_cast<float>(n) / static_cast<float>(kSR);
        const float s = std::sin(2.0f * kPi * 220.0f * t); // full-scale
        float outL = 0.0f;
        float outR = 0.0f;
        d.process(s, s, outL, outR);
        REQUIRE(std::isfinite(outL));
        REQUIRE(std::isfinite(outR));
        const float a = std::max(std::abs(outL), std::abs(outR));
        if (a > maxAbs) {
            maxAbs = a;
        }
    }

    REQUIRE(maxAbs < 50.0f);
}
