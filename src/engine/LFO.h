#pragma once

// Stub — replaced by src/engine/LFO.h from fix/issue-11 at merge time.

namespace agentic_synth::engine {

enum class LfoShape { Sine, Triangle, Saw, Square, SampleAndHold };

class LFO {
public:
    LFO() = default;

    void setSampleRate(double /*sampleRate*/) noexcept {}
    void setShape(LfoShape /*shape*/) noexcept {}
    void setDepth(float d) noexcept { depth_ = d; }
    void setFreeRate(float /*hz*/) noexcept {}
    void trigger() noexcept {}
    void reset() noexcept {}

    [[nodiscard]] float processSample() noexcept { return 0.0f; }
    void setTargetSlot(int slot) noexcept { targetSlot_ = slot; }
    [[nodiscard]] int targetSlot() const noexcept { return targetSlot_; }

private:
    float depth_{0.0f};
    int targetSlot_{0};
};

} // namespace agentic_synth::engine
