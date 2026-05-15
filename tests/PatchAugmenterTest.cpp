// Phase 23 — PatchAugmenter guardrail tests.
//
// The augmenter post-processes LLM-generated patches BEFORE they reach the
// engine, enforcing system-prompt §0 rules 10 + 12 (3-osc default + noise-
// only ban). The tests below cover the four documented strategies:
//
//   • Single Saw + descriptive prompt → Reese topology
//   • Noise-only patch                → pitched fundamental + noise as
//                                       texture (osc[2])
//   • Simple prompt ("pure sub")      → patch passes through untouched
//   • Already 3-osc patch             → patch passes through untouched
//   • Already 2-osc patch             → third slot filled, first two
//                                       preserved
//
// Refinement-mode short-circuit lives one level up in
// PromptHandler::generateLlmPatch — when previousPatch is supplied the
// augmenter is bypassed. There is no integration test for that path here
// because it would need a live LLM; the unit test for the bypass is the
// presence of the `if (!refinement) augmentPatch(...)` guard in
// PromptHandler.cpp, exercised by the existing RefinementContextTest cases.

#include <catch2/catch_test_macros.hpp>

#include "agent/PatchAugmenter.h"
#include "engine/PatchStruct.h"

using namespace agentic_synth;
using agentic_synth::agent::augmentPatch;
using agentic_synth::agent::isSimplePrompt;

namespace {

// Build a PatchStruct with the requested single osc audible and the other two
// disabled at zero volume. Mirrors what a "lazy" LLM emits when it ignores
// the 3-osc rule.
PatchStruct singleOscPatch(OscType type, float volume = 0.8f) {
    PatchStruct p = make_default_patch();
    for (auto& o : p.osc) {
        o.enabled = 0;
        o.volume = 0.0f;
    }
    p.osc[0].type = type;
    p.osc[0].volume = volume;
    p.osc[0].enabled = 1;
    return p;
}

// Build a noise-only patch: all three oscs are Noise (or only Noise audible).
PatchStruct noiseOnlyPatch(float vol = 0.7f) {
    PatchStruct p = make_default_patch();
    for (auto& o : p.osc) {
        o.type = OscType::Noise;
        o.enabled = 0;
        o.volume = 0.0f;
    }
    p.osc[0].type = OscType::Noise;
    p.osc[0].volume = vol;
    p.osc[0].enabled = 1;
    return p;
}

} // namespace

TEST_CASE("isSimplePrompt: matches §0-rule-12 exemptions", "[augmenter][phase23]") {
    REQUIRE(isSimplePrompt("pure sine sub"));
    REQUIRE(isSimplePrompt("just a sine"));
    REQUIRE(isSimplePrompt("single oscillator lead"));
    REQUIRE(isSimplePrompt("one osc bass"));
    REQUIRE(isSimplePrompt("a minimal pluck"));
    REQUIRE(isSimplePrompt("pure tone"));
    REQUIRE(isSimplePrompt("raw saw"));
    REQUIRE(isSimplePrompt("clean sub"));
}

TEST_CASE("isSimplePrompt: descriptive prompts return false", "[augmenter][phase23]") {
    REQUIRE_FALSE(isSimplePrompt("warm pad"));
    REQUIRE_FALSE(isSimplePrompt("dubstep bass"));
    REQUIRE_FALSE(isSimplePrompt("acid lead"));
    REQUIRE_FALSE(isSimplePrompt("glass cathedral pad"));
    REQUIRE_FALSE(isSimplePrompt("warm sub that breathes"));
    REQUIRE_FALSE(isSimplePrompt(""));
}

TEST_CASE("augmentPatch: single Sawtooth + 'warm pad' → Reese topology", "[augmenter][phase23]") {
    PatchStruct p = singleOscPatch(OscType::Sawtooth);
    REQUIRE(augmentPatch(p, "warm pad"));

    // osc[0] preserved as the seed saw
    REQUIRE(p.osc[0].type == OscType::Sawtooth);
    REQUIRE(p.osc[0].enabled);

    // osc[1] = detuned saw partner
    REQUIRE(p.osc[1].type == OscType::Sawtooth);
    REQUIRE(p.osc[1].enabled);
    REQUIRE(p.osc[1].detune_cents == -10.0f);
    REQUIRE(p.osc[1].volume >= 0.15f);

    // osc[2] = sub-octave sine
    REQUIRE(p.osc[2].type == OscType::Sine);
    REQUIRE(p.osc[2].enabled);
    REQUIRE(p.osc[2].semitone_offset == -12.0f);
    REQUIRE(p.osc[2].volume >= 0.15f);
}

TEST_CASE("augmentPatch: noise-only + 'atmospheric dark texture' → Triangle + Noise@2",
          "[augmenter][phase23]") {
    PatchStruct p = noiseOnlyPatch(0.6f);
    REQUIRE(augmentPatch(p, "atmospheric dark texture"));

    // osc[0] became a pitched fundamental — Triangle is the default for
    // textures that aren't tagged bass/pad/lead/ambient.
    REQUIRE(p.osc[0].type == OscType::Triangle);
    REQUIRE(p.osc[0].enabled);
    REQUIRE(p.osc[0].volume >= 0.15f);

    // osc[2] is the noise layer with audible volume preserved.
    REQUIRE(p.osc[2].type == OscType::Noise);
    REQUIRE(p.osc[2].enabled);
    REQUIRE(p.osc[2].volume >= 0.25f);
}

TEST_CASE("augmentPatch: noise-only + 'bass' → Sawtooth fundamental", "[augmenter][phase23]") {
    PatchStruct p = noiseOnlyPatch();
    REQUIRE(augmentPatch(p, "dark bass with grit"));
    REQUIRE(p.osc[0].type == OscType::Sawtooth);
    REQUIRE(p.osc[2].type == OscType::Noise);
}

TEST_CASE("augmentPatch: noise-only + 'ambient drone' → Sine fundamental", "[augmenter][phase23]") {
    PatchStruct p = noiseOnlyPatch();
    REQUIRE(augmentPatch(p, "ambient drone"));
    REQUIRE(p.osc[0].type == OscType::Sine);
    REQUIRE(p.osc[2].type == OscType::Noise);
}

TEST_CASE("augmentPatch: single Sine + 'pure sub' → NOT modified (simple-prompt exemption)",
          "[augmenter][phase23]") {
    PatchStruct p = singleOscPatch(OscType::Sine);
    const auto before = p;
    REQUIRE_FALSE(augmentPatch(p, "pure sub"));
    REQUIRE(p.osc[0].type == before.osc[0].type);
    REQUIRE(p.osc[1].enabled == before.osc[1].enabled);
    REQUIRE(p.osc[2].enabled == before.osc[2].enabled);
}

TEST_CASE("augmentPatch: single Sine + 'just a sine bass' → NOT modified", "[augmenter][phase23]") {
    PatchStruct p = singleOscPatch(OscType::Sine);
    REQUIRE_FALSE(augmentPatch(p, "just a sine bass"));
    REQUIRE_FALSE(p.osc[1].enabled);
    REQUIRE_FALSE(p.osc[2].enabled);
}

TEST_CASE("augmentPatch: already 3-osc patch → not modified", "[augmenter][phase23]") {
    PatchStruct p = make_default_patch();
    p.osc[0].type = OscType::Sawtooth;
    p.osc[0].volume = 0.7f;
    p.osc[0].enabled = 1;
    p.osc[1].type = OscType::Sawtooth;
    p.osc[1].volume = 0.5f;
    p.osc[1].detune_cents = -10.0f;
    p.osc[1].enabled = 1;
    p.osc[2].type = OscType::Sine;
    p.osc[2].volume = 0.45f;
    p.osc[2].semitone_offset = -12.0f;
    p.osc[2].enabled = 1;

    const auto before = p;
    REQUIRE_FALSE(augmentPatch(p, "warm pad"));
    REQUIRE(p.osc[0].type == before.osc[0].type);
    REQUIRE(p.osc[0].volume == before.osc[0].volume);
    REQUIRE(p.osc[1].type == before.osc[1].type);
    REQUIRE(p.osc[1].detune_cents == before.osc[1].detune_cents);
    REQUIRE(p.osc[2].type == before.osc[2].type);
    REQUIRE(p.osc[2].semitone_offset == before.osc[2].semitone_offset);
}

TEST_CASE("augmentPatch: already 2-osc patch + non-simple prompt → osc[2] added, [0,1] preserved",
          "[augmenter][phase23]") {
    PatchStruct p = make_default_patch();
    // osc[0] = audible Triangle pad seed
    p.osc[0].type = OscType::Triangle;
    p.osc[0].volume = 0.7f;
    p.osc[0].semitone_offset = 0.0f;
    p.osc[0].enabled = 1;
    // osc[1] = audible Triangle detune
    p.osc[1].type = OscType::Triangle;
    p.osc[1].volume = 0.55f;
    p.osc[1].detune_cents = 8.0f;
    p.osc[1].enabled = 1;
    // osc[2] = disabled
    p.osc[2].enabled = 0;
    p.osc[2].volume = 0.0f;

    const auto before = p;
    REQUIRE(augmentPatch(p, "warm pad"));

    // osc[0] / osc[1] untouched
    REQUIRE(p.osc[0].type == before.osc[0].type);
    REQUIRE(p.osc[0].volume == before.osc[0].volume);
    REQUIRE(p.osc[1].type == before.osc[1].type);
    REQUIRE(p.osc[1].detune_cents == before.osc[1].detune_cents);
    REQUIRE(p.osc[1].volume == before.osc[1].volume);

    // osc[2] now audible
    REQUIRE(p.osc[2].enabled);
    REQUIRE(p.osc[2].volume >= 0.15f);
}

TEST_CASE("augmentPatch: single Triangle + 'lush pad' → Pad topology", "[augmenter][phase23]") {
    PatchStruct p = singleOscPatch(OscType::Triangle);
    REQUIRE(augmentPatch(p, "lush evolving pad"));
    REQUIRE(p.osc[0].type == OscType::Triangle);
    REQUIRE(p.osc[1].type == OscType::Triangle);
    REQUIRE(p.osc[1].detune_cents == 9.0f);
    REQUIRE(p.osc[2].type == OscType::Sine);
    REQUIRE(p.osc[2].semitone_offset == 12.0f);
}

TEST_CASE("augmentPatch: single FM + 'electric piano' → FM topology", "[augmenter][phase23]") {
    PatchStruct p = singleOscPatch(OscType::FM);
    REQUIRE(augmentPatch(p, "electric piano with tine bell"));
    REQUIRE(p.osc[0].type == OscType::FM);
    REQUIRE(p.osc[1].type == OscType::Sine);
    REQUIRE(p.osc[1].semitone_offset == 0.0f);
    REQUIRE(p.osc[2].type == OscType::Sine);
    REQUIRE(p.osc[2].semitone_offset == 12.0f);
}

TEST_CASE("augmentPatch: single Square + 'hollow lead' → generic layering", "[augmenter][phase23]") {
    PatchStruct p = singleOscPatch(OscType::Square);
    REQUIRE(augmentPatch(p, "hollow reedy lead"));
    REQUIRE(p.osc[0].type == OscType::Square);
    REQUIRE(p.osc[1].type == OscType::Square);
    REQUIRE(p.osc[2].type == OscType::Sine);
}

TEST_CASE("augmentPatch: enabled but inaudible osc (vol < 0.15) counts as silent",
          "[augmenter][phase23]") {
    // The §0 rule 12 audibility threshold is 0.15 — an osc marked enabled
    // with volume=0.05 contributes nothing the producer can hear, so the
    // augmenter must treat it as a silent slot.
    PatchStruct p = singleOscPatch(OscType::Sawtooth, 0.8f);
    p.osc[1].enabled = 1; // LLM marked enabled but at fadeout volume
    p.osc[1].volume = 0.05f;
    p.osc[1].type = OscType::Sawtooth;

    REQUIRE(augmentPatch(p, "warm pad"));
    // The Reese strategy should overwrite osc[1] with a proper detune partner.
    REQUIRE(p.osc[1].volume >= 0.15f);
    REQUIRE(p.osc[1].detune_cents == -10.0f);
}

// Phase 26 — augmenter_actions transparency log.
//
// Each strategy must populate the augmenter_actions buffer so the UI can
// render a "patch adjusted" banner. The string is user-facing (sensory-
// first, no jargon) and pipe-separated when multiple actions stack
// (currently the augmenter only fires one strategy per call, so the
// buffer holds one entry).

TEST_CASE("augmenter_actions: noise-only fix writes a user-facing description",
          "[augmenter][phase26]") {
    PatchStruct p = noiseOnlyPatch();
    REQUIRE(augmentPatch(p, "electric storm bass"));
    REQUIRE(p.augmenter_actions[0] != '\0');
    const std::string actions{p.augmenter_actions};
    REQUIRE(actions.find("noise") != std::string::npos);
    REQUIRE(actions.find("|") == std::string::npos);
}

TEST_CASE("augmenter_actions: single-saw → Reese writes layering description",
          "[augmenter][phase26]") {
    PatchStruct p = singleOscPatch(OscType::Sawtooth);
    REQUIRE(augmentPatch(p, "huge dubstep bass"));
    REQUIRE(p.augmenter_actions[0] != '\0');
    const std::string actions{p.augmenter_actions};
    REQUIRE(actions.find("Reese") != std::string::npos);
}

TEST_CASE("augmenter_actions: simple prompt produces empty action buffer",
          "[augmenter][phase26]") {
    PatchStruct p = singleOscPatch(OscType::Sine);
    REQUIRE_FALSE(augmentPatch(p, "pure sine sub"));
    REQUIRE(p.augmenter_actions[0] == '\0');
}

TEST_CASE("augmenter_actions: already-3-osc patch leaves action buffer empty",
          "[augmenter][phase26]") {
    PatchStruct p = make_default_patch();
    for (auto& o : p.osc) {
        o.enabled = 1;
        o.volume = 0.6f;
        o.type = OscType::Sawtooth;
    }
    REQUIRE_FALSE(augmentPatch(p, "warm pad"));
    REQUIRE(p.augmenter_actions[0] == '\0');
}
