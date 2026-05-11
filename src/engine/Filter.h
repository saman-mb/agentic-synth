#pragma once

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
    virtual float process(float input) = 0;
    virtual void reset() = 0;
};

// 24 dB/oct Moog ladder — zero-delay-feedback formulation
class MoogLadder final : public Filter {
public:
    void prepare(double sampleRate) override;
    void setCutoff(float hz) override;
    void setResonance(float resonance) override;
    float process(float input) override;
    void reset() override;

private:
    void updateCoefficients();

    double sampleRate_ = 44100.0;
    float cutoff_ = 1000.0f;
    float resonance_ = 0.0f;

    double g_ = 0.0;
    double a_ = 0.0; // g / (1 + g)
    double b_ = 0.0; // 1 / (1 + g)
    double k_ = 0.0; // feedback gain; capped at 3.8 < 4 (self-oscillation threshold)

    std::array<double, 4> s_ = {};
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
