#pragma once

#include <array>
#include <vector>

namespace agentic_synth::engine {

// 3-tap modulated delay-line chorus (Juno-ensemble / Dimension-D inspired).
//
// Each tap is independently modulated by a sine LFO at chorus.rate_hz with a
// 2π/3 phase offset between taps; per-channel LFOs use an additional π
// offset between L and R so the three taps land on slightly different delay
// times across the stereo field — that's where the "ensemble" width comes
// from. Cubic-Hermite interpolation on the delay-line read keeps the LFO
// modulation click-free even at low rates.
//
// Audio-thread contract:
//   - prepare(sr, channels) allocates the delay buffer ONCE
//   - setRate / setDepth / setMix can be called at block-rate; cheap atomic-
//     style stores (single-writer audio thread is the only caller anyway)
//   - processStereo runs the per-sample DSP — zero allocations, zero locks
//   - reset() zero-fills the delay lines (call on patch load)
//
// Reference: Dattorro "Effect Design Pt 2: Delay-Line Modulation and Chorus"
// (JAES, 1997); Pirkle, "Designing Audio Effect Plug-Ins in C++" Ch. 16.
class Chorus {
public:
    Chorus();

    void prepare(double sampleRate, int channels = 2);

    // 0.1 .. 5.0 Hz. Above 1 Hz the effect becomes more "vibrato" than
    // "chorus" but we honour whatever the patch says.
    void setRate(float rateHz) noexcept;

    // 0 .. 1. Scales the modulation excursion around each tap's base delay.
    void setDepth(float depth01) noexcept;

    // 0 .. 1 wet/dry crossfade. mix == 0 → input passed through bit-exact.
    void setMix(float mix01) noexcept;

    void processStereo(float* left, float* right, int numSamples) noexcept;

    void reset() noexcept;

    // Test helpers.
    [[nodiscard]] float mix() const noexcept { return mix_; }

private:
    // 30 ms max delay + a few ms of modulation headroom. With cubic
    // interpolation we need ≥ 1 sample before the read index, so add a small
    // margin (4 samples) before the modulation can pull negative.
    static constexpr float kMaxDelayMs = 30.0f;
    static constexpr float kModDepthMs = 8.0f; // worst-case half-excursion

    // Cubic-Hermite interpolation read of `buf` at fractional position
    // `delaySamples` behind `writeIdx`. Buffer must be at least
    // ceil(delaySamples) + 2 samples long; caller guarantees via prepare().
    static float readInterpolated(const float* buf, int bufSize, int writeIdx,
                                  float delaySamples) noexcept;

    int bufSize_{0};
    std::vector<float> bufL_;
    std::vector<float> bufR_;
    int writeIdx_{0};

    double sampleRate_{44100.0};
    float rateHz_{0.4f};
    float depth_{0.35f};
    float mix_{0.0f};

    // LFO phase per channel, incremented per sample.
    double phaseL_{0.0};
    double phaseR_{0.0};

    // Per-tap base delays in ms (Dimension-D-ish: 15 / 20 / 25).
    static constexpr std::array<float, 3> kTapBaseMs{15.0f, 20.0f, 25.0f};
    // Per-tap modulation depth scalars (slightly different so the three
    // sinusoids don't sum to a phase-locked single LFO).
    static constexpr std::array<float, 3> kTapModMs{5.0f, 7.0f, 6.0f};
};

} // namespace agentic_synth::engine
