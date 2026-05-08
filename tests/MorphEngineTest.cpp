#include "engine/MorphEngine.h"
#include "engine/PatchStruct.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace agentic_synth::engine;

TEST_CASE("MorphEngine: initial state has no targets and zero position", "[morph]") {
    MorphEngine morph;
    REQUIRE(morph.targetCount() == 0);
    REQUIRE(morph.position() == 0.0f);
}

TEST_CASE("MorphEngine: save target assigns slot and increments count", "[morph]") {
    MorphEngine morph;
    auto p = make_default_patch();

    int slot = morph.saveTarget(p);
    REQUIRE(slot == 0);
    REQUIRE(morph.targetCount() == 1);

    slot = morph.saveTarget(p);
    REQUIRE(slot == 1);
    REQUIRE(morph.targetCount() == 2);
}

TEST_CASE("MorphEngine: save to explicit slot works", "[morph]") {
    MorphEngine morph;
    auto p = make_default_patch();

    int slot = morph.saveTarget(p, 2);
    REQUIRE(slot == 2);
    REQUIRE(morph.targetCount() == 3); // slots 0, 1 are empty, but count is 3
}

TEST_CASE("MorphEngine: morphed patch at 0.0 equals first target", "[morph]") {
    MorphEngine morph;
    PatchStruct a = make_default_patch();
    a.filter.cutoff_hz = 200.0f;
    PatchStruct b = make_default_patch();
    b.filter.cutoff_hz = 2000.0f;

    morph.saveTarget(a);
    morph.saveTarget(b);

    auto result = morph.morphedPatchAt(0.0f);
    REQUIRE(result.filter.cutoff_hz == Catch::Matchers::WithinAbs(200.0f, 0.01f));
}

TEST_CASE("MorphEngine: morphed patch at 1.0 equals last target", "[morph]") {
    MorphEngine morph;
    PatchStruct a = make_default_patch();
    a.filter.cutoff_hz = 200.0f;
    PatchStruct b = make_default_patch();
    b.filter.cutoff_hz = 2000.0f;

    morph.saveTarget(a);
    morph.saveTarget(b);

    auto result = morph.morphedPatchAt(1.0f);
    REQUIRE(result.filter.cutoff_hz == Catch::Matchers::WithinAbs(2000.0f, 0.01f));
}

TEST_CASE("MorphEngine: morphed patch at 0.5 is midpoint of two targets", "[morph]") {
    MorphEngine morph;
    PatchStruct a = make_default_patch();
    a.filter.cutoff_hz = 1000.0f;
    a.master_gain = 0.0f;
    PatchStruct b = make_default_patch();
    b.filter.cutoff_hz = 3000.0f;
    b.master_gain = 1.0f;

    morph.saveTarget(a);
    morph.saveTarget(b);

    auto result = morph.morphedPatchAt(0.5f);
    REQUIRE(result.filter.cutoff_hz == Catch::Matchers::WithinAbs(2000.0f, 1.0f));
    REQUIRE(result.master_gain == Catch::Matchers::WithinAbs(0.5f, 0.01f));
}

TEST_CASE("MorphEngine: clear target removes it", "[morph]") {
    MorphEngine morph;
    auto p = make_default_patch();

    morph.saveTarget(p);
    morph.saveTarget(p);
    REQUIRE(morph.targetCount() == 2);

    morph.clearTarget(0);
    REQUIRE(morph.targetCount() == 1);
    REQUIRE_FALSE(morph.target(0).has_value());
    REQUIRE(morph.target(1).has_value());
}

TEST_CASE("MorphEngine: clearAll removes all targets", "[morph]") {
    MorphEngine morph;
    auto p = make_default_patch();

    morph.saveTarget(p);
    morph.saveTarget(p);
    morph.saveTarget(p);
    REQUIRE(morph.targetCount() == 3);

    morph.clearAll();
    REQUIRE(morph.targetCount() == 0);
}

TEST_CASE("MorphEngine: MIDI CC on matching controller updates position", "[morph]") {
    MorphEngine morph;
    morph.setMorphCc(2);

    // CC value [0, 127] maps to position [0, 1]
    bool consumed = morph.onMidiCC(2, 0);
    REQUIRE(consumed);
    REQUIRE(morph.position() == Catch::Matchers::WithinAbs(0.0f, 0.01f));

    consumed = morph.onMidiCC(2, 64);
    REQUIRE(consumed);
    REQUIRE(morph.position() == Catch::Matchers::WithinAbs(0.5f, 0.01f));

    consumed = morph.onMidiCC(2, 127);
    REQUIRE(consumed);
    REQUIRE(morph.position() == Catch::Matchers::WithinAbs(1.0f, 0.01f));
}

TEST_CASE("MorphEngine: MIDI CC on non-matching controller is ignored", "[morph]") {
    MorphEngine morph;
    morph.setMorphCc(2);

    bool consumed = morph.onMidiCC(1, 64); // Mod wheel, not morph CC
    REQUIRE_FALSE(consumed);
    REQUIRE(morph.position() == 0.0f); // Unchanged
}

TEST_CASE("MorphEngine: setPosition fires pollCallback flag", "[morph]") {
    MorphEngine morph;
    auto p = make_default_patch();
    morph.saveTarget(p);
    morph.saveTarget(p);

    // Before setting position, callback flag should be false
    REQUIRE_FALSE(morph.pollCallback());

    morph.setPosition(0.5f);

    // pollCallback should return true (position changed) and fire the callback once
    REQUIRE(morph.pollCallback());

    // Second poll should return false (already consumed)
    REQUIRE_FALSE(morph.pollCallback());
}

TEST_CASE("MorphEngine: callback fires with correct morphed patch", "[morph]") {
    MorphEngine morph;
    PatchStruct a = make_default_patch();
    a.filter.cutoff_hz = 100.0f;
    PatchStruct b = make_default_patch();
    b.filter.cutoff_hz = 500.0f;

    morph.saveTarget(a);
    morph.saveTarget(b);

    std::optional<PatchStruct> captured;
    morph.setCallback([&](const PatchStruct& p) { captured = p; });

    morph.setPosition(0.5f);
    morph.pollCallback();

    REQUIRE(captured.has_value());
    REQUIRE(captured->filter.cutoff_hz == Catch::Matchers::WithinAbs(300.0f, 1.0f));
}
