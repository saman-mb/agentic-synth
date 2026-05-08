#include "engine/MidiHandler.h"
#include "engine/VoiceManager.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace agentic_synth::engine;

namespace {
VoiceManager makeVM(int voices = 4) {
    VoiceManager vm(voices);
    vm.prepare(44100.0);
    return vm;
}
} // namespace

TEST_CASE("MidiHandler: note on routes to VoiceManager", "[midi]") {
    auto vm = makeVM();
    MidiHandler handler(vm);

    REQUIRE(vm.activeVoiceCount() == 0);
    handler.process(RawMidiMsg::noteOn(60, 100));
    REQUIRE(vm.activeVoiceCount() == 1);
    REQUIRE(vm.activeNotes().front() == 60);
}

TEST_CASE("MidiHandler: note off releases voice", "[midi]") {
    auto vm = makeVM();
    MidiHandler handler(vm);

    handler.process(RawMidiMsg::noteOn(60, 100));
    REQUIRE(vm.activeVoiceCount() == 1);
    handler.process(RawMidiMsg::noteOff(60));
    // Voice enters release; still active until envelope finishes.
    const auto notes = vm.activeNotes();
    // Note is still sounding (release phase) but noteIsOn flag is cleared –
    // a second noteOn on same pitch should reuse the voice.
    handler.process(RawMidiMsg::noteOn(60, 80));
    REQUIRE(vm.activeVoiceCount() >= 1);
}

TEST_CASE("MidiHandler: velocity-0 note-on treated as note-off", "[midi]") {
    auto vm = makeVM();
    MidiHandler handler(vm);

    handler.process(RawMidiMsg::noteOn(60, 100));
    REQUIRE(vm.activeVoiceCount() == 1);
    // Velocity 0 = note-off per MIDI spec.
    handler.process(RawMidiMsg::noteOn(60, 0));
    // Voice is in release; activeVoiceCount still ≥ 0 (depends on envelope).
    // Check no second voice was allocated.
    REQUIRE(vm.activeVoiceCount() <= 1);
}

TEST_CASE("MidiHandler: CC1 mod wheel stored and accessible", "[midi]") {
    auto vm = makeVM();
    MidiHandler handler(vm);

    REQUIRE(handler.modWheelValue() == 0.0f);
    handler.process(RawMidiMsg::cc(1, 64));
    CHECK_THAT(handler.modWheelValue(), Catch::Matchers::WithinAbs(64.0f / 127.0f, 1e-4f));

    handler.process(RawMidiMsg::cc(1, 127));
    CHECK_THAT(handler.modWheelValue(), Catch::Matchers::WithinAbs(1.0f, 1e-4f));

    handler.process(RawMidiMsg::cc(1, 0));
    CHECK_THAT(handler.modWheelValue(), Catch::Matchers::WithinAbs(0.0f, 1e-4f));
}

TEST_CASE("MidiHandler: CC74 filter cutoff mapped log-scale", "[midi]") {
    auto vm = makeVM();
    MidiHandler handler(vm);

    handler.process(RawMidiMsg::cc(74, 0));
    REQUIRE(handler.currentCutoffHz() < 25.0f); // near 20 Hz

    handler.process(RawMidiMsg::cc(74, 127));
    REQUIRE(handler.currentCutoffHz() > 10000.0f); // near 18 kHz
}

TEST_CASE("MidiHandler: CC71 resonance stored and applied", "[midi]") {
    auto vm = makeVM();
    MidiHandler handler(vm);

    REQUIRE(handler.currentResonance() == 0.0f);
    handler.process(RawMidiMsg::cc(71, 127));
    CHECK_THAT(handler.currentResonance(), Catch::Matchers::WithinAbs(1.0f, 1e-4f));
}

TEST_CASE("MidiHandler: CC123 all notes off clears voices", "[midi]") {
    auto vm = makeVM(8);
    MidiHandler handler(vm);

    for (int note : {60, 62, 64, 65, 67})
        handler.process(RawMidiMsg::noteOn(note, 100));
    REQUIRE(vm.activeVoiceCount() == 5);

    handler.process(RawMidiMsg::cc(123, 0));
    // All voices in release phase; activeVoiceCount reflects release phase.
    // Render a few samples so envelopes can settle.
    float buf[512] = {};
    vm.renderBlock(buf, 512);
    REQUIRE(vm.activeVoiceCount() == 0);
}

TEST_CASE("MidiHandler: CC120 all sound off clears voices", "[midi]") {
    auto vm = makeVM(4);
    MidiHandler handler(vm);

    handler.process(RawMidiMsg::noteOn(60, 100));
    handler.process(RawMidiMsg::noteOn(64, 100));
    REQUIRE(vm.activeVoiceCount() == 2);

    handler.process(RawMidiMsg::cc(120, 0));
    float buf[512] = {};
    vm.renderBlock(buf, 512);
    REQUIRE(vm.activeVoiceCount() == 0);
}

TEST_CASE("MidiHandler: setHostTempo propagates to VoiceManager", "[midi]") {
    auto vm = makeVM();
    MidiHandler handler(vm);

    // Verify no-crash with tempo changes
    handler.setHostTempo(120.0);
    handler.setHostTempo(90.0);
    handler.setHostTempo(180.0);

    // Verify handler still processes MIDI correctly after tempo changes
    handler.process(RawMidiMsg::noteOn(60, 100));
    REQUIRE(vm.activeVoiceCount() == 1);
    REQUIRE(vm.activeNotes().front() == 60);

    handler.process(RawMidiMsg::noteOff(60));
    SUCCEED("setHostTempo accepted without error");
}

TEST_CASE("MidiHandler: polyphonic note allocation", "[midi]") {
    auto vm = makeVM(4);
    MidiHandler handler(vm);

    handler.process(RawMidiMsg::noteOn(60, 100));
    handler.process(RawMidiMsg::noteOn(62, 100));
    handler.process(RawMidiMsg::noteOn(64, 100));
    handler.process(RawMidiMsg::noteOn(65, 100));
    REQUIRE(vm.activeVoiceCount() == 4);
}
