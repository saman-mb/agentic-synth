#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>

#include "agent/SessionMemory.h"
#include "engine/PatchStruct.h"

using namespace agentic_synth;
using namespace agentic_synth::agent;
using Catch::Approx;

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
    for (float b : bias)
        CHECK(b == 0.0f);
}

TEST_CASE("SessionMemory: Tweak feedback kind recorded and shown in recap") {
    SessionMemory mem;
    auto patch = makePatchWithCutoff(1000.0f);
    mem.recordFeedback(FeedbackKind::Tweak, "tweak filter", patch);

    REQUIRE(mem.events().size() == 1);
    CHECK(mem.events()[0].kind == FeedbackKind::Tweak);
    CHECK(mem.events()[0].prompt == "tweak filter");

    std::string recap = mem.buildRecap("tweak filter", 5);
    CHECK(recap.find("TWEAKED") != std::string::npos);
}

TEST_CASE("SessionMemory: denormalizeCutoff inverts normalizeCutoff") {
    for (float hz : {20.0f, 100.0f, 440.0f, 1000.0f, 8000.0f, 20000.0f}) {
        float norm = SessionMemory::normalizeCutoff(hz);
        float back = SessionMemory::denormalizeCutoff(norm);
        CHECK(std::abs(back - hz) < 1.0f);
    }
}

TEST_CASE("SessionMemory: extractVector maps patch fields to [0,1] range") {
    PatchStruct patch = make_default_patch();
    patch.filter.cutoff_hz = 18000.0f;
    patch.filter.resonance = 0.5f;
    patch.amp_env.attack_s = 0.005f;
    patch.amp_env.sustain = 1.0f;
    patch.lfo[0].depth = 0.0f;
    patch.reverb.mix = 0.0f;
    patch.master_gain = 1.0f;
    patch.osc[0].volume = 1.0f;

    PatchVector v = SessionMemory::extractVector(patch);

    CHECK(v[0] > 0.9f);          // near top of log cutoff scale
    CHECK(v[1] == Approx(0.5f)); // resonance passthrough
    CHECK(v[3] == Approx(1.0f)); // sustain passthrough
    CHECK(v[6] == Approx(1.0f)); // master_gain passthrough
    CHECK(v[7] == Approx(1.0f)); // osc[0].volume passthrough

    for (float x : v)
        CHECK(x >= 0.0f);
    for (float x : v)
        CHECK(x <= 1.0f);
}
