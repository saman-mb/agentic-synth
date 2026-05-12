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
    d.setTimeSeconds(0.1f); // 4410 samples
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

TEST_CASE("Delay: stereo=1 real ping-pong") {
    // ALGORITHM CHANGE (Phase 3 / architect P1 #8): the previous Delay only
    // cross-fed the FEEDBACK between channels but still wrote the input to its
    // same-side buffer, so an L impulse came out of L after one delay length.
    // Real ping-pong routes L's signal+feedback into the R delay line (and R's
    // into L), so an L impulse appears on R after one delay length, then back
    // on L after two delay lengths, alternating until decayed by feedback.
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

    // t=1×delay: L impulse landed in the R buffer => R taps it, L is silent.
    REQUIRE(std::abs(rAt1) > 0.5f);
    REQUIRE(std::abs(lAt1) < 1e-4f);
    // t=2×delay: R's delayed signal (= 1.0) got cross-fed back into L through
    // feedback (× 0.5) and now appears on L. R is silent at this moment.
    REQUIRE(std::abs(lAt2) > 0.2f);
    REQUIRE(std::abs(rAt2) < 1e-3f);
}

TEST_CASE("Delay: fractional interpolation splits impulse across neighbours") {
    // Push impulse, set delay to 100.5 samples (≈ 2.28ms, above the 1ms min
    // clamp), expect ~50/50 amplitude split between the readouts at samples
    // 100 and 101 (linear interp signature).
    Delay d;
    d.prepare(kSR);
    d.setFeedback(0.0f);
    d.setMix(1.0f);
    d.setStereo(0.0f);
    d.setTimeSeconds(static_cast<float>(100.5 / kSR));

    float outL = 0.0f;
    float outR = 0.0f;
    // Impulse on L at n=0.
    d.process(1.0f, 0.0f, outL, outR);

    float at100 = 0.0f;
    float at101 = 0.0f;
    float maxOther = 0.0f;
    for (int n = 1; n <= 120; ++n) {
        d.process(0.0f, 0.0f, outL, outR);
        if (n == 100) {
            at100 = outL;
        } else if (n == 101) {
            at101 = outL;
        } else {
            maxOther = std::max(maxOther, std::abs(outL));
        }
    }

    // Linear interp at frac≈0.5 => roughly 0.5 weight in each tap. Allow
    // some tolerance for the float roundtrip on the time→samples conversion.
    REQUIRE_THAT(at100, Catch::Matchers::WithinAbs(0.5f, 5e-3f));
    REQUIRE_THAT(at101, Catch::Matchers::WithinAbs(0.5f, 5e-3f));
    // Other samples must be silent — no leak.
    REQUIRE(maxOther < 1e-5f);
}

TEST_CASE("Delay: modulating delay time produces no zipper / no NaN") {
    // Sweep 5ms → 15ms → 5ms over 1000 samples on a constant tone.
    // Assert: no NaN, output bounded, no large sample-to-sample jumps.
    Delay d;
    d.prepare(kSR);
    d.setFeedback(0.3f);
    d.setMix(1.0f);
    d.setStereo(0.0f);
    d.setTimeSeconds(0.005f);

    const int N = 1000;
    const float minTime = 0.005f;
    const float maxTime = 0.015f;

    float prevOutL = 0.0f;
    float maxJump = 0.0f;
    float maxAbs = 0.0f;

    for (int n = 0; n < N; ++n) {
        // Triangular sweep over [minTime, maxTime].
        const float phase = static_cast<float>(n) / static_cast<float>(N - 1);
        const float tri = phase < 0.5f ? (2.0f * phase) : (2.0f * (1.0f - phase));
        const float t = minTime + (maxTime - minTime) * tri;
        d.setTimeSeconds(t);

        const float tt = static_cast<float>(n) / static_cast<float>(kSR);
        const float s = 0.5f * std::sin(2.0f * kPi * 220.0f * tt);
        float outL = 0.0f;
        float outR = 0.0f;
        d.process(s, s, outL, outR);

        REQUIRE(std::isfinite(outL));
        REQUIRE(std::isfinite(outR));
        maxAbs = std::max(maxAbs, std::abs(outL));
        if (n > 0) {
            maxJump = std::max(maxJump, std::abs(outL - prevOutL));
        }
        prevOutL = outL;
    }

    // Output stays bounded (no resonant blow-up from modulation).
    REQUIRE(maxAbs < 4.0f);
    // Largest sample-to-sample jump should be well under full-scale: with
    // integer-quantised reads this regularly hit > 0.5 from index jumps.
    // Linear interp keeps deltas modest even with the sweep. Tightened from
    // 0.5 → 0.05 (Phase 3 follow-up): the measured value with linear interp
    // on a 220 Hz tone is ~0.044, so a 0.05 bound holds with ~14% margin and
    // is sensitive enough to flag a regression to integer-quantised reads.
    REQUIRE(maxJump < 0.05f);
}

TEST_CASE("Delay: stereo=0 mono regression — independent L/R lines") {
    // At stereo=0 the L and R delay lines run completely in parallel:
    // the L output is determined only by L input + L delayed feedback.
    // Feed asymmetric input (L=tone, R=0) and confirm R output stays at zero
    // for the dry tail.
    Delay d;
    d.prepare(kSR);
    d.setTimeSeconds(0.05f);
    d.setFeedback(0.5f);
    d.setMix(1.0f);
    d.setStereo(0.0f);

    const int delaySamples = static_cast<int>(0.05 * kSR);
    const int totalSamples = delaySamples * 3 + 100;

    float outL = 0.0f;
    float outR = 0.0f;
    d.process(1.0f, 0.0f, outL, outR);

    float maxRWet = 0.0f;
    float lAt1 = 0.0f;
    float lAt2 = 0.0f;
    for (int n = 1; n < totalSamples; ++n) {
        d.process(0.0f, 0.0f, outL, outR);
        if (n == delaySamples) {
            lAt1 = outL;
        } else if (n == 2 * delaySamples) {
            lAt2 = outL;
        }
        maxRWet = std::max(maxRWet, std::abs(outR));
    }

    // R never sees any energy when stereo=0 and input was L-only.
    REQUIRE(maxRWet < 1e-5f);
    // L behaves like a classic mono delay with 0.5 feedback.
    REQUIRE_THAT(lAt1, Catch::Matchers::WithinAbs(1.0f, 1e-4f));
    REQUIRE_THAT(lAt2, Catch::Matchers::WithinAbs(0.5f, 1e-3f));
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
