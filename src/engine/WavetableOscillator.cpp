#include "engine/WavetableOscillator.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstring>
#include <fstream>
#include <numbers>

namespace agentic_synth::engine {

// ---- FFT (radix-2 in-place Cooley-Tukey, power-of-two sizes) ----
//
// Offline-only — used at mip-construction time. Audio thread never touches this.
// ~40 lines; avoids pulling in juce_dsp as a new engine-lib link dependency.

namespace {

using cpx = std::complex<float>;

void fftRadix2(std::vector<cpx>& a, bool inverse) {
    const int n = static_cast<int>(a.size());
    // Bit-reversal permutation.
    for (int i = 1, j = 0; i < n; ++i) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap(a[i], a[j]);
    }
    // Iterative butterflies.
    for (int len = 2; len <= n; len <<= 1) {
        const float ang = (inverse ? 2.0f : -2.0f) * std::numbers::pi_v<float> / static_cast<float>(len);
        const cpx wn(std::cos(ang), std::sin(ang));
        for (int i = 0; i < n; i += len) {
            cpx w(1.0f, 0.0f);
            for (int k = 0; k < len / 2; ++k) {
                const cpx u = a[i + k];
                const cpx v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wn;
            }
        }
    }
    if (inverse) {
        const float inv = 1.0f / static_cast<float>(n);
        for (auto& x : a)
            x *= inv;
    }
}

} // namespace

// Test-only shim — forwards to the anonymous-namespace implementation.
// Keeps the production hot path inlinable inside this TU while still letting
// tests verify FFT(IFFT) round-trip correctness without duplicating the kernel.
void fftRadix2ForTesting(std::vector<cpx>& a, bool inverse) { fftRadix2(a, inverse); }

// ---- WavetableData ----

void WavetableData::buildFromFrames(const float* samples, int numFrames) {
    frameCount = std::min(numFrames, kMaxWavetableFrames);
    // Mip 0 = source (full bandwidth, no DFT touch).
    for (int f = 0; f < frameCount; ++f) {
        mips[0][f].assign(samples + f * kWavetableSize, samples + f * kWavetableSize + kWavetableSize);
    }
    // Mips 1..N-1 = band-limited copies, all kWavetableSize samples.
    for (int level = 1; level < kNumMipLevels; ++level)
        buildMipLevel(level);
}

void WavetableData::buildMipLevel(int level) {
    // Bin cutoff: mip level k keeps DFT bins [0 .. kWavetableSize / 2^(k+1)),
    // matching the old (tableSize >> k)-sample Nyquist. Bin N/2 (real Nyquist
    // for a real-valued time signal) is folded into the cutoff window when k=0
    // but levels >= 1 always lie strictly below it.
    const int cutoff = kWavetableSize >> (level + 1);

    std::vector<cpx> spec(kWavetableSize);
    for (int f = 0; f < frameCount; ++f) {
        const auto& src = mips[0][f]; // bandlimit from the original, not the previous mip
        for (int i = 0; i < kWavetableSize; ++i)
            spec[i] = cpx(src[i], 0.0f);

        fftRadix2(spec, /*inverse=*/false);

        // Zero positive-frequency bins >= cutoff and their conjugate negatives.
        // Bins 0..cutoff-1 stay; bin cutoff and above (up to N-cutoff exclusive
        // for the mirror) are zeroed. We also zero bin N/2 (real Nyquist) when
        // cutoff <= N/2 to be safe — it carries no usable phase anyway.
        for (int b = cutoff; b <= kWavetableSize - cutoff; ++b)
            spec[b] = cpx(0.0f, 0.0f);

        fftRadix2(spec, /*inverse=*/true);

        auto& dst = mips[level][f];
        dst.resize(kWavetableSize);
        for (int i = 0; i < kWavetableSize; ++i)
            dst[i] = spec[i].real();
    }
}

// ---- WavetableOscillator ----

namespace {

// Build the shared default table once and cache it. Multi-frame so that
// LFO/automation on WavetablePos (setMorphPosition) produces an audible
// timbre sweep out of the box. Frames progress sine → triangle-ish →
// saw → square, normalised to peak ≈ 1.0 each.
//
// Memory: 4 frames × kWavetableSize (256) × kNumMipLevels (7) × 4 bytes
//        ≈ 28 KB total, shared across every default-constructed oscillator
// instance via shared_ptr — no per-voice multiplication.
std::shared_ptr<const WavetableData> buildDefaultTable() {
    constexpr int kFrames = 4;
    std::vector<float> frames(kFrames * kWavetableSize);

    auto norm = [](float* dst, int n) noexcept {
        float peak = 0.0f;
        for (int i = 0; i < n; ++i)
            peak = std::max(peak, std::abs(dst[i]));
        if (peak > 1e-9f) {
            const float inv = 1.0f / peak;
            for (int i = 0; i < n; ++i)
                dst[i] *= inv;
        }
    };

    // Frame 0 — pure sine.
    float* f0 = &frames[0 * kWavetableSize];
    for (int i = 0; i < kWavetableSize; ++i)
        f0[i] = static_cast<float>(std::sin(2.0 * std::numbers::pi * i / kWavetableSize));

    // Frame 1 — triangle-ish: odd harmonics with 1/h^2 amplitude (the band-
    // limited triangle series). Sounds noticeably brighter than the pure sine
    // but still quite soft.
    float* f1 = &frames[1 * kWavetableSize];
    for (int i = 0; i < kWavetableSize; ++i) {
        double y = 0.0;
        for (int h = 1; h <= 31; h += 2) {
            const double sign = ((h - 1) / 2) % 2 == 0 ? 1.0 : -1.0;
            y += sign * std::sin(2.0 * std::numbers::pi * h * i / kWavetableSize) / static_cast<double>(h * h);
        }
        f1[i] = static_cast<float>(y);
    }
    norm(f1, kWavetableSize);

    // Frame 2 — sawtooth: sum of 1/h harmonics up to a generous count. The
    // FFT mip pyramid further band-limits this per-octave at render time.
    float* f2 = &frames[2 * kWavetableSize];
    for (int i = 0; i < kWavetableSize; ++i) {
        double y = 0.0;
        for (int h = 1; h <= 64; ++h)
            y += std::sin(2.0 * std::numbers::pi * h * i / kWavetableSize) / static_cast<double>(h);
        f2[i] = static_cast<float>(y);
    }
    norm(f2, kWavetableSize);

    // Frame 3 — square: sum of odd 1/h harmonics. Strongest harmonic content
    // → maximum timbre contrast against frame 0.
    float* f3 = &frames[3 * kWavetableSize];
    for (int i = 0; i < kWavetableSize; ++i) {
        double y = 0.0;
        for (int h = 1; h <= 63; h += 2)
            y += std::sin(2.0 * std::numbers::pi * h * i / kWavetableSize) / static_cast<double>(h);
        f3[i] = static_cast<float>(y);
    }
    norm(f3, kWavetableSize);

    auto data = std::make_shared<WavetableData>();
    data->buildFromFrames(frames.data(), kFrames);
    return data;
}

const std::shared_ptr<const WavetableData>& defaultTable() {
    // Magic-static: one shared copy across every default-constructed
    // oscillator instance for the lifetime of the process. Construction
    // happens on first call (offline, allocates) — never on the audio thread.
    static const std::shared_ptr<const WavetableData> kTable = buildDefaultTable();
    return kTable;
}

} // namespace

WavetableOscillator::WavetableOscillator() { table_ = defaultTable(); }

void WavetableOscillator::setSampleRate(double sampleRate) noexcept {
    sampleRate_ = sampleRate < 1.0 ? 1.0 : sampleRate;
    phaseIncrement_ = frequency_ / sampleRate_;
}

void WavetableOscillator::setFrequency(double hz) noexcept {
    frequency_ = hz;
    phaseIncrement_ = frequency_ / sampleRate_;
}

void WavetableOscillator::setMorphPosition(float position) noexcept { morphPos_ = std::clamp(position, 0.0f, 1.0f); }

void WavetableOscillator::reset() noexcept { phase_ = 0.0; }

void WavetableOscillator::setTable(std::shared_ptr<const WavetableData> table) { table_ = std::move(table); }

void WavetableOscillator::loadFromFrames(const float* samples, int numFrames) {
    auto data = std::make_shared<WavetableData>();
    data->buildFromFrames(samples, numFrames);
    table_ = std::move(data);
}

float WavetableOscillator::mipLevelFloat() const noexcept {
    // For phase increment phi cycles/sample, the highest harmonic that fits below
    // Nyquist is h_max = 1/(2*phi). Mip k preserves bins below N/2^(k+1), so we
    // need N/2^(k+1) >= h_max + 1, giving the minimum safe k:
    //
    //   k_safe = log2(N / (h_max + 1)) - 1
    //          ≈ log2(2 * N * phi) - 1
    //          = log2(N * phi)
    //
    // Previously this used ratio = 2 * N * phi (i.e. log2 result was 1 too high),
    // which over-bandlimited the top octave of harmonics. Architect P1 #10.
    if (phaseIncrement_ <= 0.0)
        return 0.0f;
    const double ratio = static_cast<double>(kWavetableSize) * phaseIncrement_;
    if (ratio <= 1.0)
        return 0.0f;
    return static_cast<float>(std::log2(ratio));
}

float WavetableOscillator::currentMipLevelF() const noexcept { return mipLevelFloat(); }

float WavetableOscillator::readMorphedSample(int mipLevel, double phase) const noexcept {
    if (!table_ || table_->frameCount == 0)
        return 0.0f;

    // All mips are kWavetableSize samples now (FFT bandlimited, full resolution).
    constexpr int tableSize = kWavetableSize;

    const double tablePhase = phase * tableSize;
    const int idx0 = static_cast<int>(tablePhase) % tableSize;
    const int idx1 = (idx0 + 1) % tableSize;
    const float frac = static_cast<float>(tablePhase - std::floor(tablePhase));

    const int frameCount = table_->frameCount;
    const auto& mip = table_->mips[mipLevel];

    if (frameCount == 1) {
        const auto& frame = mip[0];
        if (static_cast<int>(frame.size()) < tableSize)
            return 0.0f;
        return frame[idx0] + frac * (frame[idx1] - frame[idx0]);
    }

    const float framePos = morphPos_ * static_cast<float>(frameCount - 1);
    const int frame0 = static_cast<int>(framePos);
    const int frame1 = std::min(frame0 + 1, frameCount - 1);
    const float frameFrac = framePos - static_cast<float>(frame0);

    const auto& f0 = mip[frame0];
    const auto& f1 = mip[frame1];
    if (static_cast<int>(f0.size()) < tableSize || static_cast<int>(f1.size()) < tableSize)
        return 0.0f;

    const float s0 = f0[idx0] + frac * (f0[idx1] - f0[idx0]);
    const float s1 = f1[idx0] + frac * (f1[idx1] - f1[idx0]);
    return s0 + frameFrac * (s1 - s0);
}

// Resolve crossfade endpoints given a continuous safe-mip threshold.
// k_safe = log2(N*phi) is the EXACT threshold: the minimum INTEGER mip that
// fully bandlimits is ceil(k_safe). The floor mip would alias. Therefore the
// crossfade pair is (ceil(k_safe), ceil(k_safe)+1) — both alias-safe — with
// weight rising toward the higher mip as we approach the next octave boundary.
namespace {
inline void resolveMipPair(float levelF, int& idxLow, int& idxHigh, float& weight) noexcept {
    const float ceilF = std::ceil(levelF);
    idxLow = std::clamp(static_cast<int>(ceilF), 0, kNumMipLevels - 1);
    idxHigh = std::clamp(idxLow + 1, 0, kNumMipLevels - 1);
    // weight on idxHigh = frac(levelF). At an exact integer levelF, ceil==floor
    // so weight=0 and we sit on idxLow; on both sides of an integer the
    // resolved pair shifts by exactly one mip with weight transitioning through
    // 0 → continuous output.
    weight = std::clamp(levelF - std::floor(levelF), 0.0f, 1.0f);
}
} // namespace

float WavetableOscillator::processSample() noexcept {
    const float levelF = mipLevelFloat();
    int idxLow, idxHigh;
    float fracLevel;
    resolveMipPair(levelF, idxLow, idxHigh, fracLevel);

    float out;
    if (!mipCrossfadeEnabled_ || idxLow == idxHigh) {
        // Pick the minimum-safe integer mip (ceil of the continuous threshold).
        out = readMorphedSample(idxLow, phase_);
    } else {
        const float sLow = readMorphedSample(idxLow, phase_);
        const float sHigh = readMorphedSample(idxHigh, phase_);
        out = sLow * (1.0f - fracLevel) + sHigh * fracLevel;
    }

    phase_ = std::fmod(phase_ + phaseIncrement_, 1.0);
    return out;
}

void WavetableOscillator::processBlock(float* output, int numSamples) noexcept {
    // mipLevelFloat() depends only on phaseIncrement, which is constant across
    // the block — pull the indices out of the inner loop.
    const float levelF = mipLevelFloat();
    int idxLow, idxHigh;
    float fracLevel;
    resolveMipPair(levelF, idxLow, idxHigh, fracLevel);
    const bool useCrossfade = mipCrossfadeEnabled_ && idxLow != idxHigh;

    for (int i = 0; i < numSamples; ++i) {
        if (useCrossfade) {
            const float sLow = readMorphedSample(idxLow, phase_);
            const float sHigh = readMorphedSample(idxHigh, phase_);
            output[i] = sLow * (1.0f - fracLevel) + sHigh * fracLevel;
        } else {
            output[i] = readMorphedSample(idxLow, phase_);
        }
        phase_ = std::fmod(phase_ + phaseIncrement_, 1.0);
    }
}

// ---- WAV loader ----

namespace {

bool parseWav(const std::string& path, std::vector<float>& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f)
        return false;

    // Determine file size once for chunk-bounds validation.
    f.seekg(0, std::ios::end);
    const auto fileSize = f.tellg();
    f.seekg(0);

    auto read4 = [&]() -> uint32_t {
        uint8_t b[4]{};
        f.read(reinterpret_cast<char*>(b), 4);
        return static_cast<uint32_t>(b[0]) | (static_cast<uint32_t>(b[1]) << 8) | (static_cast<uint32_t>(b[2]) << 16) |
               (static_cast<uint32_t>(b[3]) << 24);
    };
    auto read2 = [&]() -> uint16_t {
        uint8_t b[2]{};
        f.read(reinterpret_cast<char*>(b), 2);
        return static_cast<uint16_t>(b[0] | (b[1] << 8));
    };
    auto tag = [&]() -> std::string {
        char buf[4]{};
        f.read(buf, 4);
        return {buf, 4};
    };

    if (tag() != "RIFF")
        return false;
    read4(); // file size field (ignored — we use actual file size)
    if (tag() != "WAVE")
        return false;

    uint16_t audioFmt = 0, channels = 0, bitsPerSample = 0;
    bool foundFmt = false, foundData = false;

    while (f && !foundData) {
        const std::string chunkId = tag();
        const uint32_t chunkSize = read4();

        if (!f)
            break;

        const auto chunkStart = f.tellg();
        // Reject hostile chunk sizes that would seek past the end of the file.
        if (chunkStart < 0 || chunkStart > fileSize ||
            static_cast<uint64_t>(chunkSize) > static_cast<uint64_t>(fileSize - chunkStart)) {
            return false;
        }
        const auto chunkEnd = chunkStart + static_cast<std::streamoff>(chunkSize);

        if (chunkId == "fmt " && !foundFmt) {
            audioFmt = read2();
            channels = read2();
            read4(); // sample rate
            read4(); // byte rate
            read2(); // block align
            bitsPerSample = read2();
            foundFmt = true;
            f.seekg(chunkEnd);
        } else if (chunkId == "data" && foundFmt) {
            foundData = true;
            const int ch = std::max(1, static_cast<int>(channels));

            if (audioFmt == 1 && bitsPerSample == 16) {
                const int n = static_cast<int>(chunkSize) / (ch * 2);
                out.reserve(n);
                for (int i = 0; i < n; ++i) {
                    float sum = 0.0f;
                    for (int c = 0; c < ch; ++c) {
                        int16_t s{};
                        f.read(reinterpret_cast<char*>(&s), 2);
                        sum += static_cast<float>(s) / 32768.0f;
                    }
                    out.push_back(sum / static_cast<float>(ch));
                }
            } else if (audioFmt == 3 && bitsPerSample == 32) {
                const int n = static_cast<int>(chunkSize) / (ch * 4);
                out.reserve(n);
                for (int i = 0; i < n; ++i) {
                    float sum = 0.0f;
                    for (int c = 0; c < ch; ++c) {
                        float s{};
                        f.read(reinterpret_cast<char*>(&s), 4);
                        sum += s;
                    }
                    out.push_back(sum / static_cast<float>(ch));
                }
            } else {
                return false;
            }
        } else {
            f.seekg(chunkEnd);
        }
    }
    return foundData && !out.empty();
}

} // namespace

bool WavetableOscillator::loadFromWav(const std::string& path) {
    std::vector<float> samples;
    if (!parseWav(path, samples))
        return false;

    const int totalSamples = static_cast<int>(samples.size());
    const int numFrames = std::clamp(totalSamples / kWavetableSize, 1, kMaxWavetableFrames);
    samples.resize(numFrames * kWavetableSize, 0.0f);

    auto data = std::make_shared<WavetableData>();
    data->buildFromFrames(samples.data(), numFrames);
    table_ = std::move(data);
    return true;
}

} // namespace agentic_synth::engine
