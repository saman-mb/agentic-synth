#pragma once

#include "engine/ADSREnvelope.h"
#include "engine/Filter.h"
#include "engine/LFO.h"
#include "engine/ParamSmoother.h"
#include "engine/PatchStruct.h"
#include "engine/PatchValidator.h"
#include "engine/VAOscillator.h"
#include "engine/WavetableOscillator.h"

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace agentic_synth::engine {

// Per-voice DSP state. VoiceManager owns and allocates all voices.
struct Voice {
    int midiNote{-1};     // -1 = free slot
    bool noteIsOn{false}; // false = envelope releasing, true = held
    uint64_t noteOnOrder{0};

    float velocity{1.0f}; // [0, 1] — scales amp env peak and filter env mod

    float targetFrequency{440.0f};
    float currentFrequency{440.0f}; // slides toward target during portamento

    WavetableOscillator wavetableOsc;
    VAOscillator vaOsc;
    std::unique_ptr<Filter> filter; // MoogLadder by default
    ADSREnvelope ampEnv;
    ADSREnvelope filterEnv;
    std::array<LFO, 2> lfos;
    DcBlocker dcBlocker;

    // LFO routing/depth pulled from patch each block.
    std::array<LfoTarget, 2> lfoTargets{{LfoTarget::None, LfoTarget::None}};
    std::array<float, 2> lfoDepths{{0.0f, 0.0f}};

    // Filter env modulation (filter.env_mod from patch).
    float filterEnvMod{0.0f};

    // Voice-steal fade-out: when > 0, voice output is multiplied by a linear
    // ramp from fadeOutSamplesRemaining_/fadeOutSamplesTotal_ → 0.
    int fadeOutSamplesRemaining{0};
    int fadeOutSamplesTotal{0};

    // Stereo panning, fixed per voice lifetime. pan ∈ [-1, +1]; gains are
    // precomputed constant-power coefficients so renderBlock doesn't call
    // cos/sin per sample. panGainL² + panGainR² == 1 within float epsilon.
    float pan{0.0f};
    float panGainL{0.7071068f}; // cos(π/4) — center default
    float panGainR{0.7071068f}; // sin(π/4)

    [[nodiscard]] bool isActive() const noexcept {
        return ampEnv.isActive() || fadeOutSamplesRemaining > 0;
    }
    void prepare(double sampleRate);
    // baseCutoffHz/resonance come from VoiceManager per-sample smoothers; this
    // lets us apply LFO + filter-env modulation on top of the smoothed value.
    float render(float portamentoAlpha, float baseCutoffHz, float resonance) noexcept;
};

// N-voice polyphonic allocator with oldest-note stealing and portamento.
class VoiceManager {
public:
    static constexpr int kDefaultVoiceCount = 16;
    // ~5 ms at 48 kHz; recomputed in prepare() based on actual sample rate.
    static constexpr float kVoiceStealFadeSeconds = 0.005f;

    explicit VoiceManager(int voiceCount = kDefaultVoiceCount);

    // Call once before processing begins (or on sample-rate change).
    void prepare(double sampleRate);

    // MIDI event handlers.
    void noteOn(int midiNote, float velocity);
    void noteOff(int midiNote);

    // 0 = instant pitch change; > 0 = glide time in seconds.
    void setPortamento(float seconds) noexcept;
    // true = retrigger envelope on each noteOn; false = legato (keep envelope running).
    void setRetrigger(bool retrigger) noexcept;

    // Parameter setters applied to all voices (safe to call from audio thread).
    void setFilterCutoff(float hz) noexcept;
    void setFilterResonance(float resonance) noexcept;
    void setAmpEnvelope(ADSREnvelope::Params params) noexcept;
    void setFilterEnvelope(ADSREnvelope::Params params) noexcept;
    void setMasterGain(float gain) noexcept;

    // Apply LFO config + filter env mod from a patch (block-rate).
    // Reads patch.lfo[i] and patch.filter.env_mod; LFOs themselves are smooth
    // by virtue of their phase accumulator, so a setTarget-style smoother is
    // not required for these (rate/depth changes are perceptually fine).
    void applyPatch(const PatchStruct& patch) noexcept;

    // Forward host DAW tempo to all voice LFOs for tempo-sync.
    void setHostTempo(double bpm) noexcept;
    // Release all held and sounding voices (MIDI All Notes Off).
    void allNotesOff() noexcept;

    float renderNextSample() noexcept;
    void renderBlock(float* output, int numSamples) noexcept;
    void renderBlock(float* left, float* right, int numSamples) noexcept;

    [[nodiscard]] int activeVoiceCount() const noexcept;
    [[nodiscard]] int voiceCount() const noexcept;
    // Returns MIDI note numbers of all currently active voices (for testing/UI).
    [[nodiscard]] std::vector<int> activeNotes() const;

    // Test helpers — access smoothed parameter snapshots.
    [[nodiscard]] float currentSmoothedCutoff() const noexcept { return cutoffSmoother_.current(); }
    [[nodiscard]] float currentSmoothedGain() const noexcept { return gainSmoother_.current(); }

private:
    Voice* findFreeVoice() noexcept;
    // Oldest-note policy: prefers releasing voices, then oldest held voice.
    Voice* stealVoice() noexcept;
    Voice* findVoiceForNote(int midiNote) noexcept;
    [[nodiscard]] float portamentoAlpha() const noexcept;
    [[nodiscard]] static float midiNoteToHz(int note) noexcept;
    float advanceSmoothersAndRender() noexcept;

    std::vector<Voice> voices_;
    double sampleRate_{44100.0};
    float portamentoSeconds_{0.0f};
    bool retrigger_{true};
    uint64_t noteCounter_{0};
    int voiceStealFadeSamples_{240};

    // Per-sample smoothing of block-rate parameters.
    ParamSmoother cutoffSmoother_;
    ParamSmoother resonanceSmoother_;
    ParamSmoother gainSmoother_;

    // First applyPatch after prepare() snaps the smoothers to the target
    // value so the synth doesn't glide audibly from the default on load.
    bool primed_{false};
};

} // namespace agentic_synth::engine
