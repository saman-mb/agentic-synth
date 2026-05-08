#include <catch2/catch_test_macros.hpp>
#include "engine/PresetDatabase.h"

using namespace agentic_synth::engine;

TEST_CASE("PresetDatabase: save and load round-trip", "[persistence]") {
    PresetDatabase db;

    PatchStruct original{};
    original.filterCutoffHz = 500.0f;
    original.filterResonance = 0.3f;
    original.oscillatorMix[0] = 1.0f;
    original.ampAttackMs = 100.0f;

    REQUIRE(db.savePreset("test_patch", original));

    PatchStruct loaded{};
    REQUIRE(db.loadPreset("test_patch", loaded));
    REQUIRE(loaded.filterCutoffHz == 500.0f);
    REQUIRE(loaded.filterResonance == 0.3f);
}

TEST_CASE("PresetDatabase: list presets", "[persistence]") {
    PresetDatabase db;

    PatchStruct p{};
    db.savePreset("a", p);
    db.savePreset("b", p);

    auto presets = db.listPresets();
    // At minimum our saved presets are present
    REQUIRE(presets.size() >= 2);
}

TEST_CASE("PresetDatabase: log and retrieve events", "[persistence]") {
    PresetDatabase db;

    db.logEvent("generation", R"({"latency_ms": 42.5})");
    db.logEvent("ui_interaction", R"({"param": "cutoff", "value": 500})");

    auto events = db.recentEvents(10);
    REQUIRE(events.size() >= 2);
}

TEST_CASE("PresetDatabase: export and import JSON", "[persistence]") {
    PresetDatabase db;

    PatchStruct p{};
    p.filterCutoffHz = 800.0f;
    db.savePreset("export_test", p);

    REQUIRE(db.exportToJson("/tmp/test_presets.json"));
    REQUIRE(db.importFromJson("/tmp/test_presets.json"));
}
