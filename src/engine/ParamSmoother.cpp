#include "engine/ParamSmoother.h"

#include <cmath>

namespace agentic_synth::engine {

namespace {
constexpr float kTwoPi = 6.28318530717958647692f;
} // namespace

void ParamSmoother::setSampleRate(double sampleRateHz) noexcept {
    sampleRate_ = sampleRateHz > 0.0 ? sampleRateHz : 44100.0;
    recomputeCoeff();
}

void ParamSmoother::setCutoffHz(float cutoffHz) noexcept {
    cutoffHz_ = cutoffHz > 0.0f ? cutoffHz : 0.0f;
    recomputeCoeff();
}

void ParamSmoother::reset(float value) noexcept {
    state_ = value;
    target_ = value;
}

void ParamSmoother::setTarget(float target) noexcept { target_ = target; }

float ParamSmoother::process() noexcept {
    state_ += coeff_ * (target_ - state_);
    return state_;
}

void ParamSmoother::recomputeCoeff() noexcept {
    if (cutoffHz_ <= 0.0f || sampleRate_ <= 0.0) {
        coeff_ = 0.0f;
        return;
    }
    const double arg = -static_cast<double>(kTwoPi) * static_cast<double>(cutoffHz_) / sampleRate_;
    coeff_ = static_cast<float>(1.0 - std::exp(arg));
}

} // namespace agentic_synth::engine
