#include "engine/MidiHandler.h"

namespace agentic_synth::engine {

MidiHandler::MidiHandler(VoiceManager& vm) noexcept : vm_(vm) {
    // Initialise cached amp envelope to ADSR defaults.
    ampEnv_.attackSeconds = 0.005f;
    ampEnv_.decaySeconds = 0.1f;
    ampEnv_.sustainLevel = 1.0f;
    ampEnv_.releaseSeconds = 0.1f;
}

void MidiHandler::process(const RawMidiMsg& msg) noexcept {
    const uint8_t type = msg.status & 0xF0u;
    switch (type) {
    case 0x80u:
        handleNoteOff(msg.data1);
        break;
    case 0x90u:
        if (msg.data2 == 0)
            handleNoteOff(msg.data1); // velocity-0 note-on = note-off
        else
            handleNoteOn(msg.data1, static_cast<float>(msg.data2) / 127.0f);
        break;
    case 0xB0u:
        handleCC(msg.data1, msg.data2);
        break;
    default:
        break;
    }
}

void MidiHandler::setHostTempo(double bpm) noexcept { vm_.setHostTempo(bpm); }

// ── private ───────────────────────────────────────────────────────────────────

void MidiHandler::handleNoteOn(int note, float velocity) noexcept { vm_.noteOn(note, velocity); }

void MidiHandler::handleNoteOff(int note) noexcept { vm_.noteOff(note); }

void MidiHandler::handleCC(int controller, int value) noexcept {
    switch (controller) {
    case 1: // Mod wheel: modulate filter cutoff additively.
        modWheel_ = static_cast<float>(value) / 127.0f;
        vm_.setFilterCutoff(cutoffHz_ * (1.0f + modWheel_ * 2.0f));
        break;

    case 7: // Volume
        ccVolume_ = static_cast<float>(value) / 127.0f;
        break;

    case 64: // Sustain pedal – release all held notes on pedal-off.
        if (sustainPedal_ && value < 64)
            vm_.allNotesOff();
        sustainPedal_ = (value >= 64);
        break;

    case 71: // Resonance / Timbre
        resonance_ = static_cast<float>(value) / 127.0f;
        vm_.setFilterResonance(resonance_);
        break;

    case 72: { // Amp release time (0 = 20 ms, 127 = 10 s, log scale)
        const float norm = static_cast<float>(value) / 127.0f;
        ampEnv_.releaseSeconds = 0.02f * std::pow(500.0f, norm);
        vm_.setAmpEnvelope(ampEnv_);
        break;
    }
    case 73: { // Amp attack time (0 = 1 ms, 127 = 10 s, log scale)
        const float norm = static_cast<float>(value) / 127.0f;
        ampEnv_.attackSeconds = 0.001f * std::pow(10000.0f, norm);
        vm_.setAmpEnvelope(ampEnv_);
        break;
    }
    case 74: // Filter cutoff / Brightness
        cutoffHz_ = ccToCutoff(value);
        vm_.setFilterCutoff(cutoffHz_ * (1.0f + modWheel_ * 2.0f));
        break;

    case 75: { // Amp decay time
        const float norm = static_cast<float>(value) / 127.0f;
        ampEnv_.decaySeconds = 0.001f * std::pow(10000.0f, norm);
        vm_.setAmpEnvelope(ampEnv_);
        break;
    }
    case 120: // All Sound Off
    case 123: // All Notes Off
        vm_.allNotesOff();
        break;

    default:
        break;
    }
}

} // namespace agentic_synth::engine
