#pragma once

// Stub — replaced by src/engine/WavetableOscillator.h from fix/issue-7 at merge time.

namespace agentic_synth::engine {

class WavetableOscillator {
public:
    WavetableOscillator() = default;

    void setSampleRate(double /*sr*/) noexcept {}
    void setFrequency(double hz) noexcept { freq_ = static_cast<float>(hz); }
    void setMorphPosition(float /*pos*/) noexcept {}
    void reset() noexcept {}

    [[nodiscard]] float processSample() noexcept { return 0.0f; }
    void processBlock(float* output, int numSamples) noexcept {
        for (int i = 0; i < numSamples; ++i) output[i] = processSample();
    }

private:
    float freq_{440.0f};
};

} // namespace agentic_synth::engine
