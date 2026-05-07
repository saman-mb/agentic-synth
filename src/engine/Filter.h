#pragma once

// Stub — replaced by src/engine/Filter.h from fix/issue-9 at merge time.

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

class MoogLadder final : public Filter {
public:
    void prepare(double /*sampleRate*/) override {}
    void setCutoff(float /*hz*/) override {}
    void setResonance(float /*resonance*/) override {}
    float process(float input) override { return input; }
    void reset() override {}
};

class SVFilter final : public Filter {
public:
    explicit SVFilter(FilterMode /*mode*/ = FilterMode::LP) {}
    void prepare(double /*sampleRate*/) override {}
    void setCutoff(float /*hz*/) override {}
    void setResonance(float /*resonance*/) override {}
    float process(float input) override { return input; }
    void reset() override {}
    void setMode(FilterMode /*mode*/) {}
};

} // namespace agentic_synth::engine
