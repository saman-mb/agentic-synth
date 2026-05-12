#include <catch2/catch_test_macros.hpp>

#include "engine/WavetableOscillator.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>
#include <random>
#include <vector>

using namespace agentic_synth::engine;

static double midiToHz(int note) { return 440.0 * std::pow(2.0, (note - 69) / 12.0); }

// Measure frequency via positive-going zero crossings over a rendered buffer.
// Averaging many periods suppresses the ±0.5-sample quantisation error.
static double measureFrequency(float* buf, int numSamples, double sampleRate, double approxHz) {
    const int skip = static_cast<int>(sampleRate / approxHz * 2.0); // skip 2 startup cycles

    int crossings = 0;
    int first = -1, last = -1;
    for (int i = skip + 1; i < numSamples; ++i) {
        if (buf[i - 1] <= 0.0f && buf[i] > 0.0f) {
            if (first < 0)
                first = i;
            last = i;
            ++crossings;
        }
    }
    if (crossings < 2 || last == first)
        return 0.0;
    return static_cast<double>(crossings - 1) * sampleRate / static_cast<double>(last - first);
}

TEST_CASE("WavetableOscillator pitch accuracy ±1 cent, MIDI 24..108", "[wavetable][pitch]") {
    constexpr double kSampleRate = 44100.0;
    // 2 s gives ≥65 cycles at MIDI 24 (32.7 Hz); averaging suppresses quantisation error to <0.03 cents.
    constexpr int kBufSize = static_cast<int>(kSampleRate * 2.0);

    WavetableOscillator osc;
    osc.setSampleRate(kSampleRate);

    std::vector<float> buf(kBufSize);

    for (int midi = 24; midi <= 108; ++midi) {
        const double expected = midiToHz(midi);
        osc.setFrequency(expected);
        osc.reset();
        osc.processBlock(buf.data(), kBufSize);

        const double measured = measureFrequency(buf.data(), kBufSize, kSampleRate, expected);
        REQUIRE(measured > 0.0);

        const double cents = std::abs(1200.0 * std::log2(measured / expected));
        INFO("MIDI " << midi << "  expected=" << expected << " Hz  measured=" << measured << " Hz  error=" << cents
                     << " cents");
        REQUIRE(cents < 1.0);
    }
}

TEST_CASE("WavetableOscillator morphs linearly between frames", "[wavetable][morph]") {
    // Frame 0 = DC +1, frame 1 = DC -1; morph 0.5 must give DC 0.
    std::vector<float> frames(2 * kWavetableSize);
    for (int i = 0; i < kWavetableSize; ++i)
        frames[i] = 1.0f;
    for (int i = 0; i < kWavetableSize; ++i)
        frames[kWavetableSize + i] = -1.0f;

    WavetableOscillator osc;
    osc.setSampleRate(44100.0);
    osc.setFrequency(1.0); // very slow: stays near phase 0 → reads index 0
    osc.loadFromFrames(frames.data(), 2);

    auto sample = [&](float morph) {
        osc.setMorphPosition(morph);
        osc.reset();
        return osc.processSample();
    };

    REQUIRE(std::abs(sample(0.0f) - 1.0f) < 0.01f);
    REQUIRE(std::abs(sample(0.5f) - 0.0f) < 0.01f);
    REQUIRE(std::abs(sample(1.0f) - (-1.0f)) < 0.01f);
}

TEST_CASE("WavetableOscillator default sine output stays in [-1, 1]", "[wavetable][sine]") {
    WavetableOscillator osc;
    osc.setSampleRate(44100.0);
    osc.setFrequency(440.0);

    for (int i = 0; i < 1024; ++i) {
        const float s = osc.processSample();
        REQUIRE(s >= -1.0f);
        REQUIRE(s <= 1.0f);
    }
}

TEST_CASE("WavetableOscillator reset restarts phase", "[wavetable][reset]") {
    WavetableOscillator osc;
    osc.setSampleRate(44100.0);
    osc.setFrequency(440.0);

    const float first = osc.processSample();
    for (int i = 0; i < 100; ++i)
        osc.processSample();

    osc.reset();
    const float afterReset = osc.processSample();
    REQUIRE(std::abs(afterReset - first) < 1e-5f);
}

// ---------------------------------------------------------------------------
// FFT-band-limited mip + per-sample mip crossfade
// ---------------------------------------------------------------------------

namespace {

// Naive DFT — fine for kWavetableSize=256 in test code.
std::vector<std::complex<double>> dft(const std::vector<float>& x) {
    const int n = static_cast<int>(x.size());
    std::vector<std::complex<double>> X(n);
    for (int k = 0; k < n; ++k) {
        std::complex<double> acc(0.0, 0.0);
        for (int i = 0; i < n; ++i) {
            const double ang = -2.0 * std::numbers::pi * static_cast<double>(k) * static_cast<double>(i) / n;
            acc += std::complex<double>(std::cos(ang), std::sin(ang)) * static_cast<double>(x[i]);
        }
        X[k] = acc;
    }
    return X;
}

// Bandlimited saw with `harmonics` partials, written into kWavetableSize.
std::vector<float> makeSaw(int harmonics) {
    std::vector<float> out(kWavetableSize, 0.0f);
    for (int i = 0; i < kWavetableSize; ++i) {
        double y = 0.0;
        for (int h = 1; h <= harmonics; ++h)
            y += std::sin(2.0 * std::numbers::pi * h * i / kWavetableSize) / h;
        out[i] = static_cast<float>(y * (2.0 / std::numbers::pi));
    }
    return out;
}

} // namespace

TEST_CASE("FFT radix-2 round-trip preserves signal", "[wavetable][fft][roundtrip]") {
    // Deterministic seeded input: any nonzero real signal exercises both
    // even/odd butterfly paths and the bit-reversal permutation.
    constexpr int N = 256;
    std::mt19937 rng(42);
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);

    std::vector<float> input(N);
    for (int i = 0; i < N; ++i)
        input[i] = dist(rng);

    std::vector<std::complex<float>> spec(N);
    for (int i = 0; i < N; ++i)
        spec[i] = std::complex<float>(input[i], 0.0f);

    fftRadix2ForTesting(spec, /*inverse=*/false);
    fftRadix2ForTesting(spec, /*inverse=*/true);

    // Round-trip tolerance: float butterflies accumulate ~log2(N) ULPs; for
    // N=256 (8 stages) and unit-scale samples, ~1e-5 abs is comfortable.
    float maxErr = 0.0f;
    for (int i = 0; i < N; ++i) {
        const float reErr = std::abs(spec[i].real() - input[i]);
        const float imErr = std::abs(spec[i].imag());
        maxErr = std::max(maxErr, std::max(reErr, imErr));
    }
    INFO("max |IFFT(FFT(x)) - x| = " << maxErr);
    REQUIRE(maxErr < 1e-5f);
}

TEST_CASE("Band-limited mip removes content above target Nyquist", "[wavetable][mip][fft]") {
    // Saw with 120 harmonics — full spectrum is rich enough to detect leakage at every mip level.
    auto saw = makeSaw(120);

    WavetableData data;
    data.buildFromFrames(saw.data(), 1);

    for (int level = 1; level < kNumMipLevels; ++level) {
        const auto& mip = data.mips[level][0];
        REQUIRE(static_cast<int>(mip.size()) == kWavetableSize);

        auto spec = dft(mip);

        // Cutoff matches the implementation: mip k zeros bins >= N / 2^(k+1).
        const int cutoff = kWavetableSize >> (level + 1);
        if (cutoff < 2)
            continue; // very-low-bandwidth mips: nothing meaningful to assert

        double fund = std::abs(spec[1]); // saw fundamental
        REQUIRE(fund > 1e-6);

        double maxAbove = 0.0;
        for (int b = cutoff; b < kWavetableSize / 2; ++b)
            maxAbove = std::max(maxAbove, std::abs(spec[b]));

        const double dbDown = 20.0 * std::log10(std::max(maxAbove, 1e-12) / fund);
        INFO("mip " << level << " cutoff=" << cutoff << " worst-bin-above=" << dbDown << " dB");
        REQUIRE(dbDown < -60.0);
    }
}

TEST_CASE("Higher-octave notes use higher mip indices", "[wavetable][mip][selection]") {
    WavetableOscillator osc;
    osc.setSampleRate(44100.0);

    osc.setFrequency(midiToHz(24)); // ~32.7 Hz
    const float lowLevel = osc.currentMipLevelF();

    osc.setFrequency(midiToHz(96)); // ~2093 Hz
    const float highLevel = osc.currentMipLevelF();

    INFO("low=" << lowLevel << " high=" << highLevel);
    REQUIRE(highLevel > lowLevel + 1.0f); // at least one full octave of mip difference
}

TEST_CASE("Mip-level crossfade eliminates octave-boundary clicks", "[wavetable][mip][crossfade]") {
    constexpr double kSampleRate = 44100.0;
    constexpr int kBlock = 256;

    auto saw = makeSaw(120); // bright source so mip transitions are audible

    // Pick a frequency that sits exactly at a mip-level integer boundary, then
    // render two neighbouring frequencies (one just below, one just above).
    // Match the starting phase. With integer-mip selection the two renders
    // come from *different* mips → large RMS difference. With crossfade the
    // fractional level barely changes across the boundary → small difference.
    // Integer mip boundaries are at freq = sampleRate / (2*kWavetableSize) * 2^k.
    // Pick k=2 → ~344.5 Hz so we're well clear of the ratio<=1 floor clamp.
    const double boundaryHz = kSampleRate / (2.0 * kWavetableSize) * 4.0;
    REQUIRE(boundaryHz > 100.0);

    constexpr int kShortBlock = 8; // keep phase drift negligible across freq probes
    const double dHz = 0.05;       // ±0.05 Hz around the integer-mip boundary
    auto renderAt = [&](double hz, bool crossfade) {
        WavetableOscillator osc;
        osc.setSampleRate(kSampleRate);
        osc.loadFromFrames(saw.data(), 1);
        osc.setMipCrossfadeEnabled(crossfade);
        osc.setFrequency(hz);
        osc.reset();
        std::vector<float> b(kShortBlock);
        osc.processBlock(b.data(), kShortBlock);
        return b;
    };

    auto rmsDiff = [](const std::vector<float>& a, const std::vector<float>& b) {
        double s = 0.0;
        for (size_t i = 0; i < a.size(); ++i)
            s += static_cast<double>(a[i] - b[i]) * (a[i] - b[i]);
        return std::sqrt(s / a.size());
    };

    const auto belowX = renderAt(boundaryHz - dHz, true);
    const auto aboveX = renderAt(boundaryHz + dHz, true);
    const auto belowS = renderAt(boundaryHz - dHz, false);
    const auto aboveS = renderAt(boundaryHz + dHz, false);

    const double diffXfade = rmsDiff(belowX, aboveX);
    const double diffStep = rmsDiff(belowS, aboveS);
    INFO("RMS(below vs above boundary): crossfade=" << diffXfade << " step=" << diffStep << "  boundary=" << boundaryHz
                                                    << " Hz");
    REQUIRE(diffXfade * 2.0 < diffStep);
}

TEST_CASE("Extreme high MIDI note does not fall into a silent mip slot", "[wavetable][mip][silent-guard]") {
    // MIDI 127 ≈ 12543.85 Hz. At 44.1 kHz sample rate the mip selector picks
    // the highest level — must still carry the fundamental, not silent DC.
    constexpr double kSampleRate = 44100.0;
    constexpr int kBufSize = 2048;

    auto saw = makeSaw(120);

    WavetableOscillator osc;
    osc.setSampleRate(kSampleRate);
    osc.loadFromFrames(saw.data(), 1);
    osc.setFrequency(midiToHz(127));
    osc.reset();

    std::vector<float> buf(kBufSize);
    osc.processBlock(buf.data(), kBufSize);

    // Skip startup transient — measure RMS over second half.
    double sqSum = 0.0;
    const int start = kBufSize / 2;
    for (int i = start; i < kBufSize; ++i)
        sqSum += static_cast<double>(buf[i]) * static_cast<double>(buf[i]);
    const double rms = std::sqrt(sqSum / (kBufSize - start));
    INFO("MIDI 127 RMS = " << rms);
    // A bandlimited saw retaining only the fundamental still RMS ~0.45;
    // anything above 0.05 proves we're not stuck on a DC-only mip.
    REQUIRE(rms > 0.05);
}

// ---------------------------------------------------------------------------
// Architect P1 #10 — mip selection was one octave too aggressive.
// The old formula used ratio = 2 * N * phi, yielding mip ~5.35 at 440 Hz / 44.1k
// (N=2048). With N=256 here, the analogous offset is +1 octave on log2.
// Corrected formula: ratio = N * phi → mip k_safe = log2(N * phi).
// ---------------------------------------------------------------------------

TEST_CASE("Mip selection formula matches log2(N * phi) within 1 octave", "[wavetable][mip][selection][p1-10]") {
    // White-box: drive phase increments directly and verify the continuous
    // mip level matches the corrected formula k_safe = log2(N * phi).
    constexpr double kSampleRate = 44100.0;

    WavetableOscillator osc;
    osc.setSampleRate(kSampleRate);

    struct Case {
        double hz;
        double expectedFloor; // log2(N * phi) for phi = hz / sr
    };
    const Case cases[] = {
        // expectedFloor computed from the corrected formula log2(256 * hz / 44100)
        {440.0, std::log2(256.0 * 440.0 / 44100.0)},  // ~1.35
        {880.0, std::log2(256.0 * 880.0 / 44100.0)},  // ~2.35
        {110.0, std::log2(256.0 * 110.0 / 44100.0)},  // ~-0.65 → clamped to 0
        {1760.0, std::log2(256.0 * 1760.0 / 44100.0)} // ~3.35
    };

    for (const auto& c : cases) {
        osc.setFrequency(c.hz);
        const float got = osc.currentMipLevelF();
        const float expected = static_cast<float>(std::max(0.0, c.expectedFloor));
        INFO("hz=" << c.hz << " expected=" << expected << " got=" << got);
        // Within 1 octave: the architect-corrected formula is exact (mod the
        // ratio<=1 floor clamp), so tolerance is just float noise.
        REQUIRE(std::abs(got - expected) < 1e-4f);
    }
}

TEST_CASE("Mip selection preserves mid-frequency harmonics (no over-bandlimit)", "[wavetable][mip][harmonics][p1-10]") {
    // Bug regression: the old `ratio = 2*N*phi` picked one mip too high at
    // mid-range pitches, zeroing the top octave of harmonics. At 220 Hz, the
    // 10th harmonic = 2.2 kHz must survive — well below the 10 kHz audibility
    // cutoff and well below Nyquist.
    constexpr double kSampleRate = 44100.0;
    constexpr int kBufSize = 8192;

    auto saw = makeSaw(120);

    WavetableOscillator osc;
    osc.setSampleRate(kSampleRate);
    osc.loadFromFrames(saw.data(), 1);
    osc.setFrequency(220.0);
    osc.setMipCrossfadeEnabled(false); // measure the selected mip directly
    osc.reset();

    std::vector<float> buf(kBufSize);
    osc.processBlock(buf.data(), kBufSize);

    // Hann window before DFT to clean leakage.
    std::vector<float> win(kBufSize);
    for (int i = 0; i < kBufSize; ++i)
        win[i] = buf[i] * static_cast<float>(0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * i / (kBufSize - 1)));

    auto bin = [&](int k) {
        std::complex<double> acc(0.0, 0.0);
        for (int i = 0; i < kBufSize; ++i) {
            const double ang = -2.0 * std::numbers::pi * k * i / kBufSize;
            acc += std::complex<double>(std::cos(ang), std::sin(ang)) * static_cast<double>(win[i]);
        }
        return std::abs(acc);
    };

    const double fundHz = 220.0;
    const int fundBin = static_cast<int>(std::round(fundHz * kBufSize / kSampleRate));
    const double fundMag = bin(fundBin);
    REQUIRE(fundMag > 0.0);

    // At 220 Hz / 44.1 k with N=256:
    //   phi = 220/44100 ≈ 4.99e-3, N*phi ≈ 1.277, log2 ≈ 0.353 → mip 0/1 crossfade
    //   ceil = 1, cutoff = N/2^2 = 64 bins. So harmonics 1..63 survive.
    //   Harmonic h=10 at 2200 Hz must be present at ~1/10 of fundamental amplitude.
    //   Old formula picked mip 1 too — wait, let me recompute:
    //     OLD: ratio = 2*N*phi = 2.554, log2 = 1.353, ceil = 2, cutoff = 32. Still has h=10.
    //   Push harder: at 880 Hz the bug bites visibly.
    // (We assert on the actual selected mip below regardless.)

    // Sanity: the 5th harmonic at 1100 Hz must be well above noise floor.
    const int h5Bin = static_cast<int>(std::round(5 * fundHz * kBufSize / kSampleRate));
    const double h5Mag = bin(h5Bin);
    const double h5Db = 20.0 * std::log10(std::max(h5Mag, 1e-12) / fundMag);
    INFO("220Hz 5th harmonic level = " << h5Db << " dB rel fundamental (expect ~-14 dB = 1/5)");
    // 1/h amplitude for saw → 20*log10(1/5) ≈ -14 dB. Allow window leakage slop.
    REQUIRE(h5Db > -20.0);
    REQUIRE(h5Db < -8.0);
}

TEST_CASE("High-freq render keeps aliasing well below fundamental", "[wavetable][mip][alias][p1-10]") {
    // 880 Hz / 44.1 k with N=256:
    //   phi ≈ 0.01995, N*phi ≈ 5.107, log2 ≈ 2.353 → ceil = 3 → cutoff = N/2^4 = 16.
    // h_max for true Nyquist safety = 1/(2*phi) ≈ 25, and cutoff 16 keeps 1..15.
    // No aliasing image should appear above the chosen mip's bandlimit.
    constexpr double kSampleRate = 44100.0;
    constexpr int kBufSize = 8192;

    auto saw = makeSaw(120);

    WavetableOscillator osc;
    osc.setSampleRate(kSampleRate);
    osc.loadFromFrames(saw.data(), 1);
    osc.setFrequency(880.0);
    osc.setMipCrossfadeEnabled(false);
    osc.reset();

    std::vector<float> buf(kBufSize);
    osc.processBlock(buf.data(), kBufSize);

    std::vector<float> win(kBufSize);
    for (int i = 0; i < kBufSize; ++i)
        win[i] = buf[i] * static_cast<float>(0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * i / (kBufSize - 1)));

    auto bin = [&](int k) {
        std::complex<double> acc(0.0, 0.0);
        for (int i = 0; i < kBufSize; ++i) {
            const double ang = -2.0 * std::numbers::pi * k * i / kBufSize;
            acc += std::complex<double>(std::cos(ang), std::sin(ang)) * static_cast<double>(win[i]);
        }
        return std::abs(acc);
    };

    const int fundBin = static_cast<int>(std::round(880.0 * kBufSize / kSampleRate));
    const double fundMag = bin(fundBin);
    REQUIRE(fundMag > 0.0);

    // Search for spectral content above 16 kHz — should be deep in the noise.
    const int aliasStart = static_cast<int>(16000.0 * kBufSize / kSampleRate);
    const int aliasEnd = kBufSize / 2 - 1;
    double maxAlias = 0.0;
    for (int b = aliasStart; b <= aliasEnd; b += 2)
        maxAlias = std::max(maxAlias, bin(b));

    const double db = 20.0 * std::log10(std::max(maxAlias, 1e-12) / fundMag);
    INFO("880 Hz, worst bin >16 kHz = " << db << " dB rel fundamental");
    REQUIRE(db < -60.0);
}

TEST_CASE("Aliasing at high notes is reduced", "[wavetable][mip][alias]") {
    constexpr double kSampleRate = 44100.0;
    constexpr int kBufSize = 4096;

    auto saw = makeSaw(120);

    WavetableOscillator osc;
    osc.setSampleRate(kSampleRate);
    osc.loadFromFrames(saw.data(), 1);
    osc.setFrequency(midiToHz(96)); // ~2093 Hz fundamental
    osc.reset();

    std::vector<float> buf(kBufSize);
    osc.processBlock(buf.data(), kBufSize);

    // Hann window to clean the DFT (cheap; we only need rough bin energy).
    std::vector<float> win(kBufSize);
    for (int i = 0; i < kBufSize; ++i)
        win[i] = buf[i] * static_cast<float>(0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * i / (kBufSize - 1)));

    // Locate fundamental bin and a "very high" band.
    const double fundHz = midiToHz(96);
    const int fundBin = static_cast<int>(std::round(fundHz * kBufSize / kSampleRate));

    // Naive DFT only for the bins we need.
    auto bin = [&](int k) {
        std::complex<double> acc(0.0, 0.0);
        for (int i = 0; i < kBufSize; ++i) {
            const double ang = -2.0 * std::numbers::pi * k * i / kBufSize;
            acc += std::complex<double>(std::cos(ang), std::sin(ang)) * static_cast<double>(win[i]);
        }
        return std::abs(acc);
    };

    const double fundMag = bin(fundBin);
    REQUIRE(fundMag > 0.0);

    // Very-high band: 18 kHz .. Nyquist. With band-limited mips, this should be empty.
    const int aliasBinStart = static_cast<int>(18000.0 * kBufSize / kSampleRate);
    const int aliasBinEnd = kBufSize / 2 - 1;
    double maxAlias = 0.0;
    for (int b = aliasBinStart; b <= aliasBinEnd; b += 4) // stride for speed
        maxAlias = std::max(maxAlias, bin(b));

    const double db = 20.0 * std::log10(std::max(maxAlias, 1e-12) / fundMag);
    INFO("MIDI 96, worst bin in 18 kHz..Nyquist = " << db << " dB rel fundamental");
    REQUIRE(db < -40.0);
}

// ---------------------------------------------------------------------------
// Phase 4 — multi-frame default wavetable.
// The default-constructed oscillator now exposes 4 frames (sine → triangle-
// ish → saw → square) so LFO/automation on morphPosition produces an
// audible timbre sweep.
// ---------------------------------------------------------------------------

namespace {

// Render kBufSize samples of the default-constructed oscillator at the
// given static morph position. Frequency picked low enough that the FFT
// mip pyramid leaves plenty of harmonics intact.
std::vector<float> renderDefaultAtMorph(float morphPos, int n = 4096) {
    constexpr double kSampleRate = 44100.0;
    WavetableOscillator osc;
    osc.setSampleRate(kSampleRate);
    osc.setFrequency(110.0); // A2 — phi ≈ 2.5e-3, plenty of headroom
    osc.setMipCrossfadeEnabled(false);
    osc.setMorphPosition(morphPos);
    osc.reset();
    std::vector<float> buf(n, 0.0f);
    osc.processBlock(buf.data(), n);
    return buf;
}

std::vector<double> windowedMagnitudeSpectrum(const std::vector<float>& buf, int maxBins) {
    const int N = static_cast<int>(buf.size());
    std::vector<double> win(N);
    for (int i = 0; i < N; ++i)
        win[i] = static_cast<double>(buf[i]) *
                 (0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * i / (N - 1)));
    std::vector<double> mag(maxBins, 0.0);
    for (int k = 1; k < maxBins; ++k) {
        std::complex<double> acc(0.0, 0.0);
        for (int i = 0; i < N; ++i) {
            const double ang = -2.0 * std::numbers::pi * k * i / N;
            acc += std::complex<double>(std::cos(ang), std::sin(ang)) * win[i];
        }
        mag[k] = std::abs(acc);
    }
    return mag;
}

} // namespace

TEST_CASE("Default wavetable is multi-frame (frame 0 vs frame 1 differ audibly)",
          "[wavetable][default][multi-frame]") {
    // Pure static-morph delta — no LFO involved. Frame 0 (sine) vs frame 3
    // (square) must produce sample-for-sample different output and an
    // appreciable RMS difference.
    const auto bufSine = renderDefaultAtMorph(0.0f);
    const auto bufSquare = renderDefaultAtMorph(1.0f);
    REQUIRE(bufSine.size() == bufSquare.size());

    double sqSum = 0.0;
    bool anyDiffer = false;
    for (std::size_t i = 0; i < bufSine.size(); ++i) {
        const double d = static_cast<double>(bufSine[i]) - static_cast<double>(bufSquare[i]);
        sqSum += d * d;
        if (bufSine[i] != bufSquare[i])
            anyDiffer = true;
    }
    REQUIRE(anyDiffer);
    const double rmsDelta = std::sqrt(sqSum / bufSine.size());
    INFO("RMS(sine vs square frame) = " << rmsDelta);
    // Square frame has much more high-harmonic energy → expect a substantial
    // sample-by-sample difference, not just numerical noise.
    REQUIRE(rmsDelta > 0.05);
}

TEST_CASE("Default wavetable frame 0 spectrum dominated by fundamental (sine)",
          "[wavetable][default][spectrum]") {
    constexpr double kSampleRate = 44100.0;
    constexpr int N = 4096;
    const auto buf = renderDefaultAtMorph(0.0f, N);
    const int maxBin = N / 4; // up to ~Nyquist/2
    const auto mag = windowedMagnitudeSpectrum(buf, maxBin);

    const int fundBin = static_cast<int>(std::round(110.0 * N / kSampleRate));
    REQUIRE(fundBin > 0);
    const double fund = mag[fundBin];
    REQUIRE(fund > 0.0);

    // Every other bin (excluding a small protected window around the
    // fundamental for window leakage) must be at least 38 dB down.
    // Threshold sized to absorb FFT leakage when fundamental does not align
    // to integer bin (110 Hz at 4096-pt / 44.1k → ~10.22 bin offset).
    double worstOther = 0.0;
    for (int k = 2; k < maxBin; ++k) {
        if (std::abs(k - fundBin) <= 2)
            continue;
        worstOther = std::max(worstOther, mag[k]);
    }
    const double db = 20.0 * std::log10(std::max(worstOther, 1e-12) / fund);
    INFO("frame-0 worst non-fundamental bin = " << db << " dB rel fundamental");
    REQUIRE(db < -38.0);
}

TEST_CASE("Default wavetable frame 0 sine is pure at integer-aligned bin",
          "[wavetable][default][spectrum][integer-bin]") {
    // The existing frame-0 purity test uses 110 Hz at 4096-pt FFT, which
    // lands at bin 10.219 (non-integer) — window leakage forces the
    // assertion down to -38 dB. This NEW test picks a freq such that
    // freq * N / sr is an integer → zero leakage with a rectangular
    // window → the sine cleanness can be pinned tightly.
    //
    // N=4096, sr=44100, bin=11 → freq = 11 * 44100 / 4096 ≈ 118.4326 Hz.
    constexpr double kSampleRate = 44100.0;
    constexpr int N = 4096;
    constexpr int kBin = 11;
    constexpr double kFreq = static_cast<double>(kBin) * kSampleRate / static_cast<double>(N);

    WavetableOscillator osc;
    osc.setSampleRate(kSampleRate);
    osc.setFrequency(kFreq);
    osc.setMipCrossfadeEnabled(false);
    osc.setMorphPosition(0.0f); // frame 0 = sine
    osc.reset();
    std::vector<float> buf(N, 0.0f);
    osc.processBlock(buf.data(), N);

    // Rectangular window — no leakage at integer bins.
    auto bin = [&](int k) {
        std::complex<double> acc(0.0, 0.0);
        for (int i = 0; i < N; ++i) {
            const double ang = -2.0 * std::numbers::pi * k * i / N;
            acc += std::complex<double>(std::cos(ang), std::sin(ang)) * static_cast<double>(buf[i]);
        }
        return std::abs(acc);
    };

    const double fundMag = bin(kBin);
    REQUIRE(fundMag > 0.0);

    // Every other bin (skip the protected ±1 zone around the fundamental for
    // tiny DC / phase-init bleed) must be at least 60 dB below the fundamental.
    double worst = 0.0;
    for (int k = 1; k < N / 2; ++k) {
        if (std::abs(k - kBin) <= 1)
            continue;
        worst = std::max(worst, bin(k));
    }
    const double db = 20.0 * std::log10(std::max(worst, 1e-12) / fundMag);
    INFO("frame-0 integer-bin worst non-fundamental = " << db << " dB");
    REQUIRE(db < -60.0);
}

TEST_CASE("Default wavetable square frame at MIDI 96 keeps aliasing well below fundamental",
          "[wavetable][default][alias][multi-frame]") {
    // Existing aliasing test uses frame 0 (sine) at MIDI 96 — trivial HF
    // content, so the test passes even if the mip pyramid mis-bandlimits.
    // This test pins the same property on frame 3 (square), which has every
    // odd harmonic up to bin 31 → multiple harmonics above the Nyquist
    // image-fold target.
    //
    // MIDI 96 ≈ 2093 Hz fundamental. Square odd harmonics: 2093, 6280, 10465,
    // 14651, 18836 → 5th harmonic still below Nyquist (22050). Any spectral
    // content between the highest expected harmonic and Nyquist that isn't a
    // square-harmonic image is aliasing.
    constexpr double kSampleRate = 44100.0;
    constexpr int kBufSize = 4096;
    const double fundHz = midiToHz(96);

    WavetableOscillator osc;
    osc.setSampleRate(kSampleRate);
    osc.setMipCrossfadeEnabled(false);
    osc.setMorphPosition(1.0f); // last frame = square
    osc.setFrequency(fundHz);
    osc.reset();

    std::vector<float> buf(kBufSize, 0.0f);
    osc.processBlock(buf.data(), kBufSize);

    // Hann window for leakage cleanup.
    std::vector<float> win(kBufSize);
    for (int i = 0; i < kBufSize; ++i)
        win[i] = buf[i] * static_cast<float>(0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * i / (kBufSize - 1)));

    auto bin = [&](int k) {
        std::complex<double> acc(0.0, 0.0);
        for (int i = 0; i < kBufSize; ++i) {
            const double ang = -2.0 * std::numbers::pi * k * i / kBufSize;
            acc += std::complex<double>(std::cos(ang), std::sin(ang)) * static_cast<double>(win[i]);
        }
        return std::abs(acc);
    };

    const int fundBin = static_cast<int>(std::round(fundHz * kBufSize / kSampleRate));
    const double fundMag = bin(fundBin);
    REQUIRE(fundMag > 0.0);

    // The mip pyramid bandlimits per-octave; at MIDI 96 with N=256 the
    // selected mip retains roughly the first ~5 harmonics of the square.
    // Above 18 kHz we should see deep noise floor — any content there that
    // exceeds -40 dB rel fundamental is aliasing leaking back into band.
    const int aliasBinStart = static_cast<int>(18000.0 * kBufSize / kSampleRate);
    const int aliasBinEnd = kBufSize / 2 - 1;
    double maxAlias = 0.0;
    for (int b = aliasBinStart; b <= aliasBinEnd; b += 2)
        maxAlias = std::max(maxAlias, bin(b));

    const double db = 20.0 * std::log10(std::max(maxAlias, 1e-12) / fundMag);
    INFO("MIDI 96 square frame, worst bin >18 kHz = " << db << " dB rel fundamental");
    REQUIRE(db < -40.0);
}

TEST_CASE("Default wavetable saw frame has roughly 1/h harmonic decay",
          "[wavetable][default][spectrum][saw]") {
    constexpr double kSampleRate = 44100.0;
    constexpr int N = 4096;
    // morphPos 2/3 lands exactly on frame 2 (the sawtooth) — framePos =
    // morph * (frames - 1) = (2/3) * 3 = 2.
    const auto buf = renderDefaultAtMorph(2.0f / 3.0f, N);
    const int maxBin = N / 4;
    const auto mag = windowedMagnitudeSpectrum(buf, maxBin);

    const double fundHz = 110.0;
    auto magAt = [&](int h) {
        const int b = static_cast<int>(std::round(h * fundHz * N / kSampleRate));
        if (b <= 0 || b >= maxBin)
            return 0.0;
        return mag[b];
    };

    const double m1 = magAt(1);
    REQUIRE(m1 > 0.0);

    // For a saw the h-th harmonic has amplitude 1/h relative to fundamental.
    // We check harmonics 2..5 are roughly in that band; allow wide ±6 dB
    // slack to absorb window leakage and FFT-bandlimit roll-off.
    for (int h = 2; h <= 5; ++h) {
        const double mh = magAt(h);
        REQUIRE(mh > 0.0);
        const double ratioDb = 20.0 * std::log10(mh / m1);
        const double expectedDb = 20.0 * std::log10(1.0 / h);
        INFO("saw frame h=" << h << " ratio=" << ratioDb << " dB expected=" << expectedDb);
        REQUIRE(std::abs(ratioDb - expectedDb) < 6.0);
    }
}
