#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>

#include "engine/ParamSmoother.h"

using agentic_synth::engine::ParamSmoother;

namespace {
constexpr float kTwoPi = 6.28318530717958647692f;

// Sample count to reach 99% of target for a one-pole LPF: ~5 * tau samples,
// where tau (in samples) = Fs / (2*pi*fc).
int samplesFor99Percent(double sampleRate, float cutoffHz) {
    const double tauSamples = sampleRate / (kTwoPi * cutoffHz);
    return static_cast<int>(std::ceil(5.0 * tauSamples));
}
} // namespace

TEST_CASE("ParamSmoother converges to target within 5*tau samples") {
    ParamSmoother s;
    s.setSampleRate(44100.0);
    s.setCutoffHz(30.0f);
    s.reset(0.0f);
    s.setTarget(1.0f);

    const int budget = samplesFor99Percent(44100.0, 30.0f);
    for (int i = 0; i < budget; ++i) {
        s.process();
    }
    // After 5*tau, one-pole LPF reaches 1 - e^-5 ≈ 0.9933 of target.
    REQUIRE(s.current() >= 0.99f);
    REQUIRE(s.current() <= 1.0f);
}

TEST_CASE("ParamSmoother::reset sets both state and target") {
    ParamSmoother s;
    s.setSampleRate(48000.0);
    s.setCutoffHz(30.0f);

    s.setTarget(0.42f);
    s.reset(0.75f);

    REQUIRE(s.current() == 0.75f);
    REQUIRE(s.target() == 0.75f);

    // Processing should be a no-op since state == target.
    const float before = s.current();
    s.process();
    REQUIRE(s.current() == before);
}

TEST_CASE("ParamSmoother convergence scales with sample rate") {
    constexpr float kCutoff = 30.0f;
    constexpr float kThreshold = 0.99f;

    auto countSamplesToThreshold = [&](double sampleRate) {
        ParamSmoother s;
        s.setSampleRate(sampleRate);
        s.setCutoffHz(kCutoff);
        s.reset(0.0f);
        s.setTarget(1.0f);

        int n = 0;
        const int safetyBudget = static_cast<int>(sampleRate); // 1 second cap
        while (s.current() < kThreshold && n < safetyBudget) {
            s.process();
            ++n;
        }
        return n;
    };

    const int n44 = countSamplesToThreshold(44100.0);
    const int n48 = countSamplesToThreshold(48000.0);
    const int n96 = countSamplesToThreshold(96000.0);

    // Higher Fs → more samples to cover the same time-constant.
    REQUIRE(n48 > n44);
    REQUIRE(n96 > n48);

    // Time-domain shape should be similar: convergence time in seconds
    // should match across sample rates within ~5%.
    const double t44 = n44 / 44100.0;
    const double t48 = n48 / 48000.0;
    const double t96 = n96 / 96000.0;

    REQUIRE_THAT(t48, Catch::Matchers::WithinRel(t44, 0.05));
    REQUIRE_THAT(t96, Catch::Matchers::WithinRel(t44, 0.05));
}

TEST_CASE("ParamSmoother::target returns most recent setTarget") {
    ParamSmoother s;
    s.setSampleRate(44100.0);
    s.setCutoffHz(30.0f);
    s.reset(0.0f);

    s.setTarget(0.5f);
    REQUIRE(s.target() == 0.5f);

    // Advance some samples — target() must not change.
    for (int i = 0; i < 100; ++i) {
        s.process();
    }
    REQUIRE(s.target() == 0.5f);

    s.setTarget(-0.25f);
    REQUIRE(s.target() == -0.25f);

    // Mid-flight override of target.
    s.setTarget(0.9f);
    REQUIRE(s.target() == 0.9f);
}

TEST_CASE("ParamSmoother::process is idempotent when state == target") {
    ParamSmoother s;
    s.setSampleRate(44100.0);
    s.setCutoffHz(30.0f);
    s.reset(0.33f);

    REQUIRE(s.current() == 0.33f);
    REQUIRE(s.target() == 0.33f);

    for (int i = 0; i < 10'000; ++i) {
        const float out = s.process();
        REQUIRE(out == 0.33f);
    }
    REQUIRE(s.current() == 0.33f);
}
