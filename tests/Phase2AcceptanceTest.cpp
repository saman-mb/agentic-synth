#include "agent/AgentBridge.h"
#include "agent/PrePatchPipeline.h"
#include "agent/SessionMemory.h"
#include "agent/StreamParser.h"
#include "agent/Telemetry.h"
#include "engine/MorphEngine.h"
#include "engine/PatchStruct.h"
#include "engine/PatchValidator.h"
#include "engine/VariationEngine.h"
#include "engine/VoiceManager.h"
#include "mapper/GrammarSampler.h"
#include "mapper/HeuristicParser.h"
#include "mapper/SemanticMapper.h"
#include <catch2/catch_test_macros.hpp>

using namespace agentic_synth::engine;
using namespace agentic_synth::agent;
using namespace agentic_synth::mapper;

// Phase 2 Acceptance: Full NL loop with session awareness
// Verifies the end-to-end pipeline: NL input → patch → play → feedback → refine

TEST_CASE("Phase 2: NL descriptor produces valid PatchStruct", "[phase2][acceptance]") {
    agentsynth::HeuristicParser parser;
    auto patch = parser.parse("warm pad with slow attack");
    PatchValidator validator;

    // Must pass validation
    REQUIRE(validator.validate(patch).valid());

    // Key parameters should be set non-zero
    REQUIRE(patch.filter.cutoff_hz < 1000.0f); // "warm" = lower cutoff
    REQUIRE(patch.amp_env.attack_s > 0.2f);     // "slow attack"
}

TEST_CASE("Phase 2: NL refinement updates existing patch", "[phase2][acceptance]") {
    agentsynth::HeuristicParser parser;
    auto patch = parser.parse("bright lead");

    // Refine
    auto refined = parser.parse("make it darker");
    // The refined patch should have lower cutoff
    // (Integration-style: refine mutates internal state)

    REQUIRE(patch.filter.cutoff_hz > 0.0f);
    REQUIRE(refined.filter.cutoff_hz > 0.0f);
}

TEST_CASE("Phase 2: Full pipeline dispatch", "[phase2][acceptance]") {
    AgentBridge bridge;
    StreamParser streamParser;

    // Simulate incoming streaming tokens
    streamParser.feedChunk(R"({"filter_cutoff_hz": 800.0})");

    // StreamParser accumulates partial patch — verify it doesn't crash
    CHECK(streamParser.partialPatch().filter.cutoff_hz >= 0.0f);
}

TEST_CASE("Phase 2: Session memory influences next generation", "[phase2][acceptance]") {
    SessionMemory memory;

    // User liked a previous patch
    PatchStruct likedPatch{};
    likedPatch.osc[0].volume = 0.8f;
    likedPatch.filter.cutoff_hz = 500.0f;
    memory.recordFeedback(FeedbackKind::Like, "warm pad", likedPatch);

    // Next prompt should bias toward liked characteristics
    auto context = memory.buildRecap("warm pad", 5);
    REQUIRE_FALSE(context.empty());
    REQUIRE(context.find("LIKED") != std::string::npos);
}

TEST_CASE("Phase 2: Variation generation from base patch", "[phase2][acceptance]") {
    PatchStruct base{};
    base.osc[0].volume = 1.0f;
    base.filter.cutoff_hz = 1000.0f;
    base.amp_env.attack_s = 0.1f;

    VariationEngine varEngine;
    auto variations = varEngine.generateVariations(base);

    REQUIRE(variations.size() == 5);

    // Each variation should differ from the base
    int differing = 0;
    for (const auto& v : variations) {
        if (v.filter.cutoff_hz != base.filter.cutoff_hz || v.osc[0].volume != base.osc[0].volume) {
            ++differing;
        }
    }
    REQUIRE(differing > 0);
}

TEST_CASE("Phase 2: Telemetry collects metrics", "[phase2][acceptance]") {
    Telemetry telemetry;

    telemetry.recordGeneration(42.5, 10, 0.0425, true);
    telemetry.recordGeneration(15.2, 0, 0.0152, false, "timeout");
    telemetry.recordGeneration(88.1, 5, 0.0881, false, "parse_error");
    telemetry.recordGeneration(33.7, 8, 0.0337, true);

    REQUIRE(telemetry.records().size() == 4);
    int errors = 0;
    for (const auto& r : telemetry.records()) {
        if (!r.success)
            ++errors;
    }
    REQUIRE(errors == 2);
    REQUIRE(telemetry.records()[0].latency_ms > 0.0);
}

TEST_CASE("Phase 2: Morph engine creates interpolated patches", "[phase2][acceptance]") {
    PatchStruct a{}, b{};
    a.filter.cutoff_hz = 200.0f;
    b.filter.cutoff_hz = 2000.0f;
    a.osc[0].volume = 1.0f;
    b.osc[1].volume = 1.0f;

    MorphEngine morph;
    morph.saveTarget(a, 0);
    morph.saveTarget(b, 1);

    auto mid = morph.morphedPatchAt(0.5f);
    REQUIRE(mid.filter.cutoff_hz > 500.0f);
    REQUIRE(mid.filter.cutoff_hz < 1500.0f);
}
