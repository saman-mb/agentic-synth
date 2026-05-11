#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "engine/Filter.h"

#include <cmath>
#include <limits>
#include <vector>

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
// MoogLadder nonlinear-feedback / drive / stability tests
// ---------------------------------------------------------------------------

TEST_CASE("MoogLadder — stable at max resonance across cutoffs and sample rates",
          "[filter][moog][stability]") {
    const float sampleRates[] = {44100.0f, 48000.0f, 96000.0f};
    const float cutoffs[] = {200.0f, 1000.0f, 4000.0f, 8000.0f};

    for (float sr : sampleRates) {
        for (float fc : cutoffs) {
            MoogLadder f;
            f.prepare(static_cast<double>(sr));
            f.setCutoff(fc);
            f.setResonance(1.0f);

            const int totalSamples = static_cast<int>(sr * 0.2f); // 200 ms
            for (int i = 0; i < totalSamples; ++i) {
                const float in = (i == 0) ? 1.0f : 0.0f; // unit impulse at t=0
                const float out = f.process(in);
                REQUIRE(std::isfinite(out));
                // tanh-bounded feedback caps |out| to slightly above 1; allow
                // headroom for the analytic prediction transient.
                REQUIRE(std::abs(out) <= 2.0f);
            }
        }
    }
}

TEST_CASE("MoogLadder — self-oscillates with zero input at max resonance",
          "[filter][moog][self-oscillation]") {
    const float sampleRate = 48000.0f;
    MoogLadder f;
    f.prepare(static_cast<double>(sampleRate));
    f.setCutoff(500.0f);
    f.setResonance(1.0f);

    // Kick the system with a brief impulse so the soft-saturation fixed point
    // doesn't trap it at zero. With tanh feedback the system has a soft limit
    // cycle that needs a finite perturbation to settle into.
    float out = 0.0f;
    for (int i = 0; i < 8; ++i) {
        out = f.process(0.5f);
        REQUIRE(std::isfinite(out));
    }

    // Run for ~1 s of zero input and measure the steady-state peak over the
    // final 200 ms.
    const int totalSamples = static_cast<int>(sampleRate);
    const int measureStart = totalSamples - static_cast<int>(sampleRate * 0.2f);
    float peak = 0.0f;
    for (int i = 1; i < totalSamples; ++i) {
        out = f.process(0.0f);
        REQUIRE(std::isfinite(out));
        if (i >= measureStart)
            peak = std::max(peak, std::abs(out));
    }
    REQUIRE(peak > 0.1f);
}

// Helper: harmonic-content metric. Fits the best amplitude+phase fundamental
// sine via in-phase/quadrature correlation, subtracts it, returns
// residualRMS / fundamentalRMS. A pure sine through a linear system → ~0.
// Any nonlinearity adds harmonics, raising the ratio.
static double harmonicResidualRatio(Filter& filter, float freq, float sampleRate,
                                    int cycles = 64) {
    const double w = 2.0 * M_PI * static_cast<double>(freq) /
                     static_cast<double>(sampleRate);
    const int totalSamples = static_cast<int>(
        static_cast<double>(sampleRate) / static_cast<double>(freq) *
        static_cast<double>(cycles));
    const int measureStart = totalSamples / 4;
    const int measureLen = totalSamples - measureStart;

    // Capture output (heap-allocated outside the realtime path — this is test code).
    std::vector<double> outBuf(static_cast<size_t>(measureLen), 0.0);
    for (int i = 0; i < totalSamples; ++i) {
        const float in = std::sin(static_cast<float>(w) * static_cast<float>(i));
        const float out = filter.process(in);
        if (i >= measureStart)
            outBuf[static_cast<size_t>(i - measureStart)] = out;
    }

    // Project onto cos / sin at the fundamental, using the *same* phase
    // reference as the input (absolute sample index).
    double c = 0.0, s = 0.0;
    for (int j = 0; j < measureLen; ++j) {
        const double n = static_cast<double>(j + measureStart);
        c += outBuf[static_cast<size_t>(j)] * std::cos(w * n);
        s += outBuf[static_cast<size_t>(j)] * std::sin(w * n);
    }
    // amplitude * cos(w*n + phi) least-squares fit: a = 2/N * sqrt(c^2 + s^2)
    const double amp = 2.0 * std::sqrt(c * c + s * s) /
                       static_cast<double>(measureLen);

    // Build the linear fundamental estimate and compute residual RMS.
    double resSqSum = 0.0;
    double fundSqSum = 0.0;
    for (int j = 0; j < measureLen; ++j) {
        const double n = static_cast<double>(j + measureStart);
        const double fundEst = (2.0 / static_cast<double>(measureLen)) *
                               (c * std::cos(w * n) + s * std::sin(w * n));
        const double res = outBuf[static_cast<size_t>(j)] - fundEst;
        resSqSum += res * res;
        fundSqSum += fundEst * fundEst;
    }
    const double resRms = std::sqrt(resSqSum / static_cast<double>(measureLen));
    const double fundRms = std::sqrt(fundSqSum / static_cast<double>(measureLen));
    (void)amp; // amp unused — kept for documentation
    return resRms / std::max(fundRms, 1.0e-12);
}

TEST_CASE("MoogLadder — tanh saturation bounds output and adds harmonics",
          "[filter][moog][saturation]") {
    const float sampleRate = 48000.0f;
    // Choose a fundamental near (but below) cutoff so the saturator hits hard
    // and the resulting harmonics are still partially in the passband.
    const float cutoff = 1500.0f;
    const float fund = 300.0f;

    // Run #1 — heavy drive + high resonance: expect bounded RMS and large
    // harmonic residual.
    MoogLadder f;
    f.prepare(static_cast<double>(sampleRate));
    f.setCutoff(cutoff);
    f.setResonance(0.9f);
    f.setDrive(1.0f);

    // Bound check: collect RMS over the back half of the run.
    const int totalSamples = static_cast<int>(sampleRate / fund * 64);
    const int measureStart = totalSamples / 2;
    double rmsSum = 0.0;
    int rmsCount = 0;
    const float omega = 2.0f * static_cast<float>(M_PI) * fund / sampleRate;
    for (int i = 0; i < totalSamples; ++i) {
        const float in = std::sin(omega * static_cast<float>(i));
        const float out = f.process(in);
        REQUIRE(std::isfinite(out));
        if (i >= measureStart) {
            rmsSum += static_cast<double>(out) * static_cast<double>(out);
            ++rmsCount;
        }
    }
    const double rms = std::sqrt(rmsSum / std::max(rmsCount, 1));
    REQUIRE(rms < 2.0); // tanh + driveComp keep level bounded

    // Run #2 — same params, fresh instance — measure harmonic residual.
    MoogLadder f2;
    f2.prepare(static_cast<double>(sampleRate));
    f2.setCutoff(cutoff);
    f2.setResonance(0.9f);
    f2.setDrive(1.0f);
    const double ratioHot = harmonicResidualRatio(f2, fund, sampleRate);
    REQUIRE(ratioHot > 0.05); // > 5% non-fundamental energy
}

TEST_CASE("MoogLadder — denormal-safe under prolonged silence",
          "[filter][moog][denormal]") {
    MoogLadder f;
    f.prepare(48000.0);
    f.setCutoff(1000.0f);
    f.setResonance(0.5f);

    // Excite once, then feed 1e6 zero samples — integrators must stay finite
    // and not stall / explode due to subnormals.
    f.process(1.0f);
    for (int i = 0; i < 1'000'000; ++i) {
        const float out = f.process(0.0f);
        REQUIRE(std::isfinite(out));
    }
}

TEST_CASE("MoogLadder — drive parameter adds harmonic energy",
          "[filter][moog][drive]") {
    const float sampleRate = 48000.0f;
    const float fund = 300.0f;
    const float cutoff = 1500.0f;

    MoogLadder fDry;
    fDry.prepare(static_cast<double>(sampleRate));
    fDry.setCutoff(cutoff);
    fDry.setResonance(0.5f);
    fDry.setDrive(0.0f);
    const double ratioDry = harmonicResidualRatio(fDry, fund, sampleRate);

    MoogLadder fHot;
    fHot.prepare(static_cast<double>(sampleRate));
    fHot.setCutoff(cutoff);
    fHot.setResonance(0.5f);
    fHot.setDrive(1.0f);
    const double ratioHot = harmonicResidualRatio(fHot, fund, sampleRate);

    REQUIRE(ratioHot > ratioDry);
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
