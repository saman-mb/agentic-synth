#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "agent/PrePatchPipeline.h"

using namespace agentic_synth;
using namespace agentic_synth::agent;

TEST_CASE("submit dispatch latency is < 200 ms") {
    PrePatchPipeline pipeline;
    pipeline.submit("bright lead");
    CHECK(pipeline.lastDispatchLatencyMs() < 200.0);
}

TEST_CASE("poll returns heuristic patch immediately after submit") {
    PrePatchPipeline pipeline;
    const auto dispatched = pipeline.submit("dark pad");
    const auto polled = pipeline.poll();
    REQUIRE(polled.has_value());
    CHECK(polled->filter.cutoff_hz == Catch::Approx(dispatched.filter.cutoff_hz));
    CHECK(polled->amp_env.attack_s == Catch::Approx(dispatched.amp_env.attack_s));
}

TEST_CASE("refinePatch pushes kTransitionSteps patches to queue") {
    PrePatchPipeline pipeline;
    pipeline.submit("warm pad");

    // Drain the initial heuristic patch.
    pipeline.poll();

    PatchStruct llmPatch = make_default_patch();
    llmPatch.filter.cutoff_hz = 8000.0f;
    llmPatch.reverb.mix = 0.7f;
    pipeline.refinePatch(llmPatch);

    int count = 0;
    while (pipeline.poll().has_value())
        ++count;

    CHECK(count == PrePatchPipeline::kTransitionSteps);
}

TEST_CASE("interpolated patches monotonically approach LLM target") {
    PrePatchPipeline pipeline;
    PatchStruct heuristic = pipeline.submit("plucky bass");

    // Drain the heuristic.
    pipeline.poll();

    PatchStruct llmPatch = heuristic;
    llmPatch.filter.cutoff_hz = heuristic.filter.cutoff_hz + 4000.0f;
    pipeline.refinePatch(llmPatch);

    float prev = heuristic.filter.cutoff_hz;
    std::optional<PatchStruct> p;
    while ((p = pipeline.poll()).has_value()) {
        // Each step must be >= previous (we're moving cutoff upward).
        CHECK(p->filter.cutoff_hz >= prev - 1.0f); // 1 Hz tolerance
        prev = p->filter.cutoff_hz;
    }
    // Final step must equal (or be very close to) LLM cutoff.
    CHECK(prev == Catch::Approx(llmPatch.filter.cutoff_hz).epsilon(0.01f));
}

TEST_CASE("lerpPatch at t=0 equals source") {
    const PatchStruct a = make_default_patch();
    PatchStruct b = make_default_patch();
    b.filter.cutoff_hz = 1000.0f;
    b.reverb.mix = 0.8f;

    const PatchStruct result = lerpPatch(a, b, 0.0f);
    CHECK(result.filter.cutoff_hz == Catch::Approx(a.filter.cutoff_hz));
    CHECK(result.reverb.mix == Catch::Approx(a.reverb.mix));
}

TEST_CASE("lerpPatch at t=1 equals target") {
    const PatchStruct a = make_default_patch();
    PatchStruct b = make_default_patch();
    b.filter.cutoff_hz = 500.0f;
    b.reverb.mix = 0.9f;

    const PatchStruct result = lerpPatch(a, b, 1.0f);
    CHECK(result.filter.cutoff_hz == Catch::Approx(b.filter.cutoff_hz));
    CHECK(result.reverb.mix == Catch::Approx(b.reverb.mix));
}

TEST_CASE("lerpPatch at t=0.5 produces midpoint float values") {
    PatchStruct a = make_default_patch();
    a.filter.cutoff_hz = 1000.0f;
    a.reverb.mix = 0.0f;

    PatchStruct b = make_default_patch();
    b.filter.cutoff_hz = 3000.0f;
    b.reverb.mix = 1.0f;

    const PatchStruct result = lerpPatch(a, b, 0.5f);
    CHECK(result.filter.cutoff_hz == Catch::Approx(2000.0f).epsilon(0.001f));
    CHECK(result.reverb.mix == Catch::Approx(0.5f).epsilon(0.001f));
}
