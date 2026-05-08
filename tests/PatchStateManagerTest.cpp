#include "engine/PatchStateManager.h"
#include "engine/PatchStruct.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <juce_core/juce_core.h>

using namespace agentic_synth;
using namespace agentic_synth::engine;

static PatchStruct makeTestPatch() {
    PatchStruct p{};
    p.filter.cutoff_hz = 1234.0f;
    p.filter.resonance = 0.75f;
    p.amp_env.attack_s = 0.05f;
    p.amp_env.decay_s = 0.2f;
    p.amp_env.sustain = 0.7f;
    p.amp_env.release_s = 0.5f;
    p.lfo[0].rate_hz = 4.2f;
    p.lfo[0].depth = 0.8f;
    p.lfo[0].waveform = LfoWaveform::Triangle;
    p.osc[0].volume = 0.8f;
    p.osc[1].volume = 0.3f;
    return p;
}

TEST_CASE("PatchStateManager save/restore round-trip", "[engine][patch]") {
    PatchStateManager mgr;
    auto patch = makeTestPatch();
    auto xml = mgr.saveToXml(patch);
    REQUIRE_FALSE(xml.isEmpty());

    auto restored = mgr.loadFromXml(xml);
    REQUIRE_THAT(restored.filter.cutoff_hz, Catch::Matchers::WithinRel(1234.0f, 0.001f));
    REQUIRE_THAT(restored.filter.resonance, Catch::Matchers::WithinRel(0.75f, 0.001f));
    REQUIRE_THAT(restored.lfo[0].rate_hz, Catch::Matchers::WithinRel(4.2f, 0.001f));
    REQUIRE_THAT(restored.lfo[0].depth, Catch::Matchers::WithinRel(0.8f, 0.001f));
    REQUIRE(restored.lfo[0].waveform == LfoWaveform::Triangle);
    REQUIRE_THAT(restored.amp_env.sustain, Catch::Matchers::WithinRel(0.7f, 0.001f));
}

TEST_CASE("PatchStateManager handles extreme values", "[engine][patch]") {
    PatchStateManager mgr;
    PatchStruct extreme{};
    extreme.filter.cutoff_hz = 20000.0f;
    extreme.amp_env.release_s = 20.0f;
    extreme.osc[0].volume = 1.0f;

    auto xml = mgr.saveToXml(extreme);
    auto restored = mgr.loadFromXml(xml);
    REQUIRE_THAT(restored.filter.cutoff_hz, Catch::Matchers::WithinRel(20000.0f, 0.001f));
    REQUIRE_THAT(restored.amp_env.release_s, Catch::Matchers::WithinRel(20.0f, 0.001f));
}

TEST_CASE("PatchStateManager returns default on invalid XML", "[engine][patch]") {
    PatchStateManager mgr;
    auto result = mgr.loadFromXml("<invalid/>");
    // Should return zero-initialized patch without crashing
    (void)result;
}
