#pragma once

namespace agentic_synth::engine {

// Pre-filter soft-clip "tube" saturation stage with DC blocker.
//
// Audio path: input → asymmetric tanh waveshape → 1-pole HPF (≈20 Hz) → mix
// with dry. Positive samples get a slightly more aggressive drive coefficient
// than negative ones (1.10× / 0.90×) so the result is biased like a triode
// stage rather than the perfectly-symmetric saturation of a pure tanh —
// produces audible 2nd-harmonic energy on top of the 3rd/5th from the
// symmetric clip, which is what "fuses" stacked saws.
//
// Drive normalization: we scale the input by (1 + drive * kDriveScale) before
// tanh, then divide by tanh(driveScale) so unity input stays near unity
// output across the 0..0.5 drive range. drive == 0 → identity (bit-exact
// bypass guarded explicitly in processStereo).
//
// Reference: Zölzer "DAFX" Ch. 4 ("Nonlinear processing"), Pirkle Ch. 14.
// MVP: no oversampling — alias products live above ~10 kHz for the LF-rich
// saw input the augmenter uses this on, and the downstream lowpass filter
// then attenuates them. Future improvement: 2× polyphase oversampling around
// the tanh stage.
class TubeSat {
public:
    TubeSat();

    void prepare(double sampleRate, int channels = 2);

    // 0 .. 0.5. Above 0.5 the harmonic glue collapses into distortion; the
    // grammar + augmenter clamp here as well.
    void setDrive(float drive01) noexcept;

    // 0 .. 1 wet/dry. mix == 0 → bypass (engine skips entirely when drive==0
    // upstream so this is mostly defensive).
    void setMix(float mix01) noexcept;

    void processStereo(float* left, float* right, int numSamples) noexcept;

    void reset() noexcept;

    [[nodiscard]] float drive() const noexcept { return drive_; }
    [[nodiscard]] float mix() const noexcept { return mix_; }

private:
    // DC blocker state — per channel.
    struct DcBlock {
        float xPrev{0.0f};
        float yPrev{0.0f};
    };

    static float saturate(float x, float drivePos, float driveNeg, float normPos, float normNeg) noexcept;

    double sampleRate_{44100.0};
    float drive_{0.0f};
    float mix_{1.0f};

    // Precomputed shaping coefficients (recomputed when drive_ changes).
    float drivePos_{1.0f};
    float driveNeg_{1.0f};
    float normPos_{1.0f};
    float normNeg_{1.0f};

    // 1-pole HPF coefficient (R ≈ 1 - 2π * fc / fs), fc ≈ 20 Hz.
    float hpfCoeff_{0.99f};

    DcBlock blockL_{};
    DcBlock blockR_{};
};

} // namespace agentic_synth::engine
