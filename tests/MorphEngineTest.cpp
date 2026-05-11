#include "engine/MorphEngine.h"
#include "engine/PatchStruct.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace agentic_synth::engine;
using namespace agentic_synth;

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
    // targetCount() returns the number of *filled* slots, not (max-index + 1).
    // Only slot 2 is populated, so count is 1.
    REQUIRE(morph.targetCount() == 1);
    REQUIRE(morph.target(2).has_value());
    REQUIRE_FALSE(morph.target(0).has_value());
    REQUIRE_FALSE(morph.target(1).has_value());
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
    REQUIRE_THAT(result.filter.cutoff_hz, Catch::Matchers::WithinAbs(200.0f, 0.01f));
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
    REQUIRE_THAT(result.filter.cutoff_hz, Catch::Matchers::WithinAbs(2000.0f, 0.01f));
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
    // cutoff_hz uses log-domain interp → geometric mean = sqrt(1000 * 3000) ≈ 1732.05.
    REQUIRE_THAT(result.filter.cutoff_hz, Catch::Matchers::WithinAbs(1732.05f, 1.0f));
    // master_gain stays linear.
    REQUIRE_THAT(result.master_gain, Catch::Matchers::WithinAbs(0.5f, 0.01f));
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
    REQUIRE_THAT(morph.position(), Catch::Matchers::WithinAbs(0.0f, 0.01f));

    consumed = morph.onMidiCC(2, 64);
    REQUIRE(consumed);
    REQUIRE_THAT(morph.position(), Catch::Matchers::WithinAbs(0.5f, 0.01f));

    consumed = morph.onMidiCC(2, 127);
    REQUIRE(consumed);
    REQUIRE_THAT(morph.position(), Catch::Matchers::WithinAbs(1.0f, 0.01f));
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
    // Log-domain midpoint: sqrt(100 * 500) ≈ 223.6 Hz (NOT linear 300).
    REQUIRE_THAT(captured->filter.cutoff_hz, Catch::Matchers::WithinAbs(223.6f, 1.0f));
}

// ── Log-domain interpolation for perceptual fields ────────────────────────────

TEST_CASE("MorphEngine: cutoff sweeps in log domain", "[morph][log]") {
    MorphEngine morph;
    PatchStruct a = make_default_patch();
    a.filter.cutoff_hz = 200.0f;
    PatchStruct b = make_default_patch();
    b.filter.cutoff_hz = 20000.0f;
    morph.saveTarget(a);
    morph.saveTarget(b);

    // Geometric mean = sqrt(200 * 20000) = 2000 Hz, NOT linear mean 10100 Hz.
    auto result = morph.morphedPatchAt(0.5f);
    REQUIRE_THAT(result.filter.cutoff_hz, Catch::Matchers::WithinRel(2000.0f, 0.05f));
}

TEST_CASE("MorphEngine: LFO rate sweeps in log domain", "[morph][log]") {
    MorphEngine morph;
    PatchStruct a = make_default_patch();
    a.lfo[0].rate_hz = 0.1f;
    PatchStruct b = make_default_patch();
    b.lfo[0].rate_hz = 100.0f;
    morph.saveTarget(a);
    morph.saveTarget(b);

    // Geometric mean = sqrt(0.1 * 100) ≈ 3.162, NOT linear 50.05.
    auto result = morph.morphedPatchAt(0.5f);
    REQUIRE_THAT(result.lfo[0].rate_hz, Catch::Matchers::WithinRel(3.1623f, 0.05f));
}

TEST_CASE("MorphEngine: amp env attack sweeps in log domain", "[morph][log]") {
    MorphEngine morph;
    PatchStruct a = make_default_patch();
    a.amp_env.attack_s = 0.001f;
    PatchStruct b = make_default_patch();
    b.amp_env.attack_s = 1.0f;
    morph.saveTarget(a);
    morph.saveTarget(b);

    // Geometric mean = sqrt(0.001 * 1.0) ≈ 0.03162, NOT linear 0.5005.
    auto result = morph.morphedPatchAt(0.5f);
    REQUIRE_THAT(result.amp_env.attack_s, Catch::Matchers::WithinRel(0.03162f, 0.05f));
}

TEST_CASE("MorphEngine: linear fields stay linear", "[morph][log]") {
    MorphEngine morph;
    PatchStruct a = make_default_patch();
    a.master_gain = 0.0f;
    a.filter.resonance = 0.0f;
    a.reverb.mix = 0.0f;
    PatchStruct b = make_default_patch();
    b.master_gain = 1.0f;
    b.filter.resonance = 1.0f;
    b.reverb.mix = 1.0f;
    morph.saveTarget(a);
    morph.saveTarget(b);

    auto result = morph.morphedPatchAt(0.5f);
    REQUIRE_THAT(result.master_gain, Catch::Matchers::WithinAbs(0.5f, 1e-5f));
    REQUIRE_THAT(result.filter.resonance, Catch::Matchers::WithinAbs(0.5f, 1e-5f));
    REQUIRE_THAT(result.reverb.mix, Catch::Matchers::WithinAbs(0.5f, 1e-5f));
}

TEST_CASE("MorphEngine: identical patches morph to identity", "[morph][log]") {
    MorphEngine morph;
    PatchStruct p = make_default_patch();
    p.filter.cutoff_hz = 1234.0f;
    p.lfo[0].rate_hz = 7.5f;
    p.amp_env.attack_s = 0.05f;
    p.amp_env.decay_s = 0.2f;
    p.amp_env.release_s = 0.4f;
    p.filter_env.attack_s = 0.01f;
    p.filter_env.decay_s = 0.3f;
    p.filter_env.release_s = 0.5f;
    p.master_gain = 0.7f;
    p.portamento_s = 0.123f;
    p.delay.time_s = 0.456f;
    morph.saveTarget(p);
    morph.saveTarget(p);

    for (float t : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f}) {
        auto out = morph.morphedPatchAt(t);
        INFO("t = " << t);
        REQUIRE_THAT(out.filter.cutoff_hz, Catch::Matchers::WithinRel(p.filter.cutoff_hz, 1e-4f));
        REQUIRE_THAT(out.lfo[0].rate_hz, Catch::Matchers::WithinRel(p.lfo[0].rate_hz, 1e-4f));
        REQUIRE_THAT(out.amp_env.attack_s, Catch::Matchers::WithinRel(p.amp_env.attack_s, 1e-4f));
        REQUIRE_THAT(out.amp_env.decay_s, Catch::Matchers::WithinRel(p.amp_env.decay_s, 1e-4f));
        REQUIRE_THAT(out.amp_env.release_s, Catch::Matchers::WithinRel(p.amp_env.release_s, 1e-4f));
        REQUIRE_THAT(out.filter_env.attack_s, Catch::Matchers::WithinRel(p.filter_env.attack_s, 1e-4f));
        REQUIRE_THAT(out.master_gain, Catch::Matchers::WithinAbs(p.master_gain, 1e-5f));
        REQUIRE_THAT(out.portamento_s, Catch::Matchers::WithinRel(p.portamento_s, 1e-4f));
        REQUIRE_THAT(out.delay.time_s, Catch::Matchers::WithinRel(p.delay.time_s, 1e-4f));
    }
}

TEST_CASE("MorphEngine: portamento sweeps in log domain", "[morph][log]") {
    MorphEngine morph;
    PatchStruct a = make_default_patch();
    a.portamento_s = 0.01f;
    PatchStruct b = make_default_patch();
    b.portamento_s = 1.0f;
    morph.saveTarget(a);
    morph.saveTarget(b);

    // Endpoints exact.
    REQUIRE_THAT(morph.morphedPatchAt(0.0f).portamento_s, Catch::Matchers::WithinRel(0.01f, 1e-4f));
    REQUIRE_THAT(morph.morphedPatchAt(1.0f).portamento_s, Catch::Matchers::WithinRel(1.0f, 1e-4f));

    // Midpoint = geometric mean = sqrt(0.01 * 1.0) = 0.1, NOT linear 0.505.
    auto mid = morph.morphedPatchAt(0.5f);
    REQUIRE_THAT(mid.portamento_s, Catch::Matchers::WithinRel(0.1f, 0.05f));
}

TEST_CASE("MorphEngine: delay.time_s sweeps in log domain", "[morph][log]") {
    MorphEngine morph;
    PatchStruct a = make_default_patch();
    a.delay.time_s = 0.01f;
    PatchStruct b = make_default_patch();
    b.delay.time_s = 1.0f;
    morph.saveTarget(a);
    morph.saveTarget(b);

    // Endpoints exact.
    REQUIRE_THAT(morph.morphedPatchAt(0.0f).delay.time_s, Catch::Matchers::WithinRel(0.01f, 1e-4f));
    REQUIRE_THAT(morph.morphedPatchAt(1.0f).delay.time_s, Catch::Matchers::WithinRel(1.0f, 1e-4f));

    // Midpoint = geometric mean = sqrt(0.01 * 1.0) = 0.1, NOT linear 0.505.
    auto mid = morph.morphedPatchAt(0.5f);
    REQUIRE_THAT(mid.delay.time_s, Catch::Matchers::WithinRel(0.1f, 0.05f));
}
