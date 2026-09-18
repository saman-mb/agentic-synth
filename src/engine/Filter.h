#pragma once

#include "engine/Oversampler.h"

#include <array>
#include <cmath>
#include <cstddef>

namespace agentic_synth::engine {

enum class FilterMode { LP, HP, BP, Notch };

class Filter {
public:
    virtual ~Filter() = default;
    virtual void prepare(double sampleRate) = 0;
    virtual void setCutoff(float hz) = 0;
    virtual void setResonance(float resonance) = 0;
    // Optional: drive in [0, 1]; default no-op for filters without nonlinearity.
    virtual void setDrive(float /*drive*/) {}
    virtual float process(float input) = 0;
    virtual void reset() = 0;
};

// 24 dB/oct Moog ladder — zero-delay-feedback formulation with tanh feedback saturation.
// The closed-form ZDF solve produces a *linear* y4 prediction; we then apply tanh()
// to the predicted output before it is fed back. This adds the classic Moog growl,
// allows clean self-oscillation, and provides a stability safety-net at high k.
//
// #431: the tanh sits *inside* the resonance feedback loop, so the harmonics it
// generates are re-injected into the ladder and can fold into the passband at
// high drive/resonance — a static anti-imaging filter after the tanh cannot
// remove them without also changing the loop. The whole ladder therefore runs
// at 2x (Oversampler2x, half-band anti-imaging/anti-aliasing pair) with the
// ZDF coefficients derived for the oversampled rate, and is decimated back to
// the base rate at the output. prepare() owns the rate change; no allocation
// or locking happens per sample.
//
// The tanh only reaches the output through the `k * y4_fb` feedback term, so at
// resonance == 0 (k == 0) the ladder is exactly linear no matter how hard the
// drive is; there is no nonlinearity to alias and oversampling would only add
// group delay and cost. The oversampled path is therefore engaged exactly when
// resonance > 0.
class MoogLadder final : public Filter {
public:
    void prepare(double sampleRate) override;
    void setCutoff(float hz) override;
    void setResonance(float resonance) override;
    void setDrive(float drive) override;
    float process(float input) override;
    void reset() override;

private:
    void updateCoefficients();
    // Engages/disengages the 2x path when the feedback nonlinearity becomes
    // live (resonance > 0) or dormant (resonance == 0).
    void setOversamplingActive(bool active) noexcept;
    // ZDF cascade evaluated at the active internal rate (2x when oversampling).
    float processInternal(double x) noexcept;

    double sampleRate_ = 44100.0; // internal rate: base, or 2 * base
    double baseSampleRate_ = 44100.0;
    bool oversamplingActive_{false};
    float cutoff_ = 1000.0f;
    float resonance_ = 0.0f;
    float drive_ = 0.0f;

    double g_ = 0.0;
    double a_ = 0.0;         // g / (1 + g)
    double b_ = 0.0;         // 1 / (1 + g)
    double k_ = 0.0;         // feedback gain; reaches 4.0 at resonance=1.0 (self-oscillation threshold).
    double driveGain_ = 1.0; // 1 + drive * kDriveFactor
    double driveComp_ = 1.0; // output compensation (inverse of a softer share of driveGain_)

    std::array<double, 4> s_ = {};
    Oversampler2x oversampler_{};
};

// ZDF state-variable filter (Simper/Cytomic) — LP, HP, BP, Notch outputs
class SVFilter final : public Filter {
public:
    explicit SVFilter(FilterMode mode = FilterMode::LP);

    void prepare(double sampleRate) override;
    void setCutoff(float hz) override;
    void setResonance(float resonance) override;
    float process(float input) override;
    void reset() override;

    void setMode(FilterMode mode);

private:
    void updateCoefficients();

    FilterMode mode_;
    double sampleRate_ = 44100.0;
    float cutoff_ = 1000.0f;
    float resonance_ = 0.0f;

    double a1_ = 0.0, a2_ = 0.0, a3_ = 0.0;
    double k_ = 2.0; // damping = 1/Q; clamped >= 0.1 to prevent self-oscillation

    double ic1eq_ = 0.0;
    double ic2eq_ = 0.0;
};

} // namespace agentic_synth::engine
