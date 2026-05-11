#include <catch2/catch_test_macros.hpp>

#include "engine/WavetableOscillator.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>
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
