#include <catch2/catch_test_macros.hpp>

#include "agent/SessionMemory.h"
#include "engine/PatchStruct.h"

using namespace agentic_synth;
using namespace agentic_synth::agent;

static PatchStruct makePatchWithCutoff(float cutoff_hz) {
    PatchStruct p = make_default_patch();
    p.filter.cutoff_hz = cutoff_hz;
    p.amp_env.attack_s = 1.0f; // slow attack → pad-like
    p.reverb.mix = 0.7f;
    return p;
}

// ---------------------------------------------------------------------------
// Unit tests
// ---------------------------------------------------------------------------

TEST_CASE("SessionMemory: records events and buildRecap reflects them") {
    SessionMemory mem;
    auto patch = makePatchWithCutoff(500.0f);
    mem.recordFeedback(FeedbackKind::Like, "dark pad", patch);

    REQUIRE(mem.events().size() == 1);
    CHECK(mem.events()[0].kind == FeedbackKind::Like);
    CHECK(mem.events()[0].prompt == "dark pad");

    std::string recap = mem.buildRecap("dark pad", 5);
    CHECK(!recap.empty());
    // Should mention LIKED and the prompt
    CHECK(recap.find("LIKED") != std::string::npos);
    CHECK(recap.find("dark pad") != std::string::npos);
}

TEST_CASE("SessionMemory: dislike pushes bias away from that parameter region") {
    SessionMemory mem;
    // Dislike a bright patch (high cutoff)
    auto bright = makePatchWithCutoff(12000.0f);
    bright.amp_env.attack_s = 0.005f;
    mem.recordFeedback(FeedbackKind::Dislike, "bright lead", bright);

    PatchVector bias = mem.computeParameterBias("bright lead");
    // Cutoff dimension should be negative (suppress high-cutoff region)
    CHECK(bias[0] < 0.0f);
}

TEST_CASE("SessionMemory: like pushes bias toward that parameter region") {
    SessionMemory mem;
    // Like a dark patch (low cutoff)
    auto dark = makePatchWithCutoff(400.0f);
    mem.recordFeedback(FeedbackKind::Like, "dark pad", dark);

    PatchVector bias = mem.computeParameterBias("dark pad");
    // Cutoff dimension should be negative (push toward lower cutoff = dark)
    CHECK(bias[0] < 0.0f);
}

TEST_CASE("SessionMemory: cosine similarity is 1.0 for identical vectors") {
    PatchVector v{0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f};
    CHECK(SessionMemory::cosineSimilarity(v, v) > 0.9999f);
}

TEST_CASE("SessionMemory: cosine similarity is 0 for zero vector") {
    PatchVector a{0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    PatchVector b{1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    CHECK(SessionMemory::cosineSimilarity(a, b) == 0.0f);
}

TEST_CASE("SessionMemory: normalizeCutoff is monotone") {
    float n1 = SessionMemory::normalizeCutoff(100.0f);
    float n2 = SessionMemory::normalizeCutoff(1000.0f);
    float n3 = SessionMemory::normalizeCutoff(10000.0f);
    CHECK(n1 < n2);
    CHECK(n2 < n3);
    CHECK(n1 >= 0.0f);
    CHECK(n3 <= 1.0f);
}

TEST_CASE("SessionMemory: promptToVector dark < bright on cutoff dimension") {
    PatchVector dark = SessionMemory::promptToVector("dark pad");
    PatchVector bright = SessionMemory::promptToVector("bright lead");
    CHECK(dark[0] < bright[0]);
}

// ---------------------------------------------------------------------------
// Convergence test: repeated 'dark' likes converge filter cutoff bias lower
// ---------------------------------------------------------------------------

TEST_CASE("SessionMemory: 5 dark likes converge filter cutoff bias to negative") {
    SessionMemory mem;

    // Simulate 5 iterations: user always likes a dark patch with cutoff < 1000 Hz.
    for (int i = 0; i < 5; ++i) {
        // Slightly vary cutoff across iterations to model realistic feedback.
        float cutoff = 400.0f + static_cast<float>(i) * 80.0f; // 400..720 Hz
        auto patch = makePatchWithCutoff(cutoff);
        mem.recordFeedback(FeedbackKind::Like, "dark filter sound", patch);
    }

    REQUIRE(mem.events().size() == 5);

    PatchVector bias = mem.computeParameterBias("dark filter sound");

    // After 5 likes on low-cutoff patches, bias[0] (cutoff) must be negative,
    // meaning the model should be guided toward lower filter cutoff values.
    INFO("bias[0] (cutoff) = " << bias[0]);
    CHECK(bias[0] < 0.0f);

    // Also verify the recap references all events.
    std::string recap = mem.buildRecap("dark filter sound", 10);
    CHECK(recap.find("LIKED") != std::string::npos);
}

TEST_CASE("SessionMemory: clear removes all events") {
    SessionMemory mem;
    mem.recordFeedback(FeedbackKind::Like, "dark pad", makePatchWithCutoff(500.0f));
    mem.clear();
    CHECK(mem.events().empty());
    PatchVector bias = mem.computeParameterBias("dark pad");
    for (float b : bias) CHECK(b == 0.0f);
}
