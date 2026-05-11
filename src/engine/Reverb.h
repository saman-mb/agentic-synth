#pragma once

#include <array>
#include <vector>

namespace agentic_synth::engine {

// Freeverb-style stereo Schroeder reverb. Designed for cheap, musical
// "room" to "hall" tail; not concert-hall quality. Per-sample CPU cost is
// roughly 8 comb reads + 4 allpass reads × 2 channels (~30 simple ops).
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

    // damp: 0 = bright tail, 1 = dark tail. Maps to one-pole lowpass
    // inside each comb's feedback path.
    void setDamp(float damp01) noexcept;

    // Wet/dry mix. 0 = dry only, 1 = wet only.
    void setMix(float mix01) noexcept;

    // Stereo input → stereo output. Wet/dry crossfade applied inside.
    void process(float inL, float inR, float& outL, float& outR) noexcept;

    void reset() noexcept;

private:
    // Implementation details visible only for sizing — caller doesn't touch.
    static constexpr int kNumCombs = 4;
    static constexpr int kNumAllpasses = 2;

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
