#include <catch2/catch_test_macros.hpp>

#include "mapper/HeuristicParser.h"

using agentsynth::HeuristicParser;
using agentsynth::LfoTarget;
using agentsynth::OscType;

TEST_CASE("bright lead: high cutoff, saw wave") {
    HeuristicParser parser;
    auto patch = parser.parse("bright lead");
    CHECK(patch.filter.cutoff_hz > 4000.0f);
    CHECK(patch.osc[0].type == OscType::Sawtooth);
}

TEST_CASE("dark pad: low cutoff, slow attack, reverb") {
    HeuristicParser parser;
    auto patch = parser.parse("dark pad");
    CHECK(patch.filter.cutoff_hz < 1000.0f);
    CHECK(patch.amp_env.attack_s >= 0.5f);
    CHECK(patch.reverb.mix > 0.0f);
}

TEST_CASE("plucky bass: no sustain, low register") {
    HeuristicParser parser;
    auto patch = parser.parse("plucky bass");
    CHECK(patch.amp_env.sustain == 0.0f);
    CHECK(patch.osc[0].semitone_offset < 0.0f);
}

TEST_CASE("evolving ambient: active LFO, high reverb mix") {
    HeuristicParser parser;
    auto patch = parser.parse("evolving ambient");
    CHECK(patch.lfo[0].depth > 0.5f);
    CHECK(patch.reverb.mix > 0.4f);
    CHECK(patch.lfo[0].target == LfoTarget::FilterCutoff);
}

TEST_CASE("warm mono legato: monophonic with portamento") {
    HeuristicParser parser;
    auto patch = parser.parse("warm mono legato");
    CHECK(patch.voice_count == 1);
    CHECK(patch.portamento_s > 0.0f);
}
