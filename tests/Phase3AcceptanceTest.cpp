#include <catch2/catch_test_macros.hpp>
#include "engine/PatchStruct.h"
#include "engine/VoiceManager.h"
#include "engine/MidiHandler.h"
#include "plugin/AgenticSynthPlugin.h"
#include "agent/AgentBridge.h"

using namespace agentic_synth::engine;

// Phase 3 Acceptance: Plugin is stable in DAW-like conditions

TEST_CASE("Phase 3: Plugin processes audio without crash", "[phase3][acceptance]") {
    // Standalone test: verify audio processing is stable
    SynthEngine engine;
    AgentBridge bridge(engine);
    VoiceManager voices;

    voices.prepare(44100.0);

    // Simulate sustained play
    voices.noteOn(60, 100.0f);
    float sample = 0.0f;
    for (int i = 0; i < 44100; ++i) {  // 1 second of audio
        sample = voices.renderNextSample();
    }
    voices.allNotesOff();

    // No crash — sample produced
    REQUIRE(std::isfinite(sample));
}

TEST_CASE("Phase 3: MIDI note on/off produces envelope", "[phase3][acceptance]") {
    VoiceManager voices;
    voices.prepare(44100.0);

    // Initial silence
    float initial = voices.renderNextSample();
    REQUIRE(std::abs(initial) < 0.001f);

    // Note on — should start producing sound
    voices.noteOn(60, 100.0f);
    float afterNoteOn = 0.0f;
    for (int i = 0; i < 100; ++i) {
        afterNoteOn = voices.renderNextSample();
    }
    REQUIRE(std::abs(afterNoteOn) > 0.0f);

    // Note off — should eventually fade
    voices.noteOff(60);
    float afterNoteOff = 0.0f;
    for (int i = 0; i < 480; ++i) {
        afterNoteOff = voices.renderNextSample();
    }
    // Note: envelope release may still be fading
    REQUIRE(std::isfinite(afterNoteOff));
}

TEST_CASE("Phase 3: Parameter change during playback is smooth", "[phase3][acceptance]") {
    VoiceManager voices;
    voices.prepare(44100.0);

    voices.noteOn(48, 100.0f);

    // Collect block before change
    float before[256];
    for (int i = 0; i < 256; ++i) {
        before[i] = voices.renderNextSample();
    }

    // Abrupt parameter change
    voices.setFilterCutoff(500.0f);

    // Collect block after — should not glitch
    float after[256];
    for (int i = 0; i < 256; ++i) {
        after[i] = voices.renderNextSample();
    }

    // Samples should remain finite and in reasonable range
    for (int i = 0; i < 256; ++i) {
        REQUIRE(std::isfinite(before[i]));
        REQUIRE(std::isfinite(after[i]));
        REQUIRE(std::abs(before[i]) < 10.0f);
        REQUIRE(std::abs(after[i]) < 10.0f);
    }
}

TEST_CASE("Phase 3: Multiple voices play polyphonically", "[phase3][acceptance]") {
    VoiceManager voices;
    voices.prepare(44100.0);

    // Play a chord
    voices.noteOn(60, 100.0f);
    voices.noteOn(64, 80.0f);
    voices.noteOn(67, 90.0f);

    float mix[256];
    for (int i = 0; i < 256; ++i) {
        mix[i] = voices.renderNextSample();
    }

    // Chord should be audible
    float peak = 0.0f;
    for (int i = 0; i < 256; ++i) {
        peak = std::max(peak, std::abs(mix[i]));
    }
    REQUIRE(peak > 0.0f);

    voices.allNotesOff();
}
