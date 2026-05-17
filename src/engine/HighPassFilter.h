#pragma once

namespace agentic_synth::engine {

// 1-pole high-pass filter for the reverb auxiliary send path.
//
// Discrete form (DC blocker / 1-pole shelving HP):
//   y[n] = a * (y[n-1] + x[n] - x[n-1])
// where a = exp(-2π * fc / fs). At fc this yields ~6 dB/oct attenuation
// below the cutoff with unity passband above it. Sufficient to clean sub
// energy off a reverb send without coloring the audible band.
//
// Bypass contract: setCutoff(0.0f) (or any cutoff ≤ 0) puts the filter in
// pass-through mode — processStereo returns immediately without touching
// state, so toggling reverb_send_hpf_hz off-and-back is click-free.
class HighPassFilter {
public:
    HighPassFilter();

    void prepare(double sampleRate);

    // 0 (bypass) or 20..1000 Hz.
    void setCutoff(float hz) noexcept;

    void processStereo(float* left, float* right, int numSamples) noexcept;

    void reset() noexcept;

    [[nodiscard]] float cutoffHz() const noexcept { return cutoffHz_; }

private:
    void recomputeCoeff() noexcept;

    double sampleRate_{44100.0};
    float cutoffHz_{0.0f};
    float a_{1.0f};

    float xPrevL_{0.0f};
    float yPrevL_{0.0f};
    float xPrevR_{0.0f};
    float yPrevR_{0.0f};
};

} // namespace agentic_synth::engine
