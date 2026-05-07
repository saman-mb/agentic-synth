#pragma once

// Stub — replaced by src/engine/VAOscillator.h from fix/issue-8 at merge time.

namespace agentic_synth::engine {

class VAOscillator {
public:
    enum class Waveform { Saw, Square, Triangle };

    VAOscillator() = default;

    void prepare(double /*sampleRate*/) noexcept {}
    void setWaveform(Waveform /*w*/) noexcept {}
    void setFrequency(double hz) noexcept { freq_ = static_cast<float>(hz); }
    void setDetuneCents(double /*cents*/) noexcept {}
    void reset() noexcept {}

    [[nodiscard]] float processSample() noexcept { return 0.0f; }

private:
    float freq_{440.0f};
};

} // namespace agentic_synth::engine
