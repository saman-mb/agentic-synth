#pragma once

// Stub — replaced by src/engine/ADSREnvelope.h from fix/issue-10 at merge time.

namespace agentic_synth::engine {

class ADSREnvelope {
public:
    struct Params {
        float attackSeconds{0.01f};
        float decaySeconds{0.1f};
        float sustainLevel{0.7f};
        float releaseSeconds{0.3f};
    };

    explicit ADSREnvelope(double /*sampleRate*/ = 44100.0) {}

    void setSampleRate(double /*sr*/) noexcept {}
    void setParams(const Params& p) noexcept { params_ = p; }

    void noteOn() noexcept {
        active_ = true;
        releasing_ = false;
    }

    void noteOff() noexcept {
        releasing_ = true;
        active_ = false;
    }

    [[nodiscard]] float process() noexcept { return active_ ? 1.0f : 0.0f; }
    [[nodiscard]] bool isActive() const noexcept { return active_; }

    void reset() noexcept {
        active_ = false;
        releasing_ = false;
    }

private:
    Params params_;
    bool active_{false};
    bool releasing_{false};
};

} // namespace agentic_synth::engine
