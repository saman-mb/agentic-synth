#pragma once

#include "engine/ADSREnvelope.h"
#include "engine/VoiceManager.h"

#include <cmath>
#include <cstdint>

namespace agentic_synth::engine {

// Minimal MIDI message – no JUCE dependency so unit tests compile without JUCE.
struct RawMidiMsg {
    uint8_t status{0};
    uint8_t data1{0};
    uint8_t data2{0};

    static RawMidiMsg noteOn(int note, int vel, int ch = 0) noexcept {
        return {static_cast<uint8_t>(0x90u | (static_cast<unsigned>(ch) & 0x0Fu)), static_cast<uint8_t>(note & 0x7F),
                static_cast<uint8_t>(vel & 0x7F)};
    }
    static RawMidiMsg noteOff(int note, int ch = 0) noexcept {
        return {static_cast<uint8_t>(0x80u | (static_cast<unsigned>(ch) & 0x0Fu)), static_cast<uint8_t>(note & 0x7F),
                0};
    }
    static RawMidiMsg cc(int controller, int value, int ch = 0) noexcept {
        return {static_cast<uint8_t>(0xB0u | (static_cast<unsigned>(ch) & 0x0Fu)),
                static_cast<uint8_t>(controller & 0x7F), static_cast<uint8_t>(value & 0x7F)};
    }
};

// Routes MIDI events to VoiceManager and maps standard CCs to synth parameters.
// Also propagates host BPM to tempo-synced LFOs.
// JUCE-free: call process() with a RawMidiMsg; the plugin adapts juce::MidiMessage.
class MidiHandler {
public:
    explicit MidiHandler(VoiceManager& vm) noexcept;

    // Process one MIDI message (audio-thread safe).
    void process(const RawMidiMsg& msg) noexcept;

    // Forward DAW tempo to VoiceManager::setHostTempo().
    void setHostTempo(double bpm) noexcept;

    // Expose current CC values for upstream inspection / AI context.
    [[nodiscard]] float modWheelValue() const noexcept { return modWheel_; }
    [[nodiscard]] float volumeValue() const noexcept { return ccVolume_; }
    [[nodiscard]] float currentCutoffHz() const noexcept { return cutoffHz_; }
    [[nodiscard]] float currentResonance() const noexcept { return resonance_; }

private:
    void handleNoteOn(int note, float velocity) noexcept;
    void handleNoteOff(int note) noexcept;
    void handleCC(int controller, int value) noexcept;

    // CC → log-scaled filter cutoff: 0→20 Hz, 127→18 kHz.
    static float ccToCutoff(int value) noexcept {
        const float norm = static_cast<float>(value) / 127.0f;
        return 20.0f * std::pow(900.0f, norm); // 20 * 900^0 = 20, 20 * 900^1 ≈ 18000
    }

    VoiceManager& vm_;
    float modWheel_{0.0f};
    float ccVolume_{1.0f};
    float cutoffHz_{5000.0f};
    float resonance_{0.0f};
    ADSREnvelope::Params ampEnv_{}; // cached so per-CC updates touch one field at a time
    bool sustainPedal_{false};
};

} // namespace agentic_synth::engine
