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

WavetableOscillator::WavetableOscillator() {
    std::vector<float> sine(kWavetableSize);
    for (int i = 0; i < kWavetableSize; ++i)
        sine[i] = static_cast<float>(std::sin(2.0 * std::numbers::pi * i / kWavetableSize));
    auto data = std::make_shared<WavetableData>();
    data->buildFromFrames(sine.data(), 1);
    table_ = std::move(data);
}

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
    // We want mip k to be the smallest level whose cutoff bin still sits at or
    // above Nyquist for the current phase increment, exactly as before but
    // expressed continuously so the fractional part can drive the crossfade.
    //
    //   bin index of the fundamental = phaseIncrement * kWavetableSize
    //   mip k allows bins up to (kWavetableSize >> (k+1))
    //   need bins_used <= cutoff(k)  →  k >= log2(N / (2 * bins_used))
    //                                 = log2(1 / (2 * phaseIncrement))
    //
    // Solving for k: k = -log2(2 * phaseIncrement) = log2(N * phaseIncrement * 2) - log2(N).
    // Equivalent simplification: ratio = kWavetableSize * phaseIncrement * 2;
    // k_continuous = log2(ratio). Matches the old integer code's "ceil(log2(ratio))".
    if (phaseIncrement_ <= 0.0)
        return static_cast<float>(kNumMipLevels - 1);
    const double ratio = static_cast<double>(kWavetableSize) * phaseIncrement_ * 2.0;
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

float WavetableOscillator::processSample() noexcept {
    const float levelF = mipLevelFloat();
    const int idxLow = std::clamp(static_cast<int>(std::floor(levelF)), 0, kNumMipLevels - 1);
    const int idxHigh = std::clamp(idxLow + 1, 0, kNumMipLevels - 1);
    const float fracLevel = std::clamp(levelF - static_cast<float>(idxLow), 0.0f, 1.0f);

    float out;
    if (!mipCrossfadeEnabled_ || idxLow == idxHigh) {
        // Match the old behaviour: pick the higher (more bandlimited) integer mip.
        const int single = std::clamp(static_cast<int>(std::ceil(levelF)), 0, kNumMipLevels - 1);
        out = readMorphedSample(single, phase_);
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
    const int idxLow = std::clamp(static_cast<int>(std::floor(levelF)), 0, kNumMipLevels - 1);
    const int idxHigh = std::clamp(idxLow + 1, 0, kNumMipLevels - 1);
    const float fracLevel = std::clamp(levelF - static_cast<float>(idxLow), 0.0f, 1.0f);
    const bool useCrossfade = mipCrossfadeEnabled_ && idxLow != idxHigh;
    const int singleIdx = useCrossfade ? idxLow : std::clamp(static_cast<int>(std::ceil(levelF)), 0, kNumMipLevels - 1);

    for (int i = 0; i < numSamples; ++i) {
        if (useCrossfade) {
            const float sLow = readMorphedSample(idxLow, phase_);
            const float sHigh = readMorphedSample(idxHigh, phase_);
            output[i] = sLow * (1.0f - fracLevel) + sHigh * fracLevel;
        } else {
            output[i] = readMorphedSample(singleIdx, phase_);
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
