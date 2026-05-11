#pragma once

#include <vector>

namespace agentic_synth::engine {

// Stereo delay with feedback + optional stereo offset (ping-pong-ish).
// Patch params:
//   - time_s: delay time in seconds (will be clamped to [0.001, 2.0])
//   - feedback: 0..0.99 (clamped — never reach 1.0 to avoid blow-up)
//   - mix: 0..1 wet/dry crossfade
//   - stereo: 0..1, controls L/R inter-channel cross-feed
//     0 = independent L and R delay lines (parallel),
//     1 = full ping-pong (L taps from R delay, R taps from L delay)
//
// Lifetime:
//   - Construct once
//   - prepare(sampleRate, maxDelaySeconds=2.5) allocates the ring buffer
//   - setTimeSeconds / setFeedback / setMix / setStereo at block-rate
//   - process(inL, inR, &outL, &outR) per sample
//   - reset() zero-fills the buffer (call on patch load)
//
// Audio-thread safe: no allocation in process or setters.
class Delay {
public:
    Delay();

    void prepare(double sampleRate, double maxDelaySeconds = 2.5);

    void setTimeSeconds(float seconds) noexcept;
    void setFeedback(float fb01) noexcept;   // clamped to [0, 0.99]
    void setMix(float mix01) noexcept;       // 0 = dry, 1 = wet
    void setStereo(float stereo01) noexcept; // 0 = parallel, 1 = ping-pong

    void process(float inL, float inR, float& outL, float& outR) noexcept;

    void reset() noexcept;

private:
    std::vector<float> bufL_;
    std::vector<float> bufR_;
    int writeIdx_{0};
    int bufferSize_{0};

    double sampleRate_{44100.0};
    int delaySamples_{0};
    float feedback_{0.3f};
    float mix_{0.25f};
    float stereo_{0.5f};
};

} // namespace agentic_synth::engine
