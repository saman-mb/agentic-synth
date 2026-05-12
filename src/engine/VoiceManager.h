#pragma once

#include "engine/ADSREnvelope.h"
#include "engine/Delay.h"
#include "engine/Filter.h"
#include "engine/LFO.h"
#include "engine/ParamSmoother.h"
#include "engine/PatchStruct.h"
#include "engine/PatchValidator.h"
#include "engine/Reverb.h"
#include "engine/VAOscillator.h"
#include "engine/WavetableOscillator.h"

#include <array>
#include <cstdint>
#include <memory>
#include <random>
#include <vector>

namespace agentic_synth::engine {

// Per-oscillator slot inside a Voice. Phase 3: every PatchStruct osc[i] field
// has an audible consumer here. enabled gates rendering; type picks the
// renderer (VA vs Wavetable vs Noise vs Pulse vs FM); volume scales; pan
// distributes to L/R via precomputed constant-power gains; wavetable_pos
// drives wavetable morph; fm_ratio/fm_depth/pulse_width feed the FM and
// pulse paths. semitone_offset + detune_cents shift the per-osc frequency.
struct OscSlot {
    WavetableOscillator wavetableOsc;
    VAOscillator vaOsc;
    OscType type{OscType::Sawtooth};
    bool enabled{false};
    float volume{1.0f};
    float pan{0.0f};
    float panGainL{0.7071068f};
    float panGainR{0.7071068f};
    float semitoneOffset{0.0f};
    float detuneCents{0.0f};
    float wavetablePos{0.0f};
    float fmRatio{1.0f};
    float fmDepth{0.0f};
    float pulseWidth{0.5f};
    // FM modulator phase accumulator (separate from carrier's wavetableOsc phase).
    double fmPhase{0.0};
    // FM carrier phase accumulator. Phase 3 follow-up bug fix: the carrier
    // previously shared o.fmPhase with the modulator, which meant the carrier
    // ran at fc*ratio instead of fc whenever ratio != 1 (so the perceived
    // pitch was wrong for any non-1:1 FM patch). Carrier and modulator now
    // each have their own phase accumulator.
    double carrierPhase{0.0};
    // Cached sample rate for slot-local DSP (FM oscillator) so we don't have
    // to thread sampleRate_ through every renderStereo call. Set in
    // Voice::prepare. Defaults to 44.1k to match legacy hardcoded behaviour.
    float sampleRate{44100.0f};
    // White-noise RNG (small xorshift state). Independent per slot to avoid
    // identical noise per voice → audible coherence.
    uint32_t noiseRng{0x9E3779B9u};
};

// Per-voice DSP state. VoiceManager owns and allocates all voices.
struct Voice {
    int midiNote{-1};     // -1 = free slot
    bool noteIsOn{false}; // false = envelope releasing, true = held
    uint64_t noteOnOrder{0};

    float velocity{1.0f}; // [0, 1] — scales amp env peak and filter env mod

    float targetFrequency{440.0f};
    float currentFrequency{440.0f}; // slides toward target during portamento

    // Per-osc rendering slots. Slot 0 mirrors prior single-osc behaviour
    // (enabled by default, sawtooth via VA). Slots 1+ default disabled.
    std::array<OscSlot, kMaxOscillators> oscs{};

    // Active filter pointer — set to one of moogFilter / svFilter based on
    // FilterType in applyPatch. Pre-allocated; never heap-allocates at audio time.
    std::unique_ptr<MoogLadder> moogFilter;
    std::unique_ptr<SVFilter> svFilter;
    Filter* filter{nullptr};
    // Outgoing filter during a type-swap crossfade. Phase 4: when applyPatch
    // changes filter.type we keep the previous filter alive and run both in
    // parallel for kCrossfadeSamples samples, blending wet via fadeOut/fadeIn
    // ramps. Avoids the audible click of a pointer-swap with reset integrator
    // state. nullptr when not crossfading (steady state).
    Filter* crossfadeFromFilter{nullptr};
    int crossfadeRemaining{0};
    int crossfadeTotal{0};
    ADSREnvelope ampEnv;
    ADSREnvelope filterEnv;
    std::array<LFO, 2> lfos;
    DcBlocker dcBlockerL;
    DcBlocker dcBlockerR;

    // LFO routing/depth pulled from patch each block.
    std::array<LfoTarget, 2> lfoTargets{{LfoTarget::None, LfoTarget::None}};
    std::array<float, 2> lfoDepths{{0.0f, 0.0f}};

    // Filter env modulation (filter.env_mod from patch).
    float filterEnvMod{0.0f};

    // Smoothed filter drive — block-rate writer (applyPatch), per-sample reader
    // (render). Kills zipper noise on knob-driven drive moves.
    ParamSmoother driveSmoother;

    // Voice-steal fade-out: when > 0, voice output is multiplied by a linear
    // ramp from fadeOutSamplesRemaining_/fadeOutSamplesTotal_ → 0.
    int fadeOutSamplesRemaining{0};
    int fadeOutSamplesTotal{0};

    // Voice-level pan (round-robin slot offset; per-osc pan stacks on top).
    float pan{0.0f};
    float panGainL{0.7071068f};
    float panGainR{0.7071068f};

    [[nodiscard]] bool isActive() const noexcept { return ampEnv.isActive() || fadeOutSamplesRemaining > 0; }
    void prepare(double sampleRate);
    // Stereo render: each osc[i] produces a sample, scaled by volume, panned,
    // summed into L/R. baseCutoffHz/resonance come from VoiceManager
    // per-sample smoothers so LFO + filter-env modulation stack on top.
    // outL/outR are the post-filter, post-amp, post-DC-blocker stereo pair.
    void renderStereo(float portamentoAlpha, float baseCutoffHz, float resonance,
                      float& outL, float& outR) noexcept;
};

// N-voice polyphonic allocator with oldest-note stealing and portamento.
class VoiceManager {
public:
    static constexpr int kDefaultVoiceCount = 16;
    // ~5 ms at 48 kHz; recomputed in prepare() based on actual sample rate.
    static constexpr float kVoiceStealFadeSeconds = 0.005f;
    // Filter type-swap crossfade window. ~5 ms at 44.1 kHz ≈ 220 samples.
    // Cached in samples per voice in prepare().
    static constexpr float kFilterCrossfadeSeconds = 0.005f;

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

    // Snap cutoff/resonance/gain smoothers to their current targets. Call
    // after pushing a fresh batch of parameters (prepareToPlay, state load)
    // so the next audio block starts at the requested values rather than
    // gliding up from constructor defaults or stale state.
    void primeSmoothers() noexcept;

    // Apply LFO config + filter env mod from a patch (block-rate).
    // Reads patch.lfo[i] and patch.filter.env_mod; LFOs themselves are smooth
    // by virtue of their phase accumulator, so a setTarget-style smoother is
    // not required for these (rate/depth changes are perceptually fine).
    void applyPatch(const PatchStruct& patch) noexcept;

    // Forward host DAW tempo to all voice LFOs for tempo-sync.
    void setHostTempo(double bpm) noexcept;
    // Release all held and sounding voices (MIDI All Notes Off).
    void allNotesOff() noexcept;

    // Hard-clear every piece of stateful per-voice DSP and FX bus state:
    // filter integrators, DC blocker, ADSR stages, LFO phase, fade ramps,
    // delay lines, reverb combs/allpasses. Call from
    // AudioProcessor::releaseResources() and also from prepareToPlay() before
    // re-computing coefficients (some hosts skip releaseResources between
    // SR changes). Safe to call repeatedly; idempotent.
    //
    // Smoothers (cutoff/resonance/gain) are intentionally NOT reset — the
    // expected sequence is releaseResources → prepare → applyParameters →
    // primeSmoothers, and primeSmoothers snaps them to the new targets. See
    // implementation comment for the full rationale.
    void releaseResources() noexcept;

    float renderNextSample() noexcept;
    void renderBlock(float* output, int numSamples) noexcept;
    void renderBlock(float* left, float* right, int numSamples) noexcept;

    [[nodiscard]] int activeVoiceCount() const noexcept;
    [[nodiscard]] int voiceCount() const noexcept;
    // Returns MIDI note numbers of all currently active voices (for testing/UI).
    // NOT real-time safe: std::vector allocates. Audio-thread callers must use
    // a pre-allocated buffer (no current RT caller — only test/UI consumers as
    // of Phase 4). If an RT caller is added, introduce a non-allocating
    // std::span overload: `std::size_t activeNotes(std::span<int> out) const`.
    [[nodiscard]] std::vector<int> activeNotes() const;

    // Test helpers — access smoothed parameter snapshots.
    [[nodiscard]] float currentSmoothedCutoff() const noexcept { return cutoffSmoother_.current(); }
    [[nodiscard]] float currentSmoothedGain() const noexcept { return gainSmoother_.current(); }

private:
    Voice* findFreeVoice() noexcept;
    // Oldest-note policy: prefers releasing voices, then oldest held voice.
    Voice* stealVoice() noexcept;
    Voice* findVoiceForNote(int midiNote) noexcept;
    // Phase 3 / Item 3a: portamentoAlpha is cached as portamentoAlpha_ and
    // recomputed only on setPortamento / prepare. Audio thread reads the
    // cached float — no more per-sample std::exp.
    void recomputePortamentoAlpha() noexcept;
    [[nodiscard]] static float midiNoteToHz(int note) noexcept;
    float advanceSmoothersAndRender() noexcept;
    // True if voice index is within the active-polyphony cap. Lets noteOn
    // ignore voices outside the cap without resizing the underlying vector.
    [[nodiscard]] bool isWithinCap(const Voice& v) const noexcept;

    std::vector<Voice> voices_;
    double sampleRate_{44100.0};
    float portamentoSeconds_{0.0f};
    float portamentoAlpha_{0.0f}; // cached; recomputed in setPortamento + prepare
    bool retrigger_{true};
    uint64_t noteCounter_{0};
    int voiceStealFadeSamples_{240};
    // Filter type-swap crossfade length in samples (≈ 5 ms). Recomputed in
    // prepare() based on the actual sample rate.
    int filterCrossfadeSamples_{220};
    // Active polyphony cap (1..voices_.size()). voice_count from patch.
    int activeVoiceCap_{kDefaultVoiceCount};

    // Per-sample smoothing of block-rate parameters.
    ParamSmoother cutoffSmoother_;
    ParamSmoother resonanceSmoother_;
    ParamSmoother gainSmoother_;
    // Reverb width smoother — applied post-Reverb to crossfade wet from mono (0)
    // to full stereo (1) via M/S.
    ParamSmoother reverbWidthSmoother_;
    // ADSR rate smoothers (one set for amp env, one for filter env). Smooth
    // the TARGETS, not the env output — eliminates clicks when knob-moving
    // rate params during a held note. ~50ms smoothing per architect plan.
    ParamSmoother ampAttackSmoother_;
    ParamSmoother ampDecaySmoother_;
    ParamSmoother ampSustainSmoother_;
    ParamSmoother ampReleaseSmoother_;
    ParamSmoother filterEnvAttackSmoother_;
    ParamSmoother filterEnvDecaySmoother_;
    ParamSmoother filterEnvSustainSmoother_;
    ParamSmoother filterEnvReleaseSmoother_;
    // LFO depth/rate smoothers per LFO. Push raw patch values into the
    // smoother target; refresh LFO setDepth/setFreeRate at block rate.
    std::array<ParamSmoother, kMaxLfos> lfoDepthSmoothers_{};
    std::array<ParamSmoother, kMaxLfos> lfoRateSmoothers_{};

    // Block-rate cached parameters used by the per-sample stereo loop.
    // These are written by applyPatch (no atomics — single writer, audio thread)
    // and read in renderBlock to keep the inner loop branch-free.
    float reverbWidthTarget_{1.0f};
    bool delayBpmSync_{false};
    double hostBpm_{120.0};

    // First applyPatch after prepare() snaps the smoothers to the target
    // value so the synth doesn't glide audibly from the default on load.
    bool primed_{false};

    // FX bus: voices → delay → reverb. Stereo path only — mono renderBlock
    // and renderNextSample produce dry voices × master gain. Bus state has
    // no per-voice ownership; FX run once on the summed stereo signal.
    Delay delay_;
    Reverb reverb_;
};

} // namespace agentic_synth::engine
