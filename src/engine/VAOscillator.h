#pragma once

#include <random>

namespace agentic_synth::engine {

class VAOscillator {
public:
    enum class Waveform { Saw, Square, Triangle };

    VAOscillator();

    void prepare(double sampleRate);
    void setWaveform(Waveform w) noexcept;
    void setFrequency(double hz) noexcept;
    void setDetuneCents(double cents) noexcept;

    [[nodiscard]] float processSample() noexcept;
    [[nodiscard]] double getDriftCents() const noexcept;

    void reset() noexcept;

private:
    [[nodiscard]] double saw() noexcept;
    [[nodiscard]] double square() noexcept;
    [[nodiscard]] double triangle() noexcept;

    static double polyBlep(double t, double dt) noexcept;
    void updatePhaseInc() noexcept;
    void tickDrift() noexcept;

    double sampleRate_ = 44100.0;
    double frequency_ = 440.0;
    double detuneCents_ = 0.0;
    double phase_ = 0.0;
    double phaseInc_ = 0.0;
    Waveform waveform_ = Waveform::Saw;

    // Triangle integration state; initialised to waveform value at phase=0
    double triAccum_ = -1.0;
    int triRecenterCounter_ = 0;

    // Analog drift modulator
    double driftCents_ = 0.0;
    double driftTarget_ = 0.0;
    double driftAlpha_ = 0.0;  // per-sample smoothing coefficient
    double driftTimer_ = 0.0;  // samples until next target draw
    double driftPeriod_ = 0.0; // samples between target draws

    std::mt19937_64 rng_;
    std::uniform_real_distribution<double> driftTargetDist_{-5.0, 5.0};
    std::uniform_real_distribution<double> driftPeriodDist_{44100.0, 220500.0};
};

} // namespace agentic_synth::engine
