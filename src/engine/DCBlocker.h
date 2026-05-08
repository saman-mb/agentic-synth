#pragma once

namespace agentic_synth::engine {

// Single-pole HPF for DC offset rejection.
// y[n] = x[n] - x[n-1] + R * y[n-1],  R = 1 - 2π·20/Fs
// Apply per oscillator output before mixing.
class DCBlocker {
public:
    explicit DCBlocker(float sample_rate) noexcept : R_(1.0f - 6.28318530f * 20.0f / sample_rate) {}

    float process(float x) noexcept {
        const float y = x - x_prev_ + R_ * y_prev_;
        x_prev_ = x;
        y_prev_ = y;
        return y;
    }

    void reset() noexcept { x_prev_ = y_prev_ = 0.0f; }

    void set_sample_rate(float sr) noexcept { R_ = 1.0f - 6.28318530f * 20.0f / sr; }

private:
    float R_;
    float x_prev_ = 0.0f;
    float y_prev_ = 0.0f;
};

} // namespace agentic_synth::engine
