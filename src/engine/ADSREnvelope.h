#pragma once

namespace agentic_synth::engine {

// Per-voice ADSR envelope with exponential curve shaping.
// All segment curves are computed via a leaky-integrator (coeff + base) recurrence.
// curvature (TCO) controls how exponential the curves are: smaller = sharper.
class ADSREnvelope {
public:
    struct Params {
        float attackSeconds{0.01F};
        float decaySeconds{0.1F};
        float sustainLevel{0.7F}; // [0, 1]
        float releaseSeconds{0.3F};
        float curvature{0.0001F}; // target-coefficient overshoot; smaller → more exponential
    };

    explicit ADSREnvelope(double sampleRate = 44100.0);

    void setSampleRate(double sampleRate);
    void setParams(const Params& params);

    // Sample-accurate gate: takes effect on the very next process() call.
    void noteOn();  // begin attack from current level (retrigger-safe)
    void noteOff(); // begin release

    float process(); // advance one sample and return envelope amplitude [0, 1]

    [[nodiscard]] bool isActive() const noexcept;
    void reset(); // force idle, output = 0

private:
    enum class Stage { Idle, Attack, Decay, Sustain, Release };

    void recalcCoefficients();
    // Compute releaseCoeff_/releaseBase_ to land at zero in releaseSeconds
    // starting from the current output_ level. Used by noteOff() to enter
    // Release, and by recalcCoefficients() to keep the in-flight Release
    // trajectory intact when VoiceManager::applyPatch re-pushes envelope
    // params every audio block.
    void rescaleReleaseFromCurrent();

    double sampleRate_{44100.0};
    Params params_{};
    Stage stage_{Stage::Idle};
    float output_{0.0F};

    float attackCoeff_{0.0F};
    float attackBase_{0.0F};
    float decayCoeff_{0.0F};
    float decayBase_{0.0F};
    // Release-stage coefficients used by process(). Calibrated to land at
    // zero in exactly releaseSeconds from the level at which Release began
    // (set by noteOff, or by recalcCoefficients() when called mid-release).
    float releaseCoeff_{0.0F};
    float releaseBase_{0.0F};
    // Release-from-start (level=1.0) calibration. Kept separately so that
    // recalcCoefficients() called every block by VoiceManager::applyPatch
    // does NOT clobber the current-level rescale installed by noteOff. The
    // start pair is used (a) as the seed for a fresh Release entered via
    // noteOff, and (b) as the trajectory if Release is entered while
    // output_ >= 1.0 (unusual; defensive). See recalcCoefficients() comment.
    float releaseCoeffStart_{0.0F};
    float releaseBaseStart_{0.0F};
};

} // namespace agentic_synth::engine
