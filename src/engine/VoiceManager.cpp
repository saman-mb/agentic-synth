#include "engine/VoiceManager.h"

#include <algorithm>
#include <cassert>
#include <cmath>

namespace agentic_synth::engine {

// ── Voice ─────────────────────────────────────────────────────────────────────

void Voice::prepare(double sampleRate) {
    wavetableOsc.setSampleRate(sampleRate);
    vaOsc.prepare(sampleRate);
    if (filter)
        filter->prepare(sampleRate);
    ampEnv.setSampleRate(sampleRate);
    for (auto& lfo : lfos)
        lfo.setSampleRate(sampleRate);
    dcBlocker.prepare(static_cast<float>(sampleRate));
}

float Voice::render(float portamentoAlpha) noexcept {
    if (!isActive())
        return 0.0f;
    // One-pole smoother: currentFrequency glides toward targetFrequency.
    // alpha=0 → instant snap; alpha→1 → very slow glide.
    currentFrequency = portamentoAlpha * currentFrequency + (1.0f - portamentoAlpha) * targetFrequency;
    wavetableOsc.setFrequency(static_cast<double>(currentFrequency));
    vaOsc.setFrequency(static_cast<double>(currentFrequency));
    float sample = wavetableOsc.processSample() + vaOsc.processSample();
    if (filter)
        sample = filter->process(sample);
    return dcBlocker.process(sample * ampEnv.process());
}

// ── VoiceManager ──────────────────────────────────────────────────────────────

VoiceManager::VoiceManager(int voiceCount) {
    assert(voiceCount > 0);
    // Reserve first so resize() never triggers a reallocation — Voice contains
    // unique_ptr which makes it non-trivially-relocatable.
    voices_.reserve(static_cast<std::size_t>(voiceCount));
    voices_.resize(static_cast<std::size_t>(voiceCount));
    for (auto& v : voices_) {
        v.filter = std::make_unique<MoogLadder>();
    }
}

void VoiceManager::prepare(double sampleRate) {
    sampleRate_ = sampleRate;
    for (auto& v : voices_)
        v.prepare(sampleRate);
}

void VoiceManager::noteOn(int midiNote, float /*velocity*/) {
    // Reuse existing voice for same note, otherwise steal or allocate.
    Voice* v = findVoiceForNote(midiNote);
    if (v == nullptr)
        v = findFreeVoice();
    if (v == nullptr)
        v = stealVoice();
    if (v == nullptr)
        return; // shouldn't happen with a valid voice pool

    const bool wasActive = v->isActive();
    const bool portando = (portamentoSeconds_ > 0.0f) && wasActive;

    v->midiNote = midiNote;
    v->noteIsOn = true;
    v->noteOnOrder = noteCounter_++;
    v->targetFrequency = midiNoteToHz(midiNote);

    if (!portando) {
        // Snap frequency immediately when portamento is off or voice is cold.
        v->currentFrequency = v->targetFrequency;
    }

    if (!wasActive || retrigger_) {
        v->ampEnv.noteOn();
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
}

void VoiceManager::setPortamento(float seconds) noexcept { portamentoSeconds_ = seconds; }
void VoiceManager::setRetrigger(bool retrigger) noexcept { retrigger_ = retrigger; }

void VoiceManager::setFilterCutoff(float hz) noexcept {
    for (auto& v : voices_)
        if (v.filter)
            v.filter->setCutoff(hz);
}

void VoiceManager::setFilterResonance(float resonance) noexcept {
    for (auto& v : voices_)
        if (v.filter)
            v.filter->setResonance(resonance);
}

void VoiceManager::setAmpEnvelope(ADSREnvelope::Params params) noexcept {
    for (auto& v : voices_)
        v.ampEnv.setParams(params);
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
    }
}

float VoiceManager::renderNextSample() noexcept {
    float sum = 0.0f;
    const float alpha = portamentoAlpha();
    for (auto& v : voices_)
        sum += v.render(alpha);
    return sum;
}

void VoiceManager::renderBlock(float* output, int numSamples) noexcept {
    const float alpha = portamentoAlpha();
    for (int i = 0; i < numSamples; ++i) {
        float sum = 0.0f;
        for (auto& v : voices_)
            sum += v.render(alpha);
        output[i] = sum;
    }
}

void VoiceManager::renderBlock(float* left, float* right, int numSamples) noexcept {
    const float alpha = portamentoAlpha();
    for (int i = 0; i < numSamples; ++i) {
        float sum = 0.0f;
        for (auto& v : voices_)
            sum += v.render(alpha);
        left[i] = sum;
        right[i] = sum;
    }
}

int VoiceManager::activeVoiceCount() const noexcept {
    int count = 0;
    for (const auto& v : voices_)
        count += v.isActive() ? 1 : 0;
    return count;
}

int VoiceManager::voiceCount() const noexcept { return static_cast<int>(voices_.size()); }

std::vector<int> VoiceManager::activeNotes() const {
    std::vector<int> notes;
    notes.reserve(voices_.size());
    for (const auto& v : voices_) {
        if (v.isActive())
            notes.push_back(v.midiNote);
    }
    return notes;
}

Voice* VoiceManager::findFreeVoice() noexcept {
    for (auto& v : voices_) {
        if (!v.isActive())
            return &v;
    }
    return nullptr;
}

Voice* VoiceManager::stealVoice() noexcept {
    Voice* oldest = nullptr;

    // Prefer stealing a releasing voice (noteIsOn==false) over a held one,
    // both selected by oldest noteOnOrder.
    for (auto& v : voices_) {
        if (!v.noteIsOn && v.isActive()) {
            if (oldest == nullptr || v.noteOnOrder < oldest->noteOnOrder)
                oldest = &v;
        }
    }
    if (oldest != nullptr)
        return oldest;

    for (auto& v : voices_) {
        if (v.isActive()) {
            if (oldest == nullptr || v.noteOnOrder < oldest->noteOnOrder)
                oldest = &v;
        }
    }
    return oldest;
}

Voice* VoiceManager::findVoiceForNote(int midiNote) noexcept {
    for (auto& v : voices_) {
        if (v.isActive() && v.midiNote == midiNote)
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
