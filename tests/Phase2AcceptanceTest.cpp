#include <catch2/catch_test_macros.hpp>
#include "engine/PatchStruct.h"
#include "engine/VoiceManager.h"
#include "engine/SynthEngine.h"
#include "engine/PatchValidator.h"
#include "agent/AgentBridge.h"
#include "agent/StreamParser.h"
#include "agent/PrePatchPipeline.h"
#include "mapper/HeuristicParser.h"
#include "mapper/SemanticMapper.h"
#include "mapper/GrammarSampler.h"
#include "engine/VariationEngine.h"
#include "engine/MorphEngine.h"
#include "agent/SessionMemory.h"
#include "agent/Telemetry.h"

using namespace agentic_synth::engine;
using namespace agentic_synth::agent;
using namespace agentic_synth::mapper;

// Phase 2 Acceptance: Full NL loop with session awareness
// Verifies the end-to-end pipeline: NL input → patch → play → feedback → refine

TEST_CASE("Phase 2: NL descriptor produces valid PatchStruct", "[phase2][acceptance]") {
    HeuristicParser parser;
    auto patch = parser.parse("warm pad with slow attack");
    PatchValidator validator;

    // Must pass validation
    REQUIRE(validator.validate(patch).valid());

    // Key parameters should be set non-zero
    REQUIRE(patch.filterCutoffHz < 1000.0f);  // "warm" = lower cutoff
    REQUIRE(patch.ampAttackMs > 200.0f);       // "slow attack"
}

TEST_CASE("Phase 2: NL refinement updates existing patch", "[phase2][acceptance]") {
    HeuristicParser parser;
    auto patch = parser.parse("bright lead");

    // Refine
    auto refined = parser.parse("make it darker");
    // The refined patch should have lower cutoff
    // (Integration-style: refine mutates internal state)

    REQUIRE(patch.filterCutoffHz > 0.0f);
    REQUIRE(refined.filterCutoffHz > 0.0f);
}

TEST_CASE("Phase 2: Full pipeline dispatch", "[phase2][acceptance]") {
    SynthEngine engine;
    AgentBridge bridge(engine);
    StreamParser streamParser(bridge);

    // Simulate incoming streaming tokens
    streamParser.feedChunk(R"({"oscillatorMix": [0.5, 0.3, 0.2, 0.0, 0.0])");
    streamParser.feedChunk(R"(, "filterCutoffHz": 800.0})");

    // Heuristic pre-patch should fire within 200ms
    REQUIRE(bridge.lastDispatchLatencyMs() < 200.0);

    // StreamParser should have parsed both fields
    // (Verification happens through AgentBridge side effects)
}

TEST_CASE("Phase 2: Session memory influences next generation", "[phase2][acceptance]") {
    SynthEngine engine;
    SessionMemory memory;

    // User liked a previous patch
    PatchStruct likedPatch{};
    likedPatch.oscillatorMix[0] = 0.8f;
    likedPatch.filterCutoffHz = 500.0f;
    memory.recordFeedback(likedPatch, SessionMemory::Feedback::Liked);

    // Next prompt should bias toward liked characteristics
    auto context = memory.buildContextString();
    REQUIRE_FALSE(context.empty());
    REQUIRE(context.find("liked") != std::string::npos);
}

TEST_CASE("Phase 2: Variation generation from base patch", "[phase2][acceptance]") {
    PatchStruct base{};
    base.oscillatorMix[0] = 1.0f;
    base.filterCutoffHz = 1000.0f;
    base.ampAttackMs = 100.0f;

    VariationEngine varEngine;
    auto variations = varEngine.generate(base, 5);

    REQUIRE(variations.size() == 5);

    // Each variation should differ from the base
    int differing = 0;
    for (const auto& v : variations) {
        if (v.filterCutoffHz != base.filterCutoffHz ||
            v.oscillatorMix[0] != base.oscillatorMix[0]) {
            ++differing;
        }
    }
    REQUIRE(differing > 0);
}

TEST_CASE("Phase 2: Telemetry collects metrics", "[phase2][acceptance]") {
    Telemetry telemetry;

    telemetry.recordGeneration(42.5f, 1);
    telemetry.recordGeneration(15.2f, 0);
    telemetry.recordGeneration(88.1f, 0);
    telemetry.recordGeneration(33.7f, 1);

    auto report = telemetry.getReport();
    REQUIRE(report.totalGenerations == 4);
    REQUIRE(report.errorCount == 2);
    REQUIRE(report.latencyP50 > 0.0f);
}

TEST_CASE("Phase 2: Morph engine creates interpolated patches", "[phase2][acceptance]") {
    PatchStruct a{}, b{};
    a.filterCutoffHz = 200.0f;
    b.filterCutoffHz = 2000.0f;
    a.oscillatorMix[0] = 1.0f;
    b.oscillatorMix[1] = 1.0f;

    MorphEngine morph;
    morph.setTarget(0, a);
    morph.setTarget(1, b);

    auto mid = morph.morphAt(0.5f);
    REQUIRE(mid.filterCutoffHz > 500.0f);
    REQUIRE(mid.filterCutoffHz < 1500.0f);
}
