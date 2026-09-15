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

#include "SpectralHelpers.h"
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

// ── #431 aliasing baseline ──────────────────────────────────────────────────
// Analysis tone: 2500 Hz does not divide 48 kHz, so aliased harmonics fold to
// non-integer multiples and are not mistaken for legitimate harmonic content.
constexpr double kAliasSr = 48000.0;
constexpr double kAliasF0 = 2500.0;
constexpr int kAliasSettle = 9600; // 200 ms to clear the filter transient
constexpr int kAliasFft = 32768;   // power of two for the radix-2 FFT
constexpr float kAliasAmp = 0.9f;
constexpr float kFullDrive = 0.5f; // PatchStruct clamp — the hottest setting
constexpr double kAliasToleranceHz = 20.0;

std::vector<float> makeAliasTone(int n) {
    std::vector<float> out(n);
    for (int i = 0; i < n; ++i)
        out[i] = kAliasAmp * static_cast<float>(std::sin(2.0 * 3.14159265358979 * kAliasF0 * i / kAliasSr));
    return out;
}

// Faithful copy of the pre-#431 TubeSat path (base-rate asymmetric tanh + 20 Hz
// DC blocker). Frozen on purpose: it is the non-oversampled baseline the test
// must discriminate against.
struct LegacyTubeSat {
    static constexpr float kDriveScale = 9.0f;
    static constexpr float kPosAsym = 1.10f;
    static constexpr float kNegAsym = 0.90f;

    float hpfCoeff{0.99f};
    float drivePos{1.0f};
    float driveNeg{1.0f};
    float normPos{1.0f};
    float normNeg{1.0f};
    float xPrev{0.0f};
    float yPrev{0.0f};

    void prepare(double sr, float drive) {
        const double r = 1.0 - (2.0 * 3.14159265358979 * 20.0) / sr;
        hpfCoeff = static_cast<float>(std::clamp(r, 0.0, 0.9999));
        setDrive(drive);
        reset();
    }
    void setDrive(float drive01) {
        const float d = std::clamp(drive01, 0.0f, 0.5f);
        const float k = 1.0f + d * kDriveScale;
        drivePos = k * kPosAsym;
        driveNeg = k * kNegAsym;
        const float tp = std::tanh(drivePos);
        const float tn = std::tanh(driveNeg);
        normPos = (tp > 1e-6f) ? tp : 1.0f;
        normNeg = (tn > 1e-6f) ? tn : 1.0f;
    }
    void reset() {
        xPrev = 0.0f;
        yPrev = 0.0f;
    }
    float process(float x) {
        const float sat = (x >= 0.0f) ? std::tanh(x * drivePos) / normPos : std::tanh(x * driveNeg) / normNeg;
        const float hp = (sat - xPrev) + hpfCoeff * yPrev;
        xPrev = sat;
        yPrev = hp;
        return hp;
    }
};

std::vector<float> renderOversampledTubeSat(float drive) {
    TubeSat t;
    t.prepare(kAliasSr, 2);
    t.setDrive(drive);
    t.setMix(1.0f);
    const int total = kAliasSettle + kAliasFft;
    auto left = makeAliasTone(total);
    auto right = left;
    t.processStereo(left.data(), right.data(), total);
    return std::vector<float>(left.begin() + kAliasSettle, left.end());
}

std::vector<float> renderLegacyTubeSat(float drive) {
    LegacyTubeSat t;
    t.prepare(kAliasSr, drive);
    const int total = kAliasSettle + kAliasFft;
    auto left = makeAliasTone(total);
    for (int i = 0; i < total; ++i)
        left[i] = t.process(left[i]);
    return std::vector<float>(left.begin() + kAliasSettle, left.end());
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

TEST_CASE("TubeSat asymmetric: shapes positive and negative impulses differently", "[tubesat][phaseE]") {
    // Probe at a mid-range input level where tanh hasn't saturated yet.
    // At very high drive both ±x→±1 (the normalisation reaches its asymptote),
    // so asymmetry only shows on the linear-to-soft-knee portion of the curve.
    // Drive=0.2 + input=±0.3 keeps us in that zone.
    //
    // #431: the tanh now runs behind the 2x half-band pair, which smears a
    // single sample over the filter's group delay, so probe the peak of the
    // impulse response instead of the first output sample.
    TubeSat t;
    t.prepare(kSr, 2);
    t.setDrive(0.2f);
    t.setMix(1.0f);

    constexpr int kProbe = 256;
    auto peakFor = [&](float amplitude) {
        std::vector<float> probeL(kProbe, 0.0f);
        std::vector<float> probeR(kProbe, 0.0f);
        probeL[0] = amplitude;
        probeR[0] = amplitude;
        t.reset();
        t.processStereo(probeL.data(), probeR.data(), kProbe);
        float peak = 0.0f;
        for (float v : probeL) {
            if (std::fabs(v) > std::fabs(peak))
                peak = v;
        }
        return peak;
    };

    const float yPos = peakFor(0.3f);
    const float yNeg = peakFor(-0.3f);

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

// ---------------------------------------------------------------------------
// #431 — 2x oversampling suppresses aliasing folding into the passband
// ---------------------------------------------------------------------------

TEST_CASE("TubeSat — 2x oversampling suppresses inharmonic aliasing at full drive", "[tubesat][oversampling]") {
    const auto oversampled = renderOversampledTubeSat(kFullDrive);
    const auto legacy = renderLegacyTubeSat(kFullDrive);

    const double ratioOversampled =
        agentic_synth::test::inharmonicRatio(oversampled, kAliasF0, kAliasSr, kAliasToleranceHz);
    const double ratioLegacy = agentic_synth::test::inharmonicRatio(legacy, kAliasF0, kAliasSr, kAliasToleranceHz);

    INFO("oversampled inharmonic ratio = " << ratioOversampled);
    INFO("non-oversampled inharmonic ratio = " << ratioLegacy);

    // The baseline genuinely aliases, so the metric is measuring the right
    // thing rather than reporting FFT noise.
    REQUIRE(ratioLegacy > 0.02);
    // Documented bound: folded energy sits ≥ 40 dB below the fundamental.
    REQUIRE(ratioOversampled < 0.01);
    // Discriminating: the oversampled path strictly beats the base-rate path.
    REQUIRE(ratioOversampled < ratioLegacy);
}

TEST_CASE("Oversampler2x — upsample/downsample is unity-gain in the passband", "[tubesat][oversampling]") {
    // A tone well inside the half-band passband must survive the round trip at
    // (near) the same level: the pair is a reconstruction filter, not a shelf.
    agentic_synth::engine::Oversampler2x os;
    os.prepare(kAliasSr);
    constexpr double kToneHz = 500.0;
    constexpr float kAmp = 0.5f;

    double inSq = 0.0;
    double outSq = 0.0;
    int measured = 0;
    for (int i = 0; i < kAliasSettle + kAliasFft; ++i) {
        const float x = kAmp * static_cast<float>(std::sin(2.0 * 3.14159265358979 * kToneHz * i / kAliasSr));
        float up0 = 0.0f;
        float up1 = 0.0f;
        os.upsample(x, up0, up1);
        const float y = os.downsample(up0, up1);
        if (i >= kAliasSettle) {
            inSq += static_cast<double>(x) * static_cast<double>(x);
            outSq += static_cast<double>(y) * static_cast<double>(y);
            ++measured;
        }
    }
    const double gain =
        std::sqrt(outSq / static_cast<double>(measured)) / std::sqrt(inSq / static_cast<double>(measured));
    INFO("round-trip gain = " << gain);
    REQUIRE(std::fabs(gain - 1.0) < 0.02);
}
