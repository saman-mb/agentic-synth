#include "engine/HighPassFilter.h"

#include <algorithm>
#include <cmath>

namespace agentic_synth::engine {

HighPassFilter::HighPassFilter() = default;

void HighPassFilter::prepare(double sampleRate) {
    sampleRate_ = (sampleRate > 0.0) ? sampleRate : 44100.0;
    recomputeCoeff();
    reset();
}

void HighPassFilter::setCutoff(float hz) noexcept {
    cutoffHz_ = std::clamp(hz, 0.0f, 2000.0f);
    recomputeCoeff();
}

void HighPassFilter::recomputeCoeff() noexcept {
    if (cutoffHz_ <= 0.0f) {
        a_ = 1.0f;
        return;
    }
    const double w = 2.0 * 3.14159265358979 * static_cast<double>(cutoffHz_) / sampleRate_;
    a_ = static_cast<float>(std::exp(-w));
}

void HighPassFilter::reset() noexcept {
    xPrevL_ = 0.0f;
    yPrevL_ = 0.0f;
    xPrevR_ = 0.0f;
    yPrevR_ = 0.0f;
}

void HighPassFilter::processStereo(float* left, float* right, int numSamples) noexcept {
    if (left == nullptr || right == nullptr)
        return;
    if (cutoffHz_ <= 0.0f)
        return; // bypass

    for (int n = 0; n < numSamples; ++n) {
        const float xL = left[n];
        const float xR = right[n];
        const float yL = a_ * (yPrevL_ + xL - xPrevL_);
        const float yR = a_ * (yPrevR_ + xR - xPrevR_);
        xPrevL_ = xL;
        yPrevL_ = yL;
        xPrevR_ = xR;
        yPrevR_ = yR;
        left[n] = yL;
        right[n] = yR;
    }
}

} // namespace agentic_synth::engine
