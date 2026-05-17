// Phase G / #262 — MidiLearnStore unit tests. Verifies learn-mode capture,
// lookup, persistence round-trip, and bank-select filtering.

#include "agent/MidiLearnStore.h"

#include <catch2/catch_test_macros.hpp>

#include <juce_core/juce_core.h>

using agentic_synth::agent::MidiLearnStore;

namespace {

juce::File makeTempMapFile() {
    auto f = juce::File::createTempFile("midi_map.json");
    if (f.existsAsFile())
        f.deleteFile();
    return f;
}

} // namespace

TEST_CASE("MidiLearnStore: captures CC when learning, persists across reload", "[MidiLearn]") {
    auto path = makeTempMapFile();
    {
        auto store = MidiLearnStore::withFileForTesting(path);
        CHECK_FALSE(store.isLearning());
        store.enterLearnMode("filter.cutoff");
        CHECK(store.isLearning());
        CHECK(store.learningKnobId() == "filter.cutoff");

        auto captured = store.captureIfLearning(74, 0);
        REQUIRE(captured.has_value());
        CHECK(*captured == "filter.cutoff");
        CHECK_FALSE(store.isLearning());

        auto lookup = store.findKnobFor(74, 0);
        REQUIRE(lookup.has_value());
        CHECK(*lookup == "filter.cutoff");
    }
    // Reload from disk via a fresh instance — mapping survives.
    {
        auto store = MidiLearnStore::withFileForTesting(path);
        auto lookup = store.findKnobFor(74, 0);
        REQUIRE(lookup.has_value());
        CHECK(*lookup == "filter.cutoff");
    }
    path.deleteFile();
}

TEST_CASE("MidiLearnStore: capture is a no-op when not learning", "[MidiLearn]") {
    auto path = makeTempMapFile();
    auto store = MidiLearnStore::withFileForTesting(path);
    auto captured = store.captureIfLearning(74, 0);
    CHECK_FALSE(captured.has_value());
    CHECK_FALSE(store.findKnobFor(74, 0).has_value());
    path.deleteFile();
}

TEST_CASE("MidiLearnStore: bank-select CCs do not capture", "[MidiLearn]") {
    auto path = makeTempMapFile();
    auto store = MidiLearnStore::withFileForTesting(path);
    store.enterLearnMode("amp.gain");
    // CC 0 (bank MSB), CC 32 (bank LSB), CC 120-127 (mode messages) — all
    // filtered so the user doesn't accidentally bind a hidden DAW message.
    CHECK_FALSE(store.captureIfLearning(0, 0).has_value());
    CHECK_FALSE(store.captureIfLearning(32, 0).has_value());
    CHECK_FALSE(store.captureIfLearning(120, 0).has_value());
    // Still learning because nothing was captured.
    CHECK(store.isLearning());
    // A real knob CC captures cleanly.
    auto captured = store.captureIfLearning(71, 0);
    REQUIRE(captured.has_value());
    CHECK(*captured == "amp.gain");
    path.deleteFile();
}

TEST_CASE("MidiLearnStore: re-learn on same knob replaces prior CC", "[MidiLearn]") {
    auto path = makeTempMapFile();
    auto store = MidiLearnStore::withFileForTesting(path);
    store.enterLearnMode("env.attack");
    store.captureIfLearning(74, 0);

    store.enterLearnMode("env.attack");
    store.captureIfLearning(73, 0);

    // Only one mapping for env.attack — pointed at CC 73 (the latest).
    CHECK_FALSE(store.findKnobFor(74, 0).has_value());
    auto lookup = store.findKnobFor(73, 0);
    REQUIRE(lookup.has_value());
    CHECK(*lookup == "env.attack");
    CHECK(store.all().size() == 1);
    path.deleteFile();
}

TEST_CASE("MidiLearnStore: clearMapping removes the entry", "[MidiLearn]") {
    auto path = makeTempMapFile();
    auto store = MidiLearnStore::withFileForTesting(path);
    store.enterLearnMode("osc.detune");
    store.captureIfLearning(75, 0);
    REQUIRE(store.findKnobFor(75, 0).has_value());

    store.clearMapping("osc.detune");
    CHECK_FALSE(store.findKnobFor(75, 0).has_value());
    CHECK(store.all().empty());
    path.deleteFile();
}

TEST_CASE("MidiLearnStore: cancelLearnMode aborts pending capture", "[MidiLearn]") {
    auto path = makeTempMapFile();
    auto store = MidiLearnStore::withFileForTesting(path);
    store.enterLearnMode("reverb.size");
    CHECK(store.isLearning());
    store.cancelLearnMode();
    CHECK_FALSE(store.isLearning());
    // The next CC must NOT be captured.
    auto captured = store.captureIfLearning(91, 0);
    CHECK_FALSE(captured.has_value());
    path.deleteFile();
}
