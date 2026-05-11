#include "engine/WavetableOscillator.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <numbers>

namespace agentic_synth::engine {

// ---- WavetableData ----

void WavetableData::buildFromFrames(const float* samples, int numFrames) {
    frameCount = std::min(numFrames, kMaxWavetableFrames);
    for (int f = 0; f < frameCount; ++f) {
        mips[0][f].assign(samples + f * kWavetableSize, samples + f * kWavetableSize + kWavetableSize);
    }
    for (int level = 1; level < kNumMipLevels; ++level)
        buildMipLevel(level);
}

void WavetableData::buildMipLevel(int level) {
    const int dstSize = kWavetableSize >> level;
    if (dstSize < 1)
        return;
    for (int f = 0; f < frameCount; ++f) {
        const auto& src = mips[level - 1][f];
        auto& dst = mips[level][f];
        dst.resize(dstSize);
        for (int i = 0; i < dstSize; ++i)
            dst[i] = (src[2 * i] + src[2 * i + 1]) * 0.5f;
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
    sampleRate_ = sampleRate < 1.0 ? 1.0 : sampleRate; // guard against zero / negative
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

int WavetableOscillator::selectMipLevel() const noexcept {
    // Select smallest table where (tableSize * phaseIncrement) <= 0.5,
    // i.e., we advance at most half a table sample per output sample.
    if (phaseIncrement_ <= 0.0)
        return kNumMipLevels - 1;
    const double ratio = static_cast<double>(kWavetableSize) * phaseIncrement_ * 2.0;
    if (ratio <= 1.0)
        return 0;
    int level = static_cast<int>(std::ceil(std::log2(ratio)));
    return std::clamp(level, 0, kNumMipLevels - 1);
}

float WavetableOscillator::readMorphedSample(int mipLevel, double phase) const noexcept {
    if (!table_ || table_->frameCount == 0)
        return 0.0f;

    const int tableSize = kWavetableSize >> mipLevel;
    if (tableSize < 1)
        return 0.0f;

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
    const float out = readMorphedSample(selectMipLevel(), phase_);
    phase_ = std::fmod(phase_ + phaseIncrement_, 1.0);
    return out;
}

void WavetableOscillator::processBlock(float* output, int numSamples) noexcept {
    const int mipLevel = selectMipLevel();
    for (int i = 0; i < numSamples; ++i) {
        output[i] = readMorphedSample(mipLevel, phase_);
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
