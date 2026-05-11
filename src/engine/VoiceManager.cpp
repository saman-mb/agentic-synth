#include "engine/VoiceManager.h"

#include <algorithm>
#include <cassert>
#include <cmath>

namespace agentic_synth::engine {

namespace {

LfoShape toLfoShape(LfoWaveform w) noexcept {
    switch (w) {
    case LfoWaveform::Sine:
        return LfoShape::Sine;
    case LfoWaveform::Triangle:
        return LfoShape::Triangle;
    case LfoWaveform::Sawtooth:
        return LfoShape::Saw;
    case LfoWaveform::Square:
        return LfoShape::Square;
    case LfoWaveform::SampleAndHold:
        return LfoShape::SampleAndHold;
    }
    return LfoShape::Sine;
}

} // namespace

// ── Voice ─────────────────────────────────────────────────────────────────────

void Voice::prepare(double sampleRate) {
    wavetableOsc.setSampleRate(sampleRate);
    vaOsc.prepare(sampleRate);
    if (filter)
        filter->prepare(sampleRate);
    ampEnv.setSampleRate(sampleRate);
    filterEnv.setSampleRate(sampleRate);
    for (auto& lfo : lfos)
        lfo.setSampleRate(sampleRate);
    dcBlocker.prepare(static_cast<float>(sampleRate));
}

float Voice::render(float portamentoAlpha, float baseCutoffHz, float resonance) noexcept {
    if (!isActive())
        return 0.0f;

    // One-pole smoother: currentFrequency glides toward targetFrequency.
    currentFrequency = portamentoAlpha * currentFrequency + (1.0f - portamentoAlpha) * targetFrequency;

    // ── Modulation pass: evaluate the 2 LFOs once per sample ─────────────
    // LFO output is shape * mDepth (LFO::processSample already applies its
    // internal depth). We multiply again by the patch's per-target depth so
    // that LfoTarget routing can attenuate independent of the LFO's depth knob.
    float lfoPitchSemis = 0.0f;
    float lfoCutoffMod = 0.0f; // multiplicative around 1.0
    float lfoAmpMod = 0.0f;    // multiplicative around 1.0
    for (size_t i = 0; i < lfos.size(); ++i) {
        const float lfoOut = lfos[i].processSample(); // [-depth, +depth]
        const float d = lfoDepths[i];
        switch (lfoTargets[i]) {
        case LfoTarget::Pitch:
            lfoPitchSemis += lfoOut * d * 12.0f; // ±depth octaves max
            break;
        case LfoTarget::FilterCutoff:
            lfoCutoffMod += lfoOut * d; // ±d around 0
            break;
        case LfoTarget::Amplitude:
            lfoAmpMod += lfoOut * d * 0.5f; // halved so trough never silences voice
            break;
        case LfoTarget::None:
        case LfoTarget::Pan:
        case LfoTarget::WavetablePos:
        case LfoTarget::FmRatio:
            break;
        }
    }

    // Effective pitch with LFO pitch mod.
    float effectiveFreq = currentFrequency;
    if (lfoPitchSemis != 0.0f) {
        effectiveFreq *= std::pow(2.0f, lfoPitchSemis / 12.0f);
    }
    wavetableOsc.setFrequency(static_cast<double>(effectiveFreq));
    vaOsc.setFrequency(static_cast<double>(effectiveFreq));

    float sample = wavetableOsc.processSample() + vaOsc.processSample();

    // ── Filter modulation ────────────────────────────────────────────────
    // Composition order: smoothed base → LFO (multiplicative) → filter env (additive
    // scaled by env_mod * velocity). Clamp to audio range before writing to filter.
    const float filterEnvOut = filterEnv.process();
    float effectiveCutoff = baseCutoffHz * (1.0f + lfoCutoffMod);
    effectiveCutoff *= (1.0f + filterEnvOut * filterEnvMod * velocity * 2.0f);
    effectiveCutoff = std::clamp(effectiveCutoff, 20.0f, 20000.0f);

    if (filter) {
        filter->setCutoff(effectiveCutoff);
        filter->setResonance(resonance);
        sample = filter->process(sample);
    }

    // ── Amp envelope × velocity × LFO amp mod ────────────────────────────
    const float ampEnvOut = ampEnv.process();
    float gain = ampEnvOut * velocity * (1.0f + lfoAmpMod);

    // Voice-steal fade-out ramp (linear).
    if (fadeOutSamplesRemaining > 0) {
        const float rampGain =
            static_cast<float>(fadeOutSamplesRemaining) / static_cast<float>(fadeOutSamplesTotal);
        gain *= rampGain;
        --fadeOutSamplesRemaining;
        if (fadeOutSamplesRemaining == 0) {
            // Fade complete — force voice idle so the slot is reusable.
            ampEnv.reset();
            filterEnv.reset();
            // Reset filter integrators too: resonant patches click on reuse
            // otherwise (Moog ladder s_[0..3] state carries over).
            if (filter) filter->reset();
            dcBlocker.reset();
            noteIsOn = false;
            midiNote = -1;
        }
    }

    return dcBlocker.process(sample * gain);
}

// ── VoiceManager ──────────────────────────────────────────────────────────────

VoiceManager::VoiceManager(int voiceCount) {
    assert(voiceCount > 0);
    voices_.reserve(static_cast<std::size_t>(voiceCount));
    voices_.resize(static_cast<std::size_t>(voiceCount));
    for (auto& v : voices_) {
        v.filter = std::make_unique<MoogLadder>();
    }
    // Smoothers default to 30 Hz cutoff — see ParamSmoother.h.
    cutoffSmoother_.reset(1000.0f);
    resonanceSmoother_.reset(0.0f);
    gainSmoother_.reset(1.0f);
}

void VoiceManager::prepare(double sampleRate) {
    sampleRate_ = sampleRate;
    for (auto& v : voices_)
        v.prepare(sampleRate);
    voiceStealFadeSamples_ =
        std::max(1, static_cast<int>(std::lround(kVoiceStealFadeSeconds * sampleRate)));
    cutoffSmoother_.setSampleRate(sampleRate);
    resonanceSmoother_.setSampleRate(sampleRate);
    gainSmoother_.setSampleRate(sampleRate);
}

void VoiceManager::noteOn(int midiNote, float velocity) {
    Voice* v = findVoiceForNote(midiNote);
    if (v == nullptr)
        v = findFreeVoice();
    if (v == nullptr) {
        Voice* victim = stealVoice();
        if (victim != nullptr) {
            // Begin fade-out on the outgoing voice. Hide it from MIDI bookkeeping
            // (midiNote = -1) so it doesn't appear in activeNotes() — but keep
            // the envelope + filter state running so its tail rings out through
            // the fade ramp. The new note goes on a different slot.
            victim->fadeOutSamplesRemaining = voiceStealFadeSamples_;
            victim->fadeOutSamplesTotal = voiceStealFadeSamples_;
            victim->midiNote = -1;
            victim->noteIsOn = false;
            // After marking the victim as fading-but-occupied, look for a free
            // slot for the new note. If none, fall back to the victim itself
            // (degraded mode: still applies fade-ramp to outgoing audio while
            // the new note's envelope is retriggered).
            v = findFreeVoice();
            if (v == nullptr)
                v = victim;
        }
    }
    if (v == nullptr)
        return; // pool exhausted

    const bool wasActive = v->isActive() && v->fadeOutSamplesRemaining == 0;
    const bool portando = (portamentoSeconds_ > 0.0f) && wasActive;

    v->midiNote = midiNote;
    v->noteIsOn = true;
    v->noteOnOrder = noteCounter_++;
    v->velocity = std::clamp(velocity, 0.0f, 1.0f);
    v->targetFrequency = midiNoteToHz(midiNote);

    if (!portando) {
        v->currentFrequency = v->targetFrequency;
    }

    if (!wasActive || retrigger_) {
        v->ampEnv.noteOn();
        v->filterEnv.noteOn();
        for (auto& lfo : v->lfos)
            lfo.trigger();
    }
}

void VoiceManager::noteOff(int midiNote) {
    Voice* v = findVoiceForNote(midiNote);
    if (v == nullptr)
        return;
    v->noteIsOn = false;
    v->ampEnv.noteOff();
    v->filterEnv.noteOff();
}

void VoiceManager::setPortamento(float seconds) noexcept { portamentoSeconds_ = seconds; }
void VoiceManager::setRetrigger(bool retrigger) noexcept { retrigger_ = retrigger; }

void VoiceManager::setFilterCutoff(float hz) noexcept {
    cutoffSmoother_.setTarget(hz);
}

void VoiceManager::setFilterResonance(float resonance) noexcept {
    resonanceSmoother_.setTarget(resonance);
}

void VoiceManager::setAmpEnvelope(ADSREnvelope::Params params) noexcept {
    for (auto& v : voices_)
        v.ampEnv.setParams(params);
}

void VoiceManager::setFilterEnvelope(ADSREnvelope::Params params) noexcept {
    for (auto& v : voices_)
        v.filterEnv.setParams(params);
}

void VoiceManager::setMasterGain(float gain) noexcept {
    gainSmoother_.setTarget(gain);
}

void VoiceManager::applyPatch(const PatchStruct& patch) noexcept {
    // NaN/inf guard: a malformed PatchStruct would propagate non-finite
    // values forever through the one-pole smoothers (state += k*(NaN-state)).
    // Clamp at the boundary.
    const auto safe = [](float v, float fallback) noexcept {
        return std::isfinite(v) ? v : fallback;
    };

    // Filter cutoff/resonance & master gain → smoothed targets (block-rate writers,
    // sample-rate readers). First call after prepare() snaps to avoid an
    // audible glide from the default value on patch load.
    const float cutoffHz   = safe(patch.filter.cutoff_hz,  1000.0f);
    const float resonance  = safe(patch.filter.resonance,  0.0f);
    const float masterGain = safe(patch.master_gain,       1.0f);
    if (!primed_) {
        cutoffSmoother_.reset(cutoffHz);
        resonanceSmoother_.reset(resonance);
        gainSmoother_.reset(masterGain);
        primed_ = true;
    } else {
        cutoffSmoother_.setTarget(cutoffHz);
        resonanceSmoother_.setTarget(resonance);
        gainSmoother_.setTarget(masterGain);
    }

    // Amp + filter envelopes.
    ADSREnvelope::Params ampParams{};
    ampParams.attackSeconds = patch.amp_env.attack_s;
    ampParams.decaySeconds = patch.amp_env.decay_s;
    ampParams.sustainLevel = patch.amp_env.sustain;
    ampParams.releaseSeconds = patch.amp_env.release_s;
    setAmpEnvelope(ampParams);

    ADSREnvelope::Params filterParams{};
    filterParams.attackSeconds = patch.filter_env.attack_s;
    filterParams.decaySeconds = patch.filter_env.decay_s;
    filterParams.sustainLevel = patch.filter_env.sustain;
    filterParams.releaseSeconds = patch.filter_env.release_s;
    setFilterEnvelope(filterParams);

    // LFO routing + per-target depth + LFO internal depth/shape/rate.
    for (auto& v : voices_) {
        v.filterEnvMod = patch.filter.env_mod;
        for (size_t i = 0; i < v.lfos.size() && i < kMaxLfos; ++i) {
            const auto& lp = patch.lfo[i];
            v.lfos[i].setShape(toLfoShape(lp.waveform));
            v.lfos[i].setDepth(1.0f); // we apply patch depth at routing stage in render()
            v.lfos[i].setFreeRate(lp.rate_hz);
            v.lfos[i].setTempoSync(lp.bpm_sync != 0);
            v.lfoTargets[i] = lp.target;
            v.lfoDepths[i] = lp.depth;
        }
    }

    setPortamento(patch.portamento_s);
}

void VoiceManager::setHostTempo(double bpm) noexcept {
    for (auto& v : voices_)
        for (auto& lfo : v.lfos)
            lfo.setHostTempo(bpm);
}

void VoiceManager::allNotesOff() noexcept {
    for (auto& v : voices_) {
        v.noteIsOn = false;
        v.ampEnv.noteOff();
        v.filterEnv.noteOff();
    }
}

float VoiceManager::advanceSmoothersAndRender() noexcept {
    const float cutoff = cutoffSmoother_.process();
    const float res = resonanceSmoother_.process();
    const float gain = gainSmoother_.process();
    const float alpha = portamentoAlpha();
    float sum = 0.0f;
    for (auto& v : voices_)
        sum += v.render(alpha, cutoff, res);
    return sum * gain;
}

float VoiceManager::renderNextSample() noexcept { return advanceSmoothersAndRender(); }

void VoiceManager::renderBlock(float* output, int numSamples) noexcept {
    for (int i = 0; i < numSamples; ++i)
        output[i] = advanceSmoothersAndRender();
}

void VoiceManager::renderBlock(float* left, float* right, int numSamples) noexcept {
    for (int i = 0; i < numSamples; ++i) {
        const float s = advanceSmoothersAndRender();
        left[i] = s;
        right[i] = s;
    }
}

int VoiceManager::activeVoiceCount() const noexcept {
    int count = 0;
    for (const auto& v : voices_) {
        // Exclude voices that are alive only because of a steal-fade ramp on
        // a now-anonymous slot (midiNote == -1). They contribute audio for a
        // few ms but are no longer "playing" from the player's POV.
        if (v.isActive() && v.midiNote >= 0)
            ++count;
    }
    return count;
}

int VoiceManager::voiceCount() const noexcept { return static_cast<int>(voices_.size()); }

std::vector<int> VoiceManager::activeNotes() const {
    std::vector<int> notes;
    notes.reserve(voices_.size());
    for (const auto& v : voices_) {
        // Only report voices that are sounding *as a real note* — exclude
        // voices that are only alive due to a steal-fade ramp (midiNote = -1
        // is set when the fade completes, so during the ramp it's still the
        // outgoing note; that's the previous behavior we want preserved).
        if (v.isActive() && v.midiNote >= 0)
            notes.push_back(v.midiNote);
    }
    return notes;
}

Voice* VoiceManager::findFreeVoice() noexcept {
    for (auto& v : voices_) {
        // A voice still in a fade-out ramp is *not* free — assigning a new
        // note to it would defeat the click-suppression.
        if (!v.isActive() && v.fadeOutSamplesRemaining == 0)
            return &v;
    }
    return nullptr;
}

Voice* VoiceManager::stealVoice() noexcept {
    Voice* oldest = nullptr;

    for (auto& v : voices_) {
        if (v.fadeOutSamplesRemaining > 0)
            continue; // already being faded, don't double-fade
        if (!v.noteIsOn && v.isActive()) {
            if (oldest == nullptr || v.noteOnOrder < oldest->noteOnOrder)
                oldest = &v;
        }
    }
    if (oldest != nullptr)
        return oldest;

    for (auto& v : voices_) {
        if (v.fadeOutSamplesRemaining > 0)
            continue;
        if (v.isActive()) {
            if (oldest == nullptr || v.noteOnOrder < oldest->noteOnOrder)
                oldest = &v;
        }
    }
    return oldest;
}

Voice* VoiceManager::findVoiceForNote(int midiNote) noexcept {
    for (auto& v : voices_) {
        if (v.isActive() && v.midiNote == midiNote && v.fadeOutSamplesRemaining == 0)
            return &v;
    }
    return nullptr;
}

float VoiceManager::portamentoAlpha() const noexcept {
    if (portamentoSeconds_ <= 0.0f)
        return 0.0f;
    return static_cast<float>(std::exp(-1.0 / (portamentoSeconds_ * sampleRate_)));
}

float VoiceManager::midiNoteToHz(int note) noexcept {
    return 440.0f * std::pow(2.0f, static_cast<float>(note - 69) / 12.0f);
}

} // namespace agentic_synth::engine
