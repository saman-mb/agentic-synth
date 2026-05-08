#include "engine/PresetDatabase.h"
#include "engine/PatchStruct.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace agentic_synth::engine;

TEST_CASE("PresetDatabase: open/close cycle succeeds", "[presets]") {
    PresetDatabase db;
    REQUIRE(db.open(":memory:"));
    db.close();
    // No crash = success
}

TEST_CASE("PresetDatabase: save and load a preset round-trips correctly", "[presets]") {
    PresetDatabase db;
    REQUIRE(db.open(":memory:"));

    PatchStruct original = make_default_patch();
    original.filter.cutoff_hz = 500.0f;
    original.amp_env.attack_s = 0.1f;
    original.master_gain = 0.8f;

    REQUIRE(db.savePreset("test_preset", original));

    PatchStruct loaded{};
    REQUIRE(db.loadPreset("test_preset", loaded));

    // Verify round-trip fidelity (bit-exact)
    REQUIRE(loaded.version == original.version);
    REQUIRE(loaded.filter.cutoff_hz == original.filter.cutoff_hz);
    REQUIRE(loaded.amp_env.attack_s == original.amp_env.attack_s);
    REQUIRE(loaded.master_gain == original.master_gain);

    db.close();
}

TEST_CASE("PresetDatabase: listing presets returns saved names", "[presets]") {
    PresetDatabase db;
    REQUIRE(db.open(":memory:"));

    PatchStruct p = make_default_patch();
    REQUIRE(db.savePreset("preset_a", p));
    REQUIRE(db.savePreset("preset_b", p));
    REQUIRE(db.savePreset("preset_c", p));

    auto names = db.listPresets();
    REQUIRE(names.size() >= 3);
    // Order is not guaranteed, but all names should be present
    bool foundA = false, foundB = false, foundC = false;
    for (const auto& n : names) {
        if (n == "preset_a")
            foundA = true;
        if (n == "preset_b")
            foundB = true;
        if (n == "preset_c")
            foundC = true;
    }
    REQUIRE(foundA);
    REQUIRE(foundB);
    REQUIRE(foundC);

    db.close();
}

TEST_CASE("PresetDatabase: delete preset removes it from listing", "[presets]") {
    PresetDatabase db;
    REQUIRE(db.open(":memory:"));

    PatchStruct p = make_default_patch();
    REQUIRE(db.savePreset("to_delete", p));
    REQUIRE(db.listPresets().size() >= 1);

    // Find the ID of "to_delete" — since listPresets returns names, load by name then load by ID
    REQUIRE(db.deletePreset(1)); // First preset gets ID 1 in SQLite
    auto names = db.listPresets();
    auto it = std::find(names.begin(), names.end(), "to_delete");
    REQUIRE(it == names.end()); // Should no longer exist

    db.close();
}

TEST_CASE("PresetDatabase: logging and retrieving events", "[presets]") {
    PresetDatabase db;
    REQUIRE(db.open(":memory:"));

    REQUIRE(db.logEvent("session_start", "{\"version\": 1}"));
    REQUIRE(db.logEvent("generation", "{\"descriptor\": \"warm pad\"}"));
    REQUIRE(db.logEvent("feedback", "{\"liked\": true}"));

    auto events = db.recentEvents(10);
    REQUIRE(events.size() >= 3);

    db.close();
}

TEST_CASE("PresetDatabase: export/import round-trip", "[presets]") {
    PresetDatabase db;
    REQUIRE(db.open(":memory:"));

    PatchStruct p = make_default_patch();
    p.filter.cutoff_hz = 8000.0f;
    REQUIRE(db.savePreset("export_test", p));

    // Export to a temp path
    const std::string tmpPath = "/tmp/preset_export_test.json";
    REQUIRE(db.exportToJson(tmpPath));

    // Import into a fresh database
    PresetDatabase db2;
    REQUIRE(db2.open(":memory:"));
    REQUIRE(db2.importFromJson(tmpPath));

    // Verify the preset survived
    auto names = db2.listPresets();
    bool found = false;
    for (const auto& n : names) {
        if (n == "export_test")
            found = true;
    }
    REQUIRE(found);

    PatchStruct loaded{};
    REQUIRE(db2.loadPreset("export_test", loaded));
    REQUIRE(loaded.filter.cutoff_hz == 8000.0f);

    db.close();
    db2.close();
}
