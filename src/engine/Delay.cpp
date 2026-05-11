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
    bufferSize_ = static_cast<int>(std::ceil(maxSec * sampleRate_));
    if (bufferSize_ < 2) {
        bufferSize_ = 2;
    }
    bufL_.assign(static_cast<std::size_t>(bufferSize_), 0.0f);
    bufR_.assign(static_cast<std::size_t>(bufferSize_), 0.0f);
    writeIdx_ = 0;
    // Default delay sample count: clamp current time setting against new buffer.
    if (delaySamples_ < 1) {
        delaySamples_ = 1;
    }
    if (delaySamples_ > bufferSize_ - 1) {
        delaySamples_ = bufferSize_ - 1;
    }
}

void Delay::setTimeSeconds(float seconds) noexcept {
    float clamped = std::clamp(seconds, kMinTimeSeconds, kMaxTimeSeconds);
    int samples = static_cast<int>(std::lround(static_cast<double>(clamped) * sampleRate_));
    if (samples < 1) {
        samples = 1;
    }
    if (bufferSize_ > 1 && samples > bufferSize_ - 1) {
        samples = bufferSize_ - 1;
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

    int readIdx = writeIdx_ - delaySamples_;
    if (readIdx < 0) {
        readIdx += bufferSize_;
    }

    const float delayedL = bufL_[static_cast<std::size_t>(readIdx)];
    const float delayedR = bufR_[static_cast<std::size_t>(readIdx)];

    const float oneMinusStereo = 1.0f - stereo_;
    const float feedL = oneMinusStereo * delayedL + stereo_ * delayedR;
    const float feedR = oneMinusStereo * delayedR + stereo_ * delayedL;

    bufL_[static_cast<std::size_t>(writeIdx_)] = inL + feedL * feedback_;
    bufR_[static_cast<std::size_t>(writeIdx_)] = inR + feedR * feedback_;

    writeIdx_ += 1;
    if (writeIdx_ >= bufferSize_) {
        writeIdx_ -= bufferSize_;
    }

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
