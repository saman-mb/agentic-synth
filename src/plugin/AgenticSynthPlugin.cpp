#include "plugin/AgenticSynthPlugin.h"

#include "plugin/PluginEditor.h"

#include <cmath>

using APVTS = juce::AudioProcessorValueTreeState;

namespace {

// ── Enum string arrays — order MUST match PatchStruct.h enum declarations ───

const juce::StringArray& kOscTypeNames() {
    static const juce::StringArray names{"Sine", "Triangle", "Sawtooth", "Square",
                                          "Pulse", "Wavetable", "FM", "Noise"};
    return names;
}

const juce::StringArray& kFilterTypeNames() {
    static const juce::StringArray names{"LowPass", "HighPass", "BandPass", "Notch", "Peak"};
    return names;
}

const juce::StringArray& kLfoWaveformNames() {
    static const juce::StringArray names{"Sine", "Triangle", "Sawtooth", "Square", "SampleAndHold"};
    return names;
}

const juce::StringArray& kLfoTargetNames() {
    static const juce::StringArray names{"None",        "Pitch",        "FilterCutoff", "Amplitude",
                                          "Pan",         "WavetablePos", "FmRatio"};
    return names;
}

// Per-osc / per-lfo parameter ID helpers. Keep stable across releases — these
// strings are persisted in the host project state.
juce::String oscId(int i, const char* field) { return juce::String("osc") + juce::String(i) + "_" + field; }
juce::String lfoId(int i, const char* field) { return juce::String("lfo") + juce::String(i) + "_" + field; }

// Clamp helpers used by writePatchToApvts; safe-guard non-finite values before
// they reach setValueNotifyingHost (which would propagate NaN into the param's
// internal atomic).
float safeFloat(float v, float fallback) noexcept { return std::isfinite(v) ? v : fallback; }

template <typename Enum>
int safeEnumIndex(Enum e, int maxIndexInclusive) noexcept {
    const int v = static_cast<int>(e);
    return (v < 0 || v > maxIndexInclusive) ? 0 : v;
}

} // namespace

//==============================================================================
APVTS::ParameterLayout AgenticSynthPlugin::createParameterLayout() {
    using FloatParam = juce::AudioParameterFloat;
    using ChoiceParam = juce::AudioParameterChoice;
    using BoolParam = juce::AudioParameterBool;
    using IntParam = juce::AudioParameterInt;
    using Range = juce::NormalisableRange<float>;

    APVTS::ParameterLayout layout;
    const auto defaults = agentic_synth::make_default_patch();

    // ── Global ─────────────────────────────────────────────────────────────
    layout.add(std::make_unique<FloatParam>("masterGain", "Master Gain", Range(0.0f, 1.0f), defaults.master_gain));
    // Portamento: log skew so small values get more knob travel.
    layout.add(std::make_unique<FloatParam>("portamento", "Portamento",
                                            Range(0.0f, 5.0f, 0.0f, 0.5f), defaults.portamento_s));
    // voice_count: Phase 3 wired this — VoiceManager::applyPatch caps active
    // voice allocation to voices_[0..voice_count) via activeVoiceCap_.
    layout.add(std::make_unique<IntParam>("voice_count", "Voice Count", 1, 16,
                                          static_cast<int>(defaults.voice_count)));

    // ── Filter ─────────────────────────────────────────────────────────────
    // filter_type: Phase 3 wired this — applyPatch pointer-swaps between the
    // pre-allocated MoogLadder (LowPass) and SVFilter (HP/BP/Notch/Peak) per
    // voice. Phase 4 added a ~5ms crossfade on type change for click-free swap.
    layout.add(std::make_unique<ChoiceParam>("filter_type", "Filter Type", kFilterTypeNames(),
                                             safeEnumIndex(defaults.filter.type,
                                                           kFilterTypeNames().size() - 1)));
    layout.add(std::make_unique<FloatParam>("filterCutoff", "Filter Cutoff",
                                            Range(20.0f, 20000.0f, 0.0f, 0.25f),
                                            defaults.filter.cutoff_hz));
    layout.add(std::make_unique<FloatParam>("filterResonance", "Filter Resonance",
                                            Range(0.0f, 1.0f), defaults.filter.resonance));
    layout.add(std::make_unique<FloatParam>("filter_env_mod", "Filter Env Mod",
                                            Range(-1.0f, 1.0f), defaults.filter.env_mod));
    layout.add(std::make_unique<FloatParam>("filter_key_track", "Filter Key Track",
                                            Range(0.0f, 1.0f), defaults.filter.key_track));
    layout.add(std::make_unique<FloatParam>("filter_drive", "Filter Drive",
                                            Range(0.0f, 1.0f), defaults.filter.drive));

    // ── Amp envelope ───────────────────────────────────────────────────────
    layout.add(std::make_unique<FloatParam>("ampAttack", "Amp Attack",
                                            Range(0.001f, 10.0f, 0.0f, 0.3f), defaults.amp_env.attack_s));
    layout.add(std::make_unique<FloatParam>("ampDecay", "Amp Decay",
                                            Range(0.001f, 10.0f, 0.0f, 0.3f), defaults.amp_env.decay_s));
    layout.add(std::make_unique<FloatParam>("ampSustain", "Amp Sustain",
                                            Range(0.0f, 1.0f), defaults.amp_env.sustain));
    layout.add(std::make_unique<FloatParam>("ampRelease", "Amp Release",
                                            Range(0.001f, 20.0f, 0.0f, 0.3f), defaults.amp_env.release_s));

    // ── Filter envelope ────────────────────────────────────────────────────
    layout.add(std::make_unique<FloatParam>("filter_env_attack", "Filter Env Attack",
                                            Range(0.001f, 10.0f, 0.0f, 0.3f), defaults.filter_env.attack_s));
    layout.add(std::make_unique<FloatParam>("filter_env_decay", "Filter Env Decay",
                                            Range(0.001f, 10.0f, 0.0f, 0.3f), defaults.filter_env.decay_s));
    layout.add(std::make_unique<FloatParam>("filter_env_sustain", "Filter Env Sustain",
                                            Range(0.0f, 1.0f), defaults.filter_env.sustain));
    layout.add(std::make_unique<FloatParam>("filter_env_release", "Filter Env Release",
                                            Range(0.001f, 20.0f, 0.0f, 0.3f), defaults.filter_env.release_s));

    // ── Oscillators (3 × 10 fields) ───────────────────────────────────────
    // Phase 3 wired per-osc enabled/pan/volume/type: Voice::renderStereo
    // iterates each enabled osc, sums into mono filter input, then re-splits
    // L/R via volume-weighted per-osc pan (panWeightL/R). Type drives a
    // switch dispatching to VA / Wavetable / FM / Noise paths.
    for (int i = 0; i < agentic_synth::kMaxOscillators; ++i) {
        const auto& d = defaults.osc[i];
        layout.add(std::make_unique<ChoiceParam>(oscId(i, "type"),
                                                 juce::String("Osc ") + juce::String(i) + " Type",
                                                 kOscTypeNames(),
                                                 safeEnumIndex(d.type, kOscTypeNames().size() - 1)));
        layout.add(std::make_unique<FloatParam>(oscId(i, "detune_cents"),
                                                juce::String("Osc ") + juce::String(i) + " Detune",
                                                Range(-100.0f, 100.0f), d.detune_cents));
        layout.add(std::make_unique<FloatParam>(oscId(i, "semis"),
                                                juce::String("Osc ") + juce::String(i) + " Semitones",
                                                Range(-48.0f, 48.0f), d.semitone_offset));
        layout.add(std::make_unique<FloatParam>(oscId(i, "wavetable_pos"),
                                                juce::String("Osc ") + juce::String(i) + " Wavetable Pos",
                                                Range(0.0f, 1.0f), d.wavetable_pos));
        layout.add(std::make_unique<FloatParam>(oscId(i, "fm_ratio"),
                                                juce::String("Osc ") + juce::String(i) + " FM Ratio",
                                                Range(0.5f, 16.0f), d.fm_ratio));
        layout.add(std::make_unique<FloatParam>(oscId(i, "fm_depth"),
                                                juce::String("Osc ") + juce::String(i) + " FM Depth",
                                                Range(0.0f, 1.0f), d.fm_depth));
        layout.add(std::make_unique<FloatParam>(oscId(i, "pulse_width"),
                                                juce::String("Osc ") + juce::String(i) + " Pulse Width",
                                                Range(0.01f, 0.99f), d.pulse_width));
        layout.add(std::make_unique<FloatParam>(oscId(i, "pan"),
                                                juce::String("Osc ") + juce::String(i) + " Pan",
                                                Range(-1.0f, 1.0f), d.pan));
        layout.add(std::make_unique<FloatParam>(oscId(i, "volume"),
                                                juce::String("Osc ") + juce::String(i) + " Volume",
                                                Range(0.0f, 1.0f), d.volume));
        layout.add(std::make_unique<BoolParam>(oscId(i, "enabled"),
                                               juce::String("Osc ") + juce::String(i) + " Enabled",
                                               d.enabled != 0));
    }

    // ── LFOs (2 × 6 fields) ───────────────────────────────────────────────
    for (int i = 0; i < agentic_synth::kMaxLfos; ++i) {
        const auto& d = defaults.lfo[i];
        layout.add(std::make_unique<ChoiceParam>(lfoId(i, "waveform"),
                                                 juce::String("LFO ") + juce::String(i) + " Waveform",
                                                 kLfoWaveformNames(),
                                                 safeEnumIndex(d.waveform, kLfoWaveformNames().size() - 1)));
        // Phase 3 wired all LFO targets: Pitch / FilterCutoff / Amplitude /
        // Pan / WavetablePos / FmRatio. Phase 5 dropped the slot-0 gate so
        // Pan / WavetablePos / FmRatio modulate every enabled osc uniformly.
        layout.add(std::make_unique<ChoiceParam>(lfoId(i, "target"),
                                                 juce::String("LFO ") + juce::String(i) + " Target",
                                                 kLfoTargetNames(),
                                                 safeEnumIndex(d.target, kLfoTargetNames().size() - 1)));
        layout.add(std::make_unique<FloatParam>(lfoId(i, "rate_hz"),
                                                juce::String("LFO ") + juce::String(i) + " Rate",
                                                Range(0.01f, 20.0f, 0.0f, 0.4f), d.rate_hz));
        layout.add(std::make_unique<FloatParam>(lfoId(i, "depth"),
                                                juce::String("LFO ") + juce::String(i) + " Depth",
                                                Range(0.0f, 1.0f), d.depth));
        layout.add(std::make_unique<FloatParam>(lfoId(i, "phase"),
                                                juce::String("LFO ") + juce::String(i) + " Phase",
                                                Range(0.0f, 1.0f), d.phase_offset));
        layout.add(std::make_unique<BoolParam>(lfoId(i, "bpm_sync"),
                                               juce::String("LFO ") + juce::String(i) + " BPM Sync",
                                               d.bpm_sync != 0));
    }

    // ── Reverb ─────────────────────────────────────────────────────────────
    layout.add(std::make_unique<FloatParam>("reverb_size", "Reverb Size", Range(0.0f, 1.0f), defaults.reverb.size));
    layout.add(std::make_unique<FloatParam>("reverb_damping", "Reverb Damping", Range(0.0f, 1.0f),
                                            defaults.reverb.damping));
    // reverb_width: Phase 3 wired this as an M/S blend on the reverb output
    // (width=0 collapses to mono, width=1 is full stereo pass-through).
    // Smoothed via reverbWidthSmoother_ (~50ms) with snap on first apply.
    layout.add(std::make_unique<FloatParam>("reverb_width", "Reverb Width", Range(0.0f, 1.0f),
                                            defaults.reverb.width));
    layout.add(std::make_unique<FloatParam>("reverb_mix", "Reverb Mix", Range(0.0f, 1.0f), defaults.reverb.mix));

    // ── Delay ──────────────────────────────────────────────────────────────
    layout.add(std::make_unique<FloatParam>("delay_time", "Delay Time",
                                            Range(0.001f, 2.0f, 0.0f, 0.4f), defaults.delay.time_s));
    layout.add(std::make_unique<FloatParam>("delay_feedback", "Delay Feedback",
                                            Range(0.0f, 0.99f), defaults.delay.feedback));
    layout.add(std::make_unique<FloatParam>("delay_mix", "Delay Mix", Range(0.0f, 1.0f), defaults.delay.mix));
    layout.add(std::make_unique<FloatParam>("delay_stereo", "Delay Stereo", Range(0.0f, 1.0f),
                                            defaults.delay.stereo));
    // delay_bpm_sync: Phase 3 wired this. When true, delay.time_s is
    // reinterpreted as beats (1.0=quarter, 0.5=eighth, etc) and converted to
    // seconds via host BPM (or 120 BPM fallback when no AudioPlayHead).
    layout.add(std::make_unique<BoolParam>("delay_bpm_sync", "Delay BPM Sync", defaults.delay.bpm_sync != 0));

    return layout;
}

//==============================================================================
AgenticSynthPlugin::AgenticSynthPlugin()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      midiHandler_(voiceManager_), apvts_(*this, nullptr, "Parameters", createParameterLayout()) {
    cacheParameterHandles();

    // Phase 7A / SPSC migration: audition keyboard fan-in via wait-free queue.
    // See AgenticSynthPlugin.h header comments for the full contract.
    agentBridge_.setMidiNoteSink([this](int note, float velocity, bool isNoteOn) {
        const int vel = juce::jlimit(0, 127, static_cast<int>(std::round(velocity * 127.0f)));
        const auto msg = isNoteOn ? agentic_synth::engine::RawMidiMsg::noteOn(note, vel, /*ch=*/0)
                                  : agentic_synth::engine::RawMidiMsg::noteOff(note, /*ch=*/0);
        (void)auditionQueue_.push(msg);
    });

    // Phase 2 (Item #4): AgentBridge → APVTS pump runs on the message thread,
    // not the audio thread. juce::Timer is a MessageManager callback, so
    // pollPatch + writePatchToApvts (which calls setValueNotifyingHost) both
    // run cleanly off the RT path. The audio thread reads the resulting
    // APVTS state via applyParameters() next block — single source of truth.
    startTimer(kAgentBridgePollIntervalMs);
}

AgenticSynthPlugin::~AgenticSynthPlugin() {
    stopTimer();
    agentBridge_.setMidiNoteSink(nullptr);
}

void AgenticSynthPlugin::cacheParameterHandles() noexcept {
    // Phase 2 follow-up (Code Fix 1): cache concrete parameter types via
    // dynamic_cast + jassert. The audio thread then reads typed pointers
    // directly — no per-block static_cast on RangedAudioParameter*.
    //
    // dynamic_cast at cache time costs nothing (called from ctor only) and
    // catches any future param ID that gets wired to the wrong concrete type
    // at the moment of construction rather than as RT-path UB.

    auto choice = [this](const juce::String& id) {
        auto* casted = dynamic_cast<juce::AudioParameterChoice*>(apvts_.getParameter(id));
        jassert(casted != nullptr); // param id missing or wrong concrete type
        return casted;
    };
    auto boolean = [this](const juce::String& id) {
        auto* casted = dynamic_cast<juce::AudioParameterBool*>(apvts_.getParameter(id));
        jassert(casted != nullptr);
        return casted;
    };
    auto integer = [this](const juce::String& id) {
        auto* casted = dynamic_cast<juce::AudioParameterInt*>(apvts_.getParameter(id));
        jassert(casted != nullptr);
        return casted;
    };

    // Float param shadows — getRawParameterValue is documented RT-safe (atomic).
    masterGainParam_ = apvts_.getRawParameterValue("masterGain");
    portamentoParam_ = apvts_.getRawParameterValue("portamento");
    voiceCountParam_ = integer("voice_count");

    filterTypeParam_ = choice("filter_type");
    filterCutoffParam_ = apvts_.getRawParameterValue("filterCutoff");
    filterResParam_ = apvts_.getRawParameterValue("filterResonance");
    filterEnvModParam_ = apvts_.getRawParameterValue("filter_env_mod");
    filterKeyTrackParam_ = apvts_.getRawParameterValue("filter_key_track");
    filterDriveParam_ = apvts_.getRawParameterValue("filter_drive");

    ampAttackParam_ = apvts_.getRawParameterValue("ampAttack");
    ampDecayParam_ = apvts_.getRawParameterValue("ampDecay");
    ampSustainParam_ = apvts_.getRawParameterValue("ampSustain");
    ampReleaseParam_ = apvts_.getRawParameterValue("ampRelease");

    filterEnvAttackParam_ = apvts_.getRawParameterValue("filter_env_attack");
    filterEnvDecayParam_ = apvts_.getRawParameterValue("filter_env_decay");
    filterEnvSustainParam_ = apvts_.getRawParameterValue("filter_env_sustain");
    filterEnvReleaseParam_ = apvts_.getRawParameterValue("filter_env_release");

    for (int i = 0; i < agentic_synth::kMaxOscillators; ++i) {
        auto& c = oscParams_[static_cast<std::size_t>(i)];
        const auto p = juce::String("osc") + juce::String(i) + "_";
        c.type = choice(p + "type");
        c.detune_cents = apvts_.getRawParameterValue(p + "detune_cents");
        c.semis = apvts_.getRawParameterValue(p + "semis");
        c.wavetable_pos = apvts_.getRawParameterValue(p + "wavetable_pos");
        c.fm_ratio = apvts_.getRawParameterValue(p + "fm_ratio");
        c.fm_depth = apvts_.getRawParameterValue(p + "fm_depth");
        c.pulse_width = apvts_.getRawParameterValue(p + "pulse_width");
        c.pan = apvts_.getRawParameterValue(p + "pan");
        c.volume = apvts_.getRawParameterValue(p + "volume");
        c.enabled = boolean(p + "enabled");
    }

    for (int i = 0; i < agentic_synth::kMaxLfos; ++i) {
        auto& c = lfoParams_[static_cast<std::size_t>(i)];
        const auto p = juce::String("lfo") + juce::String(i) + "_";
        c.waveform = choice(p + "waveform");
        c.target = choice(p + "target");
        c.rate_hz = apvts_.getRawParameterValue(p + "rate_hz");
        c.depth = apvts_.getRawParameterValue(p + "depth");
        c.phase = apvts_.getRawParameterValue(p + "phase");
        c.bpm_sync = boolean(p + "bpm_sync");
    }

    reverbSizeParam_ = apvts_.getRawParameterValue("reverb_size");
    reverbDampingParam_ = apvts_.getRawParameterValue("reverb_damping");
    reverbWidthParam_ = apvts_.getRawParameterValue("reverb_width");
    reverbMixParam_ = apvts_.getRawParameterValue("reverb_mix");

    delayTimeParam_ = apvts_.getRawParameterValue("delay_time");
    delayFeedbackParam_ = apvts_.getRawParameterValue("delay_feedback");
    delayMixParam_ = apvts_.getRawParameterValue("delay_mix");
    delayStereoParam_ = apvts_.getRawParameterValue("delay_stereo");
    delayBpmSyncParam_ = boolean("delay_bpm_sync");
}

//==============================================================================
void AgenticSynthPlugin::prepareToPlay(double sampleRate, int samplesPerBlock) {
    juce::ignoreUnused(samplesPerBlock);

    voiceManager_.releaseResources();
    voiceManager_.prepare(sampleRate);
    while (auditionQueue_.pop().has_value()) {
    }
    // Push APVTS state to the engine via the single-source-of-truth path.
    applyParameters();
    voiceManager_.primeSmoothers();
}

void AgenticSynthPlugin::releaseResources() {
    voiceManager_.releaseResources();
    while (auditionQueue_.pop().has_value()) {
    }
}

//==============================================================================
agentic_synth::PatchStruct AgenticSynthPlugin::buildPatchFromApvts() const noexcept {
    using namespace agentic_synth;
    PatchStruct p{};
    p.version = kPatchStructVersion;
    p.patch_id = 0;

    // Global
    p.master_gain = masterGainParam_->load();
    p.portamento_s = portamentoParam_->load();
    p.voice_count = static_cast<uint8_t>(juce::jlimit(1, 16, voiceCountParam_->get()));

    // Filter
    p.filter.type = static_cast<FilterType>(filterTypeParam_->getIndex());
    p.filter.cutoff_hz = filterCutoffParam_->load();
    p.filter.resonance = filterResParam_->load();
    p.filter.env_mod = filterEnvModParam_->load();
    p.filter.key_track = filterKeyTrackParam_->load();
    p.filter.drive = filterDriveParam_->load();

    // Amp envelope
    p.amp_env.attack_s = ampAttackParam_->load();
    p.amp_env.decay_s = ampDecayParam_->load();
    p.amp_env.sustain = ampSustainParam_->load();
    p.amp_env.release_s = ampReleaseParam_->load();

    // Filter envelope
    p.filter_env.attack_s = filterEnvAttackParam_->load();
    p.filter_env.decay_s = filterEnvDecayParam_->load();
    p.filter_env.sustain = filterEnvSustainParam_->load();
    p.filter_env.release_s = filterEnvReleaseParam_->load();

    // Oscillators
    for (int i = 0; i < kMaxOscillators; ++i) {
        const auto& c = oscParams_[static_cast<std::size_t>(i)];
        auto& d = p.osc[i];
        d.type = static_cast<OscType>(c.type->getIndex());
        d.detune_cents = c.detune_cents->load();
        d.semitone_offset = c.semis->load();
        d.wavetable_pos = c.wavetable_pos->load();
        d.fm_ratio = c.fm_ratio->load();
        d.fm_depth = c.fm_depth->load();
        d.pulse_width = c.pulse_width->load();
        d.pan = c.pan->load();
        d.volume = c.volume->load();
        d.enabled = c.enabled->get() ? 1 : 0;
    }

    // LFOs
    for (int i = 0; i < kMaxLfos; ++i) {
        const auto& c = lfoParams_[static_cast<std::size_t>(i)];
        auto& d = p.lfo[i];
        d.waveform = static_cast<LfoWaveform>(c.waveform->getIndex());
        d.target = static_cast<LfoTarget>(c.target->getIndex());
        d.rate_hz = c.rate_hz->load();
        d.depth = c.depth->load();
        d.phase_offset = c.phase->load();
        d.bpm_sync = c.bpm_sync->get() ? 1 : 0;
    }

    // Reverb
    p.reverb.size = reverbSizeParam_->load();
    p.reverb.damping = reverbDampingParam_->load();
    p.reverb.width = reverbWidthParam_->load();
    p.reverb.mix = reverbMixParam_->load();

    // Delay
    p.delay.time_s = delayTimeParam_->load();
    p.delay.feedback = delayFeedbackParam_->load();
    p.delay.mix = delayMixParam_->load();
    p.delay.stereo = delayStereoParam_->load();
    p.delay.bpm_sync = delayBpmSyncParam_->get() ? 1 : 0;

    return p;
}

void AgenticSynthPlugin::applyParameters() noexcept {
    // Single source of truth: read APVTS → build PatchStruct → push to engine
    // via VoiceManager::applyPatch (which already walks every field, including
    // cutoff/res/gain smoothers, envelopes, LFOs, reverb, delay).
    //
    // No dirty-flag short-circuit. With ~70 params behind the read, the
    // patch build is a handful of nanoseconds and applyPatch itself is cheap
    // (per-voice writes are unguarded but trivial). Keeping the path
    // unconditional eliminates the entire class of "shadow-cache stale after
    // state restore" bugs that bit Phase 1.
    const auto patch = buildPatchFromApvts();
    voiceManager_.applyPatch(patch);

    // Phase 2 follow-up (Code Fix 6): a message-thread writePatchToApvts may
    // have requested smoothers be snapped to the new targets. Doing it here
    // (audio thread, after applyPatch has pushed new targets) is the only
    // race-free place — smoother state has a single writer.
    if (pendingPrime_.exchange(false, std::memory_order_acq_rel))
        voiceManager_.primeSmoothers();
}

//==============================================================================
void AgenticSynthPlugin::writePatchToApvts(const agentic_synth::PatchStruct& patch) {
    // Called on the message thread (from juce::Timer or test harness).
    // setValueNotifyingHost writes the parameter's atomic AND notifies the
    // host so automation lanes / GUIs reflect the AI-driven change. The
    // audio thread reads the new APVTS state through applyParameters() on
    // the next processBlock — no direct engine write happens here.

    using namespace agentic_synth;

    auto setFloat = [&](const char* id, float value, float fallback) {
        if (auto* p = apvts_.getParameter(id))
            p->setValueNotifyingHost(p->convertTo0to1(safeFloat(value, fallback)));
    };
    auto setChoice = [&](const char* id, int index, int maxIndex) {
        if (auto* p = apvts_.getParameter(id)) {
            const int clamped = juce::jlimit(0, maxIndex, index);
            p->setValueNotifyingHost(p->convertTo0to1(static_cast<float>(clamped)));
        }
    };
    auto setBool = [&](const char* id, bool value) {
        if (auto* p = apvts_.getParameter(id))
            p->setValueNotifyingHost(value ? 1.0f : 0.0f);
    };

    setFloat("masterGain", patch.master_gain, 0.8f);
    setFloat("portamento", patch.portamento_s, 0.0f);
    // Phase 2 follow-up (Code Fix 2): trust the patch value; clamp to the
    // param range only. Substituting "sensible defaults" (8) for voice_count
    // == 0 silently rewrote AI intent. make_default_patch is now the single
    // source of truth for defaults — see PatchStruct.h.
    if (auto* p = apvts_.getParameter("voice_count")) {
        const int v = juce::jlimit(1, 16, static_cast<int>(patch.voice_count));
        p->setValueNotifyingHost(p->convertTo0to1(static_cast<float>(v)));
    }

    setChoice("filter_type", safeEnumIndex(patch.filter.type, kFilterTypeNames().size() - 1),
              kFilterTypeNames().size() - 1);
    setFloat("filterCutoff", patch.filter.cutoff_hz, 5000.0f);
    setFloat("filterResonance", patch.filter.resonance, 0.0f);
    setFloat("filter_env_mod", patch.filter.env_mod, 0.0f);
    setFloat("filter_key_track", patch.filter.key_track, 0.0f);
    setFloat("filter_drive", patch.filter.drive, 0.0f);

    setFloat("ampAttack", patch.amp_env.attack_s, 0.005f);
    setFloat("ampDecay", patch.amp_env.decay_s, 0.1f);
    setFloat("ampSustain", patch.amp_env.sustain, 1.0f);
    setFloat("ampRelease", patch.amp_env.release_s, 0.1f);

    setFloat("filter_env_attack", patch.filter_env.attack_s, 0.01f);
    setFloat("filter_env_decay", patch.filter_env.decay_s, 0.2f);
    setFloat("filter_env_sustain", patch.filter_env.sustain, 0.0f);
    setFloat("filter_env_release", patch.filter_env.release_s, 0.1f);

    for (int i = 0; i < kMaxOscillators; ++i) {
        const auto& o = patch.osc[i];
        const auto prefix = std::string("osc") + std::to_string(i) + "_";
        setChoice((prefix + "type").c_str(),
                  safeEnumIndex(o.type, kOscTypeNames().size() - 1),
                  kOscTypeNames().size() - 1);
        setFloat((prefix + "detune_cents").c_str(), o.detune_cents, 0.0f);
        setFloat((prefix + "semis").c_str(), o.semitone_offset, 0.0f);
        setFloat((prefix + "wavetable_pos").c_str(), o.wavetable_pos, 0.0f);
        // Phase 2 follow-up (Code Fix 2): no zero → "sensible default" rewrite.
        // The param's NormalisableRange clamps to [0.5, 16] / [0.01, 0.99]
        // already; that is the correct behaviour. Substituting 1.0f / 0.5f
        // would silently override AI intent for edge cases (extreme PW).
        setFloat((prefix + "fm_ratio").c_str(), o.fm_ratio, 1.0f);
        setFloat((prefix + "fm_depth").c_str(), o.fm_depth, 0.0f);
        setFloat((prefix + "pulse_width").c_str(), o.pulse_width, 0.5f);
        setFloat((prefix + "pan").c_str(), o.pan, 0.0f);
        setFloat((prefix + "volume").c_str(), o.volume, i == 0 ? 1.0f : 0.0f);
        setBool((prefix + "enabled").c_str(), o.enabled != 0);
    }

    for (int i = 0; i < kMaxLfos; ++i) {
        const auto& l = patch.lfo[i];
        const auto prefix = std::string("lfo") + std::to_string(i) + "_";
        setChoice((prefix + "waveform").c_str(),
                  safeEnumIndex(l.waveform, kLfoWaveformNames().size() - 1),
                  kLfoWaveformNames().size() - 1);
        setChoice((prefix + "target").c_str(),
                  safeEnumIndex(l.target, kLfoTargetNames().size() - 1),
                  kLfoTargetNames().size() - 1);
        setFloat((prefix + "rate_hz").c_str(), l.rate_hz, 1.0f);
        setFloat((prefix + "depth").c_str(), l.depth, 0.0f);
        setFloat((prefix + "phase").c_str(), l.phase_offset, 0.0f);
        setBool((prefix + "bpm_sync").c_str(), l.bpm_sync != 0);
    }

    setFloat("reverb_size", patch.reverb.size, 0.5f);
    setFloat("reverb_damping", patch.reverb.damping, 0.5f);
    setFloat("reverb_width", patch.reverb.width, 0.5f);
    setFloat("reverb_mix", patch.reverb.mix, 0.0f);

    setFloat("delay_time", patch.delay.time_s, 0.25f);
    setFloat("delay_feedback", patch.delay.feedback, 0.3f);
    setFloat("delay_mix", patch.delay.mix, 0.0f);
    setFloat("delay_stereo", patch.delay.stereo, 0.5f);
    setBool("delay_bpm_sync", patch.delay.bpm_sync != 0);

    // Phase 2 follow-up (Code Fix 6): request the audio thread snap its
    // smoothers to the new targets at the top of the next processBlock so
    // the AI patch is heard at its intended values (no glide-up from
    // whatever the smoothers were settling on). Done via atomic flag rather
    // than directly calling primeSmoothers from this (message) thread —
    // smoother state is owned by the audio thread.
    pendingPrime_.store(true, std::memory_order_release);
}

//==============================================================================
void AgenticSynthPlugin::pumpAgentBridgePatchQueue() {
    // Message thread only. AgentBridge::pollPatch is a wait-free SPSC pop —
    // safe to call here. Drain until empty so an LLM stream that emitted
    // several partial patches between Timer ticks is fully reflected before
    // we return.
    while (const auto patch = agentBridge_.pollPatch()) {
        writePatchToApvts(*patch);
    }
}

void AgenticSynthPlugin::timerCallback() { pumpAgentBridgePatchQueue(); }

//==============================================================================
void AgenticSynthPlugin::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) {
    juce::ScopedNoDenormals noDenormals;

    // Phase 4: RT thread tripwire stamping for AgentBridge::dispatch.
    agentBridge_.markAudioThread();

    // Sync DAW transport tempo BEFORE applyParameters so the patch's
    // delay.bpm_sync (Phase 3) sees the correct host BPM when converting
    // delay.time_s from beat-fractions to seconds.
    if (auto* ph = getPlayHead()) {
        if (const auto pos = ph->getPosition())
            if (const auto bpm = pos->getBpm())
                voiceManager_.setHostTempo(*bpm);
    }

    applyParameters();

    // Phase 2 (Item #4): AgentBridge::pollPatch is NO LONGER called here.
    // AI patches now flow via the message-thread Timer → writePatchToApvts →
    // APVTS → applyParameters above. Single source of truth = APVTS.

    // Drain audition-keyboard MIDI directly into MidiHandler.
    for (int drained = 0; drained < kAuditionDrainBudget; ++drained) {
        const auto popped = auditionQueue_.pop();
        if (!popped.has_value())
            break;
        midiHandler_.process(*popped);
    }

    // Route all host MIDI through MidiHandler.
    for (const auto metadata : midiMessages) {
        const auto& msg = metadata.getMessage();
        const auto* raw = msg.getRawData();
        const int sz = msg.getRawDataSize();
        agentic_synth::engine::RawMidiMsg rmsg;
        rmsg.status = raw[0];
        rmsg.data1 = sz > 1 ? raw[1] : 0;
        rmsg.data2 = sz > 2 ? raw[2] : 0;
        midiHandler_.process(rmsg);

        if (msg.isController())
            agentBridge_.onMidiCC(msg.getControllerNumber(), msg.getControllerValue());
    }

    const int numSamples = buffer.getNumSamples();
    buffer.clear();

    const int numChannels = buffer.getNumChannels();
    if (numChannels >= 2) {
        voiceManager_.renderBlock(buffer.getWritePointer(0), buffer.getWritePointer(1), numSamples);
    } else {
        voiceManager_.renderBlock(buffer.getWritePointer(0), numSamples);
    }
}

//==============================================================================
juce::AudioProcessorEditor* AgenticSynthPlugin::createEditor() { return new AgenticSynthPluginEditor(*this); }

bool AgenticSynthPlugin::hasEditor() const { return true; }

//==============================================================================
const juce::String AgenticSynthPlugin::getName() const { return "TIMBRE"; }

bool AgenticSynthPlugin::acceptsMidi() const { return true; }
bool AgenticSynthPlugin::producesMidi() const { return false; }
bool AgenticSynthPlugin::isMidiEffect() const { return false; }
// Phase 2 follow-up (Code Fix 5): reverb max-size tail is ~4-5 s and delay
// tail under maximum feedback is ~2 s; 5 s is a safe upper bound for hosts
// that decide when offline-bounce can stop rendering.
double AgenticSynthPlugin::getTailLengthSeconds() const { return 5.0; }

bool AgenticSynthPlugin::isBusesLayoutSupported(const BusesLayout& layouts) const {
    // Phase 2 follow-up (Code Fix 5): accept mono or stereo on the main
    // output. Synth has no main input bus (BusesProperties only declares an
    // output) so input matching is not required.
    const auto& mainOut = layouts.getMainOutputChannelSet();
    if (mainOut != juce::AudioChannelSet::stereo() && mainOut != juce::AudioChannelSet::mono())
        return false;
    return true;
}

//==============================================================================
int AgenticSynthPlugin::getNumPrograms() { return 1; }
int AgenticSynthPlugin::getCurrentProgram() { return 0; }
void AgenticSynthPlugin::setCurrentProgram(int) {}
const juce::String AgenticSynthPlugin::getProgramName(int) { return {}; }
void AgenticSynthPlugin::changeProgramName(int, const juce::String&) {}

//==============================================================================
void AgenticSynthPlugin::getStateInformation(juce::MemoryBlock& destData) {
    const auto state = apvts_.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void AgenticSynthPlugin::setStateInformation(const void* data, int sizeInBytes) {
    std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, sizeInBytes));
    if (xml == nullptr || !xml->hasTagName(apvts_.state.getType()))
        return;

    apvts_.replaceState(juce::ValueTree::fromXml(*xml));
    // Push the freshly-restored state to the engine and snap smoothers so the
    // restore is audible immediately without a glide from stale targets.
    applyParameters();
    voiceManager_.primeSmoothers();
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new AgenticSynthPlugin(); }
