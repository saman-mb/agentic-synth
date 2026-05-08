#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "agent/StreamParser.h"

using namespace agentic_synth;
using namespace agentic_synth::agent;

TEST_CASE("StreamParser: no callback on empty input") {
    StreamParser sp;
    int calls = 0;
    sp.setCallback([&](const PatchStruct&) { ++calls; });
    sp.feedChunk("");
    CHECK(calls == 0);
}

TEST_CASE("StreamParser: single scalar field fires callback once") {
    StreamParser sp;
    int calls = 0;
    PatchStruct last{};
    sp.setCallback([&](const PatchStruct& p) {
        ++calls;
        last = p;
    });
    sp.feedChunk(R"({"master_gain": 0.5})");
    CHECK(calls == 1);
    CHECK(last.master_gain == Catch::Approx(0.5f).epsilon(0.01f));
}

TEST_CASE("StreamParser: callback fires at field completion, not stream end") {
    StreamParser sp;
    int calls = 0;
    sp.setCallback([&](const PatchStruct&) { ++calls; });
    // Stream stops after the comma; no closing brace yet.
    sp.feedChunk(R"({"master_gain": 0.7,)");
    CHECK(calls == 1);
}

TEST_CASE("StreamParser: object field updates filter params") {
    StreamParser sp;
    PatchStruct last{};
    sp.setCallback([&](const PatchStruct& p) { last = p; });
    sp.feedChunk(R"({"filter": {"cutoff_hz": 2000.0, "resonance": 0.6}})");
    CHECK(last.filter.cutoff_hz == Catch::Approx(2000.0f).epsilon(0.1f));
    CHECK(last.filter.resonance == Catch::Approx(0.6f).epsilon(0.01f));
}

TEST_CASE("StreamParser: amp_env field updates envelope params") {
    StreamParser sp;
    PatchStruct last{};
    sp.setCallback([&](const PatchStruct& p) { last = p; });
    sp.feedChunk(R"({"amp_env": {"attack_s": 0.2, "decay_s": 0.5, "sustain": 0.7, "release_s": 1.0}})");
    CHECK(last.amp_env.attack_s == Catch::Approx(0.2f).epsilon(0.01f));
    CHECK(last.amp_env.sustain == Catch::Approx(0.7f).epsilon(0.01f));
    CHECK(last.amp_env.release_s == Catch::Approx(1.0f).epsilon(0.01f));
}

TEST_CASE("StreamParser: chunked input fires callback across chunk boundary") {
    StreamParser sp;
    int calls = 0;
    sp.setCallback([&](const PatchStruct&) { ++calls; });
    sp.feedChunk(R"({"master_g)");
    CHECK(calls == 0);
    sp.feedChunk(R"(ain": 0.3})");
    CHECK(calls == 1);
}

TEST_CASE("StreamParser: isComplete after full JSON") {
    StreamParser sp;
    sp.feedChunk(R"({"master_gain": 0.8})");
    CHECK(sp.isComplete());
}

TEST_CASE("StreamParser: not complete after partial JSON") {
    StreamParser sp;
    sp.feedChunk(R"({"master_gain": 0.8)");
    CHECK_FALSE(sp.isComplete());
}

TEST_CASE("StreamParser: reset clears state and allows reuse") {
    StreamParser sp;
    int calls = 0;
    sp.setCallback([&](const PatchStruct&) { ++calls; });
    sp.feedChunk(R"({"master_gain": 0.5})");
    CHECK(sp.isComplete());
    sp.reset();
    CHECK_FALSE(sp.isComplete());
    sp.feedChunk(R"({"master_gain": 0.9})");
    CHECK(calls == 2);
}

TEST_CASE("StreamParser: multiple scalar fields each fire callback") {
    StreamParser sp;
    int calls = 0;
    sp.setCallback([&](const PatchStruct&) { ++calls; });
    sp.feedChunk(R"({"master_gain": 0.5, "portamento_s": 0.1, "voice_count": 4})");
    CHECK(calls == 3);
}

TEST_CASE("StreamParser: partialPatch accumulates across fields") {
    StreamParser sp;
    sp.setCallback([](const PatchStruct&) {});
    sp.feedChunk(R"({"master_gain": 0.4, "portamento_s": 0.2})");
    CHECK(sp.partialPatch().master_gain == Catch::Approx(0.4f).epsilon(0.01f));
    CHECK(sp.partialPatch().portamento_s == Catch::Approx(0.2f).epsilon(0.01f));
}

TEST_CASE("StreamParser: reverb field updates all sub-fields") {
    StreamParser sp;
    PatchStruct last{};
    sp.setCallback([&](const PatchStruct& p) { last = p; });
    sp.feedChunk(R"({"reverb": {"size": 0.8, "damping": 0.3, "width": 0.9, "mix": 0.4}})");
    CHECK(last.reverb.size == Catch::Approx(0.8f).epsilon(0.01f));
    CHECK(last.reverb.damping == Catch::Approx(0.3f).epsilon(0.01f));
    CHECK(last.reverb.mix == Catch::Approx(0.4f).epsilon(0.01f));
}

TEST_CASE("StreamParser: osc array updates oscillator params") {
    StreamParser sp;
    PatchStruct last{};
    sp.setCallback([&](const PatchStruct& p) { last = p; });
    sp.feedChunk(
        R"({"osc": [{"type": 2, "volume": 0.9, "enabled": 1, "detune_cents": 0.0, "fm_ratio": 1.0, "fm_depth": 0.0, "pan": 0.0, "pulse_width": 0.5, "semitone_offset": 0.0, "wavetable_pos": 0.0}]})");
    CHECK(last.osc[0].type == OscType::Sawtooth);
    CHECK(last.osc[0].volume == Catch::Approx(0.9f).epsilon(0.01f));
    CHECK(last.osc[0].enabled == 1);
}

TEST_CASE("StreamParser: no crash on unknown field") {
    StreamParser sp;
    int calls = 0;
    sp.setCallback([&](const PatchStruct&) { ++calls; });
    CHECK_NOTHROW(sp.feedChunk(R"({"unknown_field": 42, "master_gain": 0.6})"));
    CHECK(calls == 2);
}
