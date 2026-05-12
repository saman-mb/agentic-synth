#include "engine/Delay.h"

#include <algorithm>
#include <cmath>

namespace agentic_synth::engine {

namespace {
constexpr float kMinTimeSeconds = 0.001f;
constexpr float kMaxTimeSeconds = 2.0f;
constexpr float kMaxFeedback = 0.99f;
} // namespace

Delay::Delay() = default;

void Delay::prepare(double sampleRate, double maxDelaySeconds) {
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 44100.0;
    const double maxSec = maxDelaySeconds > 0.0 ? maxDelaySeconds : 2.5;
    // +1 sample headroom so the i1 = (i0+1) wrap of linear interpolation
    // always has a valid neighbour even at the maximum delay length.
    bufferSize_ = static_cast<int>(std::ceil(maxSec * sampleRate_)) + 1;
    if (bufferSize_ < 2) {
        bufferSize_ = 2;
    }
    bufL_.assign(static_cast<std::size_t>(bufferSize_), 0.0f);
    bufR_.assign(static_cast<std::size_t>(bufferSize_), 0.0f);
    writeIdx_ = 0;
    // Clamp current delay against new buffer.
    const float maxDelay = static_cast<float>(bufferSize_ - 1);
    if (delaySamples_ < 1.0f) {
        delaySamples_ = 1.0f;
    }
    if (delaySamples_ > maxDelay) {
        delaySamples_ = maxDelay;
    }
}

void Delay::setTimeSeconds(float seconds) noexcept {
    const float clamped = std::clamp(seconds, kMinTimeSeconds, kMaxTimeSeconds);
    // Fractional: do NOT round to integer samples.
    float samples = clamped * static_cast<float>(sampleRate_);
    if (samples < 1.0f) {
        samples = 1.0f;
    }
    if (bufferSize_ > 1) {
        const float maxDelay = static_cast<float>(bufferSize_ - 1);
        if (samples > maxDelay) {
            samples = maxDelay;
        }
    }
    delaySamples_ = samples;
}

void Delay::setFeedback(float fb01) noexcept { feedback_ = std::clamp(fb01, 0.0f, kMaxFeedback); }

void Delay::setMix(float mix01) noexcept { mix_ = std::clamp(mix01, 0.0f, 1.0f); }

void Delay::setStereo(float stereo01) noexcept { stereo_ = std::clamp(stereo01, 0.0f, 1.0f); }

void Delay::process(float inL, float inR, float& outL, float& outR) noexcept {
    if (bufferSize_ <= 1) {
        outL = inL;
        outR = inR;
        return;
    }

    // Fractional read position.
    const float readPos = static_cast<float>(writeIdx_) - delaySamples_;
    const float floorPos = std::floor(readPos);
    int i0 = static_cast<int>(floorPos);
    // Wrap into [0, bufferSize_).
    i0 %= bufferSize_;
    if (i0 < 0) {
        i0 += bufferSize_;
    }
    int i1 = i0 + 1;
    if (i1 >= bufferSize_) {
        i1 -= bufferSize_;
    }
    const float frac = readPos - floorPos;
    const float oneMinusFrac = 1.0f - frac;

    const float delayedL = bufL_[static_cast<std::size_t>(i0)] * oneMinusFrac
                         + bufL_[static_cast<std::size_t>(i1)] * frac;
    const float delayedR = bufR_[static_cast<std::size_t>(i0)] * oneMinusFrac
                         + bufR_[static_cast<std::size_t>(i1)] * frac;

    // Real ping-pong topology.
    //   stereo = 0: parallel mono-style lines (L→L line, R→R line).
    //   stereo = 1: L input + L's delayed feedback writes into the RIGHT line;
    //               R input + R's delayed feedback writes into the LEFT line.
    //               => an impulse on L appears in the R output after one
    //                  delay length, then in L after two, etc.
    // Crossfade between the two topologies on the `stereo` param.
    const float s = stereo_;
    const float oneMinusS = 1.0f - s;
    const float parallelL = inL + delayedL * feedback_;
    const float parallelR = inR + delayedR * feedback_;
    // Cross-routed signals: L source material destined for the R line, vice versa.
    const float pingToL = inR + delayedR * feedback_;
    const float pingToR = inL + delayedL * feedback_;

    bufL_[static_cast<std::size_t>(writeIdx_)] = oneMinusS * parallelL + s * pingToL;
    bufR_[static_cast<std::size_t>(writeIdx_)] = oneMinusS * parallelR + s * pingToR;

    writeIdx_ += 1;
    if (writeIdx_ >= bufferSize_) {
        writeIdx_ -= bufferSize_;
    }

    // Output: dry from input, wet from same-side delay line tap.
    const float oneMinusMix = 1.0f - mix_;
    outL = oneMinusMix * inL + mix_ * delayedL;
    outR = oneMinusMix * inR + mix_ * delayedR;
}

void Delay::reset() noexcept {
    std::fill(bufL_.begin(), bufL_.end(), 0.0f);
    std::fill(bufR_.begin(), bufR_.end(), 0.0f);
    writeIdx_ = 0;
}

} // namespace agentic_synth::engine
