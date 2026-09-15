#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "engine/Reverb.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <limits>
#include <vector>

using namespace agentic_synth::engine;

namespace {

constexpr double kSR = 44100.0;
constexpr double kTwoPi = 6.283185307179586;

float rms(const float* x, std::size_t n) {
    double acc = 0.0;
    for (std::size_t i = 0; i < n; ++i)
        acc += static_cast<double>(x[i]) * x[i];
    return static_cast<float>(std::sqrt(acc / static_cast<double>(std::max<std::size_t>(1, n))));
}

// Render numSamples to outL/outR with constant input (default zero).
void render(Reverb& rev, std::vector<float>& outL, std::vector<float>& outR, std::size_t numSamples, float inL = 0.0f,
            float inR = 0.0f) {
    outL.assign(numSamples, 0.0f);
    outR.assign(numSamples, 0.0f);
    for (std::size_t i = 0; i < numSamples; ++i) {
        rev.process(inL, inR, outL[i], outR[i]);
    }
}

// Render a left-channel impulse response of `n` samples (impulse on sample 0).
std::vector<float> impulseResponse(Reverb& rev, std::size_t n) {
    std::vector<float> out(n, 0.0f);
    float l = 0.0f, r = 0.0f;
    rev.process(1.0f, 1.0f, l, r);
    out[0] = l;
    for (std::size_t i = 1; i < n; ++i) {
        rev.process(0.0f, 0.0f, l, r);
        out[i] = l;
    }
    return out;
}

// In-place iterative radix-2 Cooley-Tukey FFT. x.size() must be a power of two.
void fft(std::vector<std::complex<float>>& a) {
    const std::size_t n = a.size();
    for (std::size_t i = 1, j = 0; i < n; ++i) {
        std::size_t bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap(a[i], a[j]);
    }
    for (std::size_t len = 2; len <= n; len <<= 1) {
        const float ang = -static_cast<float>(kTwoPi) / static_cast<float>(len);
        const std::complex<float> wlen(std::cos(ang), std::sin(ang));
        for (std::size_t i = 0; i < n; i += len) {
            std::complex<float> w(1.0f, 0.0f);
            for (std::size_t j = 0; j < len / 2; ++j) {
                const std::complex<float> u = a[i + j];
                const std::complex<float> v = a[i + j + len / 2] * w;
                a[i + j] = u + v;
                a[i + j + len / 2] = u - v;
                w *= wlen;
            }
        }
    }
}

// Real cepstrum: IFFT(log|FFT(x)|). Comb filters with loop length L put a
// strong peak at quefrency L (and its multiples), so counting peaks in the
// comb-delay band measures how many distinct modal series the tail contains.
std::vector<float> realCepstrum(const std::vector<float>& x) {
    const std::size_t n = x.size();
    std::vector<std::complex<float>> a(n);
    for (std::size_t t = 0; t < n; ++t) {
        const float win =
            0.5f - 0.5f * std::cos(static_cast<float>(kTwoPi * static_cast<double>(t) / static_cast<double>(n)));
        a[t] = std::complex<float>(x[t] * win, 0.0f);
    }
    fft(a);
    for (std::size_t k = 0; k < n; ++k)
        a[k] = std::complex<float>(std::log(std::abs(a[k]) + 1e-12f), 0.0f);

    // Inverse FFT via conjugate symmetry: real part of conj(FFT(conj(a))) / n.
    for (std::complex<float>& v : a)
        v = std::conj(v);
    fft(a);

    std::vector<float> c(n, 0.0f);
    for (std::size_t k = 0; k < n; ++k)
        c[k] = std::real(a[k]) / static_cast<float>(n);
    return c;
}

// Count resolved peaks of the cepstrum in [lo, hi] quefrency (samples) that
// rise to at least `rel` of the band's maximum.
std::size_t countCepstralPeaks(const std::vector<float>& c, std::size_t lo, std::size_t hi, float rel) {
    const std::size_t k0 = std::max<std::size_t>(1, std::min(lo, c.size() - 2));
    const std::size_t k1 = std::min(hi, c.size() - 2);
    float maxv = 0.0f;
    for (std::size_t k = k0; k <= k1; ++k)
        maxv = std::max(maxv, c[k]);

    std::size_t peaks = 0;
    for (std::size_t k = k0; k <= k1; ++k) {
        if (c[k] >= rel * maxv && c[k] > c[k - 1] && c[k] >= c[k + 1])
            ++peaks;
    }
    return peaks;
}

// High-frequency decay proxy: first-difference (+6 dB/oct) RMS in a late window
// divided by an early window. Lower = faster HF decay.
float hfDecayRatio(const std::vector<float>& x) {
    std::vector<float> diff(x.size(), 0.0f);
    for (std::size_t i = 1; i < x.size(); ++i)
        diff[i] = x[i] - x[i - 1];

    auto bandRms = [&](double t0, double t1) {
        const std::size_t a = static_cast<std::size_t>(t0 * kSR);
        const std::size_t b = std::min(x.size(), static_cast<std::size_t>(t1 * kSR));
        return rms(diff.data() + a, b - a);
    };
    const float early = bandRms(0.05, 0.15);
    const float late = bandRms(0.30, 0.45);
    return (early > 0.0f) ? (late / early) : 0.0f;
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
        return 0.5f * (rms(outL.data() + tailStart, tailLen) + rms(outR.data() + tailStart, tailLen));
    };

    const float small = measureTailRms(0.2f);
    const float large = measureTailRms(0.9f);

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

// ---------------------------------------------------------------------------
// 7) size=1.0 impulse tail has a dense modal spectrum (8-comb Freeverb).
// ---------------------------------------------------------------------------
TEST_CASE("Reverb size=1.0 tail has dense modal spectrum", "[reverb]") {
    Reverb rev;
    rev.prepare(kSR);
    rev.setSize(1.0f);
    rev.setDamp(0.5f);
    rev.setMix(1.0f);
    rev.reset();

    const std::size_t skip = 4096; // ignore direct/early transient
    const std::size_t n = 16384;   // ~372 ms analysis window
    const auto out = impulseResponse(rev, skip + n);
    const std::vector<float> seg(out.begin() + static_cast<std::ptrdiff_t>(skip), out.end());
    const auto cep = realCepstrum(seg);

    // Comb loop lengths span 1116..1617 samples; each comb contributes one
    // cepstral peak there. Measured: 4-comb bank -> 4 peaks, restored 8-comb
    // bank -> 8 peaks. Threshold 6 sits between them and tolerates one
    // merged/marginal peak from platform FFT rounding.
    const std::size_t peaks = countCepstralPeaks(cep, 1050, 1700, 0.4f);
    constexpr std::size_t kMinModalPeaks = 6;
    REQUIRE(peaks >= kMinModalPeaks);
}

// ---------------------------------------------------------------------------
// 8) damping=1.0 darkens the tail — HF decays much faster than damping=0.
// ---------------------------------------------------------------------------
TEST_CASE("Reverb damping=1.0 darkens the high-frequency tail", "[reverb]") {
    auto tail = [](float damp) {
        Reverb rev;
        rev.prepare(kSR);
        rev.setSize(1.0f);
        rev.setDamp(damp);
        rev.setMix(1.0f);
        rev.reset();
        return impulseResponse(rev, static_cast<std::size_t>(kSR * 0.6));
    };

    const auto bright = tail(0.0f);
    const auto dark = tail(1.0f);
    const float brightRatio = hfDecayRatio(bright);
    const float darkRatio = hfDecayRatio(dark);
    const std::size_t lateStart = static_cast<std::size_t>(kSR * 0.2);
    const float darkLateRms = rms(dark.data() + lateStart, dark.size() - lateStart);

    // Measured (8-comb bank): damp=0.0 -> HF decay ratio 0.84; damp=1.0 ->
    // 0.0014 after the rescale (the old Freeverb *0.5 cap only reached 0.056).
    // Thresholds: the HF tail must fall below 2% of its early level, decay
    // >20x faster than the bright setting, and the dark tail must remain
    // audible rather than collapsing to silence.
    constexpr float kMaxDarkHfRatio = 0.02f;
    constexpr float kDarkTailFloor = 5e-4f;
    REQUIRE(darkRatio < kMaxDarkHfRatio);
    REQUIRE(darkRatio < brightRatio * 0.05f);
    REQUIRE(darkLateRms > kDarkTailFloor);
}
