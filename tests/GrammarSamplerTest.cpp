#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "mapper/GrammarSampler.h"

using agentic_synth::FilterType;
using agentic_synth::kPatchStructVersion;
using agentic_synth::LfoTarget;
using agentic_synth::LfoWaveform;
using agentic_synth::OscType;
using agentic_synth::mapper::GrammarSampler;

// Minimal valid PatchStruct JSON that matches the GBNF grammar field order.
static std::string make_valid_json(uint32_t patch_id = 0) {
    return R"({
  "version": 1,
  "patch_id": )" +
           std::to_string(patch_id) + R"(,
  "osc": [
    {"type": "Sawtooth", "semitone_offset": 0.0, "detune_cents": 0.0, "wavetable_pos": 0.0,
     "fm_ratio": 1.0, "fm_depth": 0.0, "volume": 1.0, "pan": 0.0, "pulse_width": 0.5, "enabled": true},
    {"type": "Sine",     "semitone_offset": 0.0, "detune_cents": 0.0, "wavetable_pos": 0.0,
     "fm_ratio": 1.0, "fm_depth": 0.0, "volume": 0.0, "pan": 0.0, "pulse_width": 0.5, "enabled": false},
    {"type": "Sine",     "semitone_offset": 0.0, "detune_cents": 0.0, "wavetable_pos": 0.0,
     "fm_ratio": 1.0, "fm_depth": 0.0, "volume": 0.0, "pan": 0.0, "pulse_width": 0.5, "enabled": false}
  ],
  "filter": {"type": "LowPass", "cutoff_hz": 18000.0, "resonance": 0.0, "env_mod": 0.0, "key_track": 0.0, "drive": 0.0},
  "filter_env": {"attack_s": 0.01, "decay_s": 0.2, "sustain": 0.0, "release_s": 0.1},
  "amp_env":    {"attack_s": 0.005, "decay_s": 0.1, "sustain": 1.0, "release_s": 0.1},
  "lfo": [
    {"waveform": "Sine", "target": "None", "rate_hz": 1.0, "depth": 0.0, "phase_offset": 0.0, "bpm_sync": false},
    {"waveform": "Sine", "target": "None", "rate_hz": 1.0, "depth": 0.0, "phase_offset": 0.0, "bpm_sync": false}
  ],
  "reverb": {"size": 0.0, "damping": 0.0, "width": 0.0, "mix": 0.0},
  "delay":  {"time_s": 0.0, "feedback": 0.0, "mix": 0.0, "bpm_sync": false},
  "master_gain": 1.0,
  "portamento_s": 0.0,
  "voice_count": 8
})";
}

TEST_CASE("parse_patch_json: valid JSON round-trips to correct PatchStruct") {
    auto result = GrammarSampler::parse_patch_json(make_valid_json(42));
    REQUIRE(result.has_value());
    CHECK(result->version == kPatchStructVersion);
    CHECK(result->patch_id == 42u);
    CHECK(result->osc[0].type == OscType::Sawtooth);
    CHECK(result->osc[0].enabled == 1);
    CHECK(result->osc[1].enabled == 0);
    CHECK(result->filter.type == FilterType::LowPass);
    CHECK(result->filter.cutoff_hz == Catch::Approx(18000.0f));
    CHECK(result->amp_env.sustain == Catch::Approx(1.0f));
    CHECK(result->voice_count == 8);
    CHECK(result->master_gain == Catch::Approx(1.0f));
}

TEST_CASE("parse_patch_json: all OscType enum values accepted") {
    for (const char* t : {"Sine", "Triangle", "Sawtooth", "Square", "Pulse", "Wavetable", "FM", "Noise"}) {
        std::string json = make_valid_json();
        // Replace first "Sawtooth"
        auto pos = json.find("\"Sawtooth\"");
        json.replace(pos, 10, std::string("\"") + t + "\"");
        auto r = GrammarSampler::parse_patch_json(json);
        INFO("OscType: " << t);
        REQUIRE(r.has_value());
    }
}

TEST_CASE("parse_patch_json: all FilterType enum values accepted") {
    for (const char* t : {"LowPass", "HighPass", "BandPass", "Notch", "Peak"}) {
        std::string json = make_valid_json();
        auto pos = json.find("\"LowPass\"");
        json.replace(pos, 9, std::string("\"") + t + "\"");
        auto r = GrammarSampler::parse_patch_json(json);
        INFO("FilterType: " << t);
        REQUIRE(r.has_value());
    }
}

TEST_CASE("parse_patch_json: all LfoTarget enum values accepted") {
    for (const char* t : {"None", "Pitch", "FilterCutoff", "Amplitude", "Pan", "WavetablePos", "FmRatio"}) {
        std::string json = make_valid_json();
        auto pos = json.find("\"None\"");
        json.replace(pos, 6, std::string("\"") + t + "\"");
        auto r = GrammarSampler::parse_patch_json(json);
        INFO("LfoTarget: " << t);
        REQUIRE(r.has_value());
    }
}

TEST_CASE("parse_patch_json: all LfoWaveform enum values accepted") {
    for (const char* t : {"Sine", "Triangle", "Sawtooth", "Square", "SampleAndHold"}) {
        std::string json = make_valid_json();
        // Replace first lfo waveform "Sine"
        auto pos = json.find("\"waveform\": \"Sine\"");
        if (pos == std::string::npos)
            pos = json.find("\"waveform\":\"Sine\"");
        auto val_start = json.find("\"Sine\"", pos + 10);
        json.replace(val_start, 6, std::string("\"") + t + "\"");
        auto r = GrammarSampler::parse_patch_json(json);
        INFO("LfoWaveform: " << t);
        REQUIRE(r.has_value());
    }
}

TEST_CASE("parse_patch_json: bpm_sync bool accepted as true") {
    std::string json = make_valid_json();
    // Replace first "bpm_sync": false with true
    auto pos = json.find("\"bpm_sync\": false");
    if (pos != std::string::npos)
        json.replace(pos, 17, "\"bpm_sync\": true");
    auto r = GrammarSampler::parse_patch_json(json);
    REQUIRE(r.has_value());
}

TEST_CASE("parse_patch_json: negative float values (pan, semitone_offset)") {
    std::string json = make_valid_json();
    auto pos = json.find("\"pan\": 0.0");
    json.replace(pos, 10, "\"pan\": -0.75");
    auto r = GrammarSampler::parse_patch_json(json);
    REQUIRE(r.has_value());
    CHECK(r->osc[0].pan == Catch::Approx(-0.75f));
}

TEST_CASE("parse_patch_json: patch_id propagates correctly") {
    for (uint32_t id : {0u, 1u, 99u, 1000u}) {
        auto r = GrammarSampler::parse_patch_json(make_valid_json(id));
        REQUIRE(r.has_value());
        CHECK(r->patch_id == id);
    }
}

TEST_CASE("parse_patch_json: returns nullopt for empty input") {
    CHECK_FALSE(GrammarSampler::parse_patch_json("").has_value());
}

TEST_CASE("parse_patch_json: returns nullopt for truncated JSON") {
    const std::string full = make_valid_json();
    CHECK_FALSE(GrammarSampler::parse_patch_json(full.substr(0, full.size() / 2)).has_value());
}

TEST_CASE("parse_patch_json: returns nullopt for unknown enum string") {
    std::string json = make_valid_json();
    auto pos = json.find("\"Sawtooth\"");
    json.replace(pos, 10, "\"Banana\"");
    CHECK_FALSE(GrammarSampler::parse_patch_json(json).has_value());
}

TEST_CASE("parse_patch_json: returns nullopt when cutoff_hz out of valid range") {
    std::string json = make_valid_json();
    auto pos = json.find("18000.0");
    json.replace(pos, 7, "5.0"); // below 20 Hz minimum
    CHECK_FALSE(GrammarSampler::parse_patch_json(json).has_value());
}

TEST_CASE("parse_patch_json: voice_count 1..16 accepted, 0 rejected") {
    for (uint32_t vc : {1u, 4u, 8u, 16u}) {
        std::string json = make_valid_json();
        auto pos = json.find("\"voice_count\": 8");
        json.replace(pos, 16, "\"voice_count\": " + std::to_string(vc));
        auto r = GrammarSampler::parse_patch_json(json);
        INFO("voice_count: " << vc);
        REQUIRE(r.has_value());
        CHECK(r->voice_count == static_cast<uint8_t>(vc));
    }
    // voice_count 0 must fail
    std::string json = make_valid_json();
    auto pos = json.find("\"voice_count\": 8");
    json.replace(pos, 16, "\"voice_count\": 0");
    CHECK_FALSE(GrammarSampler::parse_patch_json(json).has_value());
}

TEST_CASE("parse_patch_json: 100 structurally identical patches all parse correctly") {
    // Validates acceptance criterion: 100/100 well-formed patches parse.
    for (uint32_t i = 0; i < 100; ++i) {
        auto r = GrammarSampler::parse_patch_json(make_valid_json(i));
        INFO("patch_id " << i);
        REQUIRE(r.has_value());
        CHECK(r->patch_id == i);
    }
}
