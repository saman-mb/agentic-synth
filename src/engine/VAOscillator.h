#pragma once

#include <cstdint>

namespace agentic_synth::engine {

class VAOscillator {
public:
    enum class Waveform { Saw, Square, Triangle };

    VAOscillator();

    void prepare(double sampleRate);
    void setWaveform(Waveform w) noexcept;
    void setFrequency(double hz) noexcept;
    void setDetuneCents(double cents) noexcept;

    // Seed analog-style drift from (patch_id, voice, osc). Replaces
    // std::random_device so identical (patch, events, SR) renders are
    // bit-identical (RFC cpp-dsp-core §4).
    void seedDrift(uint32_t seed) noexcept;

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
    void drawNewDriftTarget() noexcept;

    double sampleRate_ = 44100.0;
    double frequency_ = 440.0;
    double detuneCents_ = 0.0;
    double phase_ = 0.0;
    double phaseInc_ = 0.0;
    Waveform waveform_ = Waveform::Saw;

    // Triangle integration state; initialised to waveform value at phase=0.
    // triLeak_ is a one-pole DC-blocker coefficient (~5 Hz cutoff) applied
    // every sample to prevent unbounded integrator drift without colouring audio.
    double triAccum_ = -1.0;
    double triLeak_ = 0.0;

    // Analog drift modulator — xorshift32, seeded (not random_device).
    double driftCents_ = 0.0;
    double driftTarget_ = 0.0;
    double driftAlpha_ = 0.0;  // per-sample smoothing coefficient
    double driftTimer_ = 0.0;  // samples until next target draw
    double driftPeriod_ = 0.0; // samples between target draws
    double driftPeriodMin_ = 44100.0;
    double driftPeriodMax_ = 220500.0;
    uint32_t rngState_ = 1u;
};

} // namespace agentic_synth::engine
