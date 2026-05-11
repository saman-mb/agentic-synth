#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "engine/Reverb.h"

#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

using namespace agentic_synth::engine;

namespace {

constexpr double kSR = 44100.0;

float rms(const float* x, std::size_t n) {
    double acc = 0.0;
    for (std::size_t i = 0; i < n; ++i) acc += static_cast<double>(x[i]) * x[i];
    return static_cast<float>(std::sqrt(acc / static_cast<double>(std::max<std::size_t>(1, n))));
}

// Render numSamples to outL/outR with constant input (default zero).
void render(Reverb& rev,
            std::vector<float>& outL,
            std::vector<float>& outR,
            std::size_t numSamples,
            float inL = 0.0f,
            float inR = 0.0f) {
    outL.assign(numSamples, 0.0f);
    outR.assign(numSamples, 0.0f);
    for (std::size_t i = 0; i < numSamples; ++i) {
        rev.process(inL, inR, outL[i], outR[i]);
    }
}

} // namespace

// ---------------------------------------------------------------------------
// 1) Impulse decays to silence within 2s (size=0.5).
// ---------------------------------------------------------------------------
TEST_CASE("Reverb decays to near-silence after impulse (size=0.5)", "[reverb]") {
    Reverb rev;
    rev.prepare(kSR);
    rev.setSize(0.5f);
    rev.setDamp(0.5f);
    rev.setMix(1.0f); // wet only — measure tail directly
    rev.reset();

    const std::size_t total = static_cast<std::size_t>(kSR * 2.0); // 2s
    std::vector<float> outL(total, 0.0f), outR(total, 0.0f);

    // Single full-scale impulse on first sample.
    float l = 0.0f, r = 0.0f;
    rev.process(1.0f, 1.0f, l, r);
    outL[0] = l;
    outR[0] = r;
    for (std::size_t i = 1; i < total; ++i) {
        rev.process(0.0f, 0.0f, outL[i], outR[i]);
    }

    const std::size_t tailLen = static_cast<std::size_t>(kSR * 0.1); // last 100 ms
    const std::size_t tailStart = total - tailLen;
    const float tailRmsL = rms(outL.data() + tailStart, tailLen);
    const float tailRmsR = rms(outR.data() + tailStart, tailLen);

    REQUIRE(tailRmsL < 1e-3f);
    REQUIRE(tailRmsR < 1e-3f);
}

// ---------------------------------------------------------------------------
// 2) Larger size → longer tail (more energy in last 500 ms).
// ---------------------------------------------------------------------------
TEST_CASE("Reverb size controls tail length", "[reverb]") {
    auto measureTailRms = [](float sizeParam) {
        Reverb rev;
        rev.prepare(kSR);
        rev.setSize(sizeParam);
        rev.setDamp(0.2f);
        rev.setMix(1.0f);
        rev.reset();

        const std::size_t total = static_cast<std::size_t>(kSR * 2.0);
        std::vector<float> outL(total, 0.0f), outR(total, 0.0f);

        // Impulse.
        rev.process(1.0f, 1.0f, outL[0], outR[0]);
        for (std::size_t i = 1; i < total; ++i) {
            rev.process(0.0f, 0.0f, outL[i], outR[i]);
        }

        const std::size_t tailLen = static_cast<std::size_t>(kSR * 0.5);
        const std::size_t tailStart = total - tailLen;
        return 0.5f * (rms(outL.data() + tailStart, tailLen) +
                       rms(outR.data() + tailStart, tailLen));
    };

    const float small  = measureTailRms(0.2f);
    const float large  = measureTailRms(0.9f);

    REQUIRE(large > small);
    // Sanity: large-size tail should be meaningfully louder, not just numerical noise.
    REQUIRE(large > small * 2.0f);
}

// ---------------------------------------------------------------------------
// 3) mix=0 passes dry signal through unchanged.
// ---------------------------------------------------------------------------
TEST_CASE("Reverb mix=0 passes dry signal unchanged", "[reverb]") {
    Reverb rev;
    rev.prepare(kSR);
    rev.setSize(0.7f);
    rev.setDamp(0.5f);
    rev.setMix(0.0f);
    rev.reset();

    const int n = 4096;
    const float freq = 440.0f;
    const float omega = 2.0f * static_cast<float>(M_PI) * freq / static_cast<float>(kSR);
    for (int i = 0; i < n; ++i) {
        const float s = 0.5f * std::sin(omega * static_cast<float>(i));
        float l = 0.0f, r = 0.0f;
        rev.process(s, s, l, r);
        REQUIRE_THAT(l, Catch::Matchers::WithinAbs(s, 1e-6));
        REQUIRE_THAT(r, Catch::Matchers::WithinAbs(s, 1e-6));
    }
}

// ---------------------------------------------------------------------------
// 4) mix=1 produces non-zero output from non-zero input.
// ---------------------------------------------------------------------------
TEST_CASE("Reverb mix=1 produces non-zero output for non-silent input", "[reverb]") {
    Reverb rev;
    rev.prepare(kSR);
    rev.setSize(0.5f);
    rev.setDamp(0.5f);
    rev.setMix(1.0f);
    rev.reset();

    const std::size_t n = static_cast<std::size_t>(kSR * 0.5); // 0.5s
    std::vector<float> outL(n, 0.0f), outR(n, 0.0f);
    const float omega = 2.0f * static_cast<float>(M_PI) * 220.0f / static_cast<float>(kSR);
    for (std::size_t i = 0; i < n; ++i) {
        const float s = 0.5f * std::sin(omega * static_cast<float>(i));
        rev.process(s, s, outL[i], outR[i]);
    }

    // Skip initial pre-roll where buffers fill up.
    const std::size_t skip = static_cast<std::size_t>(kSR * 0.1);
    const float rmsL = rms(outL.data() + skip, n - skip);
    const float rmsR = rms(outR.data() + skip, n - skip);
    REQUIRE(rmsL > 0.01f);
    REQUIRE(rmsR > 0.01f);
}

// ---------------------------------------------------------------------------
// 5) No NaN/Inf in steady state at high feedback.
// ---------------------------------------------------------------------------
TEST_CASE("Reverb produces finite output at high feedback", "[reverb]") {
    Reverb rev;
    rev.prepare(kSR);
    rev.setSize(0.95f); // near self-oscillation territory for combs
    rev.setDamp(0.1f);
    rev.setMix(1.0f);
    rev.reset();

    const std::size_t n = static_cast<std::size_t>(kSR * 1.0); // 1s
    const float omega = 2.0f * static_cast<float>(M_PI) * 440.0f / static_cast<float>(kSR);
    for (std::size_t i = 0; i < n; ++i) {
        const float s = std::sin(omega * static_cast<float>(i)); // full-scale
        float l = 0.0f, r = 0.0f;
        rev.process(s, s, l, r);
        REQUIRE(std::isfinite(l));
        REQUIRE(std::isfinite(r));
    }
}

// ---------------------------------------------------------------------------
// 6) reset() clears state — post-reset zero-in yields zero-out immediately.
// ---------------------------------------------------------------------------
TEST_CASE("Reverb reset clears delay lines", "[reverb]") {
    Reverb rev;
    rev.prepare(kSR);
    rev.setSize(0.8f);
    rev.setDamp(0.3f);
    rev.setMix(1.0f);
    rev.reset();

    // Excite with noise/sine for a bit.
    const std::size_t excite = static_cast<std::size_t>(kSR * 0.2);
    const float omega = 2.0f * static_cast<float>(M_PI) * 330.0f / static_cast<float>(kSR);
    for (std::size_t i = 0; i < excite; ++i) {
        const float s = 0.5f * std::sin(omega * static_cast<float>(i));
        float l = 0.0f, r = 0.0f;
        rev.process(s, s, l, r);
    }

    rev.reset();

    // After reset, with zero input, output must be exactly zero.
    for (int i = 0; i < 256; ++i) {
        float l = 0.0f, r = 0.0f;
        rev.process(0.0f, 0.0f, l, r);
        REQUIRE(l == 0.0f);
        REQUIRE(r == 0.0f);
    }
}
