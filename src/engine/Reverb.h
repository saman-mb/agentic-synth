#pragma once

#include <array>
#include <vector>

namespace agentic_synth::engine {

// Freeverb-style stereo Schroeder reverb. Designed for cheap, musical
// "room" to "hall" tail; not concert-hall quality. Per-sample CPU cost is
// roughly 16 comb reads + 4 allpass reads for the stereo pair (~50 simple
// ops) after the full 8-comb Freeverb bank is restored (#432).
//
// Lifetime:
//   - Construct once
//   - Call prepare(sampleRate) before processing
//   - Call setSize(0..1), setDamp(0..1), setMix(0..1) at block-rate
//   - Call process(inL, inR, &outL, &outR) per sample
//   - reset() clears all delay lines (call on patch load, after long silence)
//
// Audio-thread safe: no allocation in process or any setter.
class Reverb {
public:
    Reverb();

    void prepare(double sampleRate);

    // size: 0 = small room (~0.3s tail), 1 = large hall (~6s tail).
    // Maps to comb-filter feedback gain.
    void setSize(float size01) noexcept;

    // damp: 0 = bright tail, 1 = near-fully-damped dark tail. Maps to a
    // one-pole lowpass inside each comb's feedback path; the user range is
    // rescaled onto the full internal range (see kMaxInternalDamping).
    void setDamp(float damp01) noexcept;

    // Wet/dry mix. 0 = dry only, 1 = wet only.
    void setMix(float mix01) noexcept;

    // Stereo input → stereo output. Wet/dry crossfade applied inside.
    void process(float inL, float inR, float& outL, float& outR) noexcept;

    void reset() noexcept;

private:
    // Implementation details visible only for sizing — caller doesn't touch.
    // kNumCombs matches Freeverb-original's per-channel comb count; the tail's
    // modal density scales with it (#432).
    static constexpr int kNumCombs = 8;
    static constexpr int kNumAllpasses = 2;
    // Internal one-pole damping ceiling. Freeverb's original damp1 = damp*0.5
    // could never fully darken the tail; 0.95 reaches a ~350 Hz feedback
    // lowpass corner while staying just shy of the state-freezing d = 1.0.
    static constexpr float kMaxInternalDamping = 0.95f;

    struct Comb {
        std::vector<float> buf;
        int idx{0};
        float lowpassState{0.0f};
        float feedback{0.84f};
        float damping{0.5f};
        float process(float input) noexcept;
        void resize(int sizeSamples);
        void reset() noexcept;
    };
    struct Allpass {
        std::vector<float> buf;
        int idx{0};
        float feedback{0.5f};
        float process(float input) noexcept;
        void resize(int sizeSamples);
        void reset() noexcept;
    };

    std::array<Comb, kNumCombs> combsL_;
    std::array<Comb, kNumCombs> combsR_;
    std::array<Allpass, kNumAllpasses> allpassesL_;
    std::array<Allpass, kNumAllpasses> allpassesR_;

    double sampleRate_{44100.0};
    float mix_{0.3f};
    float size_{0.5f};
    float damp_{0.5f};
};

} // namespace agentic_synth::engine
