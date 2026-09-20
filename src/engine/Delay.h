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
// Delay-time changes (#433): setTimeSeconds no longer resplices the read
// pointer instantaneously. A mid-render change crossfades the output between
// the old and new read pointers over ~10 ms (see kTimeCrossfadeSeconds). The
// first setTimeSeconds after prepare()/reset() snaps instead of fading, since
// there is no prior audio to glide from and offline/timing-accurate callers
// expect the requested delay from the first sample.
//
// Audio-thread safe: no allocation in process or setters; all crossfade state
// is plain members sized/initialised in prepare().
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
    // Linear-interpolated fractional tap `delaySamples` behind writeIdx_ in
    // `buf`. Caller guarantees delaySamples is within [1, bufferSize_ - 1].
    [[nodiscard]] float readTap(const std::vector<float>& buf, float delaySamples) const noexcept;
    // Interpolated read offset the crossfade is currently pointing at. Equals
    // delaySamples_ when no crossfade is in flight.
    [[nodiscard]] float currentCrossfadeOffset() const noexcept;

    std::vector<float> bufL_;
    std::vector<float> bufR_;
    int writeIdx_{0};
    int bufferSize_{0};

    double sampleRate_{44100.0};
    float delaySamples_{0.0f}; // target read offset in samples
    float feedback_{0.3f};
    float mix_{0.25f};
    float stereo_{0.5f};

    // ~10 ms crossfade between old and new read pointers on a time change.
    // Long enough to suppress the splice click, short enough to feel instant.
    static constexpr float kTimeCrossfadeSeconds = 0.01f;
    int timeCrossfadeTotal_{0}; // samples; 0 = not prepared / fading disabled
    int timeCrossfadeRemaining_{0};
    float timeCrossfadeFromSamples_{0.0f};
    bool timePrimed_{false};
};

} // namespace agentic_synth::engine
