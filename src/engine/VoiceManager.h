#pragma once

#include "engine/ADSREnvelope.h"
#include "engine/Filter.h"
#include "engine/LFO.h"
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

    float targetFrequency{440.0f};
    float currentFrequency{440.0f}; // slides toward target during portamento

    WavetableOscillator wavetableOsc;
    VAOscillator vaOsc;
    std::unique_ptr<Filter> filter; // MoogLadder by default
    ADSREnvelope ampEnv;
    std::array<LFO, 2> lfos;

    [[nodiscard]] bool isActive() const noexcept { return ampEnv.isActive(); }
    void prepare(double sampleRate);
    float render(float portamentoAlpha) noexcept;
};

// N-voice polyphonic allocator with oldest-note stealing and portamento.
class VoiceManager {
public:
    static constexpr int kDefaultVoiceCount = 16;

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

    float renderNextSample() noexcept;
    void renderBlock(float* output, int numSamples) noexcept;

    [[nodiscard]] int activeVoiceCount() const noexcept;
    [[nodiscard]] int voiceCount() const noexcept;
    // Returns MIDI note numbers of all currently active voices (for testing/UI).
    [[nodiscard]] std::vector<int> activeNotes() const;

private:
    Voice* findFreeVoice() noexcept;
    // Oldest-note policy: prefers releasing voices, then oldest held voice.
    Voice* stealVoice() noexcept;
    Voice* findVoiceForNote(int midiNote) noexcept;
    [[nodiscard]] float portamentoAlpha() const noexcept;
    [[nodiscard]] static float midiNoteToHz(int note) noexcept;

    std::vector<Voice> voices_;
    double sampleRate_{44100.0};
    float portamentoSeconds_{0.0f};
    bool retrigger_{true};
    uint64_t noteCounter_{0};
};

} // namespace agentic_synth::engine
