#pragma once

namespace agentic_synth::engine {

// One-pole low-pass smoother for parameter values updated at block rate
// but read per-sample. Cutoff defaults to ~30 Hz; that's slow enough to
// kill zipper noise on a 60 Hz knob update yet fast enough to be
// perceptually instantaneous (~5 ms time constant).
//
// Discrete form: state += coeff * (target - state)
// where coeff = 1 - exp(-2*pi*fc/Fs).
//
// Block-rate writers call setTarget(); per-sample readers call process().
class ParamSmoother {
public:
    ParamSmoother() = default;

    // Configure for the current audio device. Must be called whenever
    // sample rate changes (prepareToPlay).
    void setSampleRate(double sampleRateHz) noexcept;

    // Update the smoothing cutoff. Default is 30 Hz on construction.
    void setCutoffHz(float cutoffHz) noexcept;

    // Set both current and target to the same value — useful after
    // patch load where you want the new value immediately.
    void reset(float value) noexcept;

    // Update the destination. Will be approached one sample at a time.
    void setTarget(float target) noexcept;

    // Advance one sample and return the smoothed value.
    float process() noexcept;

    // Read without advancing.
    [[nodiscard]] float current() const noexcept { return state_; }
    [[nodiscard]] float target() const noexcept { return target_; }

private:
    float state_{0.0f};
    float target_{0.0f};
    float coeff_{0.0f}; // pre-computed (1 - exp(-2*pi*fc/Fs)) per sample-rate + cutoff
    double sampleRate_{44100.0};
    float cutoffHz_{30.0f};
    void recomputeCoeff() noexcept;
};

} // namespace agentic_synth::engine
