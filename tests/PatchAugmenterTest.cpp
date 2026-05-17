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

#include <cmath>
#include <cstring>

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

TEST_CASE("augmentPatch: noise-only + 'dark texture' → Triangle + Noise@2",
          "[augmenter][phase23]") {
    // Phase 30: avoid cinematic-trigger tokens here so the noise-only path
    // is what we're exercising. "atmospheric" was reclassified as cinematic
    // intent in Phase 30; a separate test (Phase 30 noise-only-then-
    // cinematic) covers the combined path.
    PatchStruct p = noiseOnlyPatch(0.6f);
    REQUIRE(augmentPatch(p, "dark gritty texture"));

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

TEST_CASE("augmentPatch: noise-only + 'ambient warm' → Sine fundamental", "[augmenter][phase23]") {
    // Phase 30: dropped "drone" from this test prompt — "drone" is now a
    // cinematic-intent token that takes precedence over noise-only,
    // routing to the cinematic-pad recipe instead.
    PatchStruct p = noiseOnlyPatch();
    REQUIRE(augmentPatch(p, "ambient warm wash"));
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
    // Phase 30: removed "evolving" — that keyword now routes to the
    // cinematic-pad path which uses a different topology (Sawtooth
    // partner, octave-spread). A plain "lush pad" without cinematic-
    // intent keywords still hits the original Pad layering path.
    PatchStruct p = singleOscPatch(OscType::Triangle);
    REQUIRE(augmentPatch(p, "lush warm pad"));
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

// Phase 27 — FM-intent coercion.
//
// User explicitly named the synthesis technique (FM / DX / Rhodes / bell /
// tine / electric piano), but the LLM shipped a non-FM oscillator. The
// augmenter rebuilds the patch around a canonical FM topology before the
// engine sees it.

TEST_CASE("augmenter FM-intent: 'classic FM aerie sound' on Saw → osc[0] becomes FM",
          "[augmenter][phase27]") {
    PatchStruct p = singleOscPatch(OscType::Sawtooth);
    REQUIRE(augmentPatch(p, "classic FM aerie sound"));
    REQUIRE(p.osc[0].type == OscType::FM);
    REQUIRE(p.osc[0].volume >= 0.15f);
    REQUIRE(p.osc[1].type == OscType::Sine);   // body
    REQUIRE(p.osc[2].type == OscType::Sine);   // shimmer
    REQUIRE(p.osc[2].semitone_offset == 12.0f);
}

TEST_CASE("augmenter FM-intent: 'tine' / 'rhodes' → DX-tine ratio 14.0",
          "[augmenter][phase27]") {
    PatchStruct p = singleOscPatch(OscType::Sawtooth);
    REQUIRE(augmentPatch(p, "rhodes tine sound"));
    REQUIRE(p.osc[0].type == OscType::FM);
    REQUIRE(p.osc[0].fm_ratio == 14.0f);
}

TEST_CASE("augmenter FM-intent: 'bell' / 'glass' → inharmonic ratio 3.14",
          "[augmenter][phase27]") {
    PatchStruct p = singleOscPatch(OscType::Sawtooth);
    REQUIRE(augmentPatch(p, "classic bell tone with shimmer"));
    REQUIRE(p.osc[0].type == OscType::FM);
    REQUIRE(p.osc[0].fm_ratio == 3.14f);
}

TEST_CASE("augmenter FM-intent: filter must stay open (FM is the timbre)",
          "[augmenter][phase27]") {
    PatchStruct p = singleOscPatch(OscType::Sawtooth);
    // Simulate the LLM closing the filter on top of FM (the classic mistake).
    p.filter.cutoff_hz = 600.0f;
    p.filter.resonance = 0.7f;
    REQUIRE(augmentPatch(p, "DX7 bell"));
    REQUIRE(p.filter.cutoff_hz >= 14000.0f);
    REQUIRE(p.filter.resonance <= 0.2f);
}

TEST_CASE("augmenter FM-intent: already-FM osc[0] is left alone (no double-coerce)",
          "[augmenter][phase27]") {
    PatchStruct p = singleOscPatch(OscType::FM);
    p.osc[0].fm_ratio = 7.0f;
    p.osc[0].fm_depth = 0.6f;
    // Other oscs disabled; single-osc layering should fire, not FM coercion.
    REQUIRE(augmentPatch(p, "FM bell"));
    REQUIRE(p.osc[0].fm_ratio == 7.0f);
    REQUIRE(p.osc[0].fm_depth == 0.6f);
}

TEST_CASE("augmenter FM-intent: prompt without FM tokens does not coerce",
          "[augmenter][phase27]") {
    PatchStruct p = singleOscPatch(OscType::Sawtooth);
    REQUIRE(augmentPatch(p, "warm pad"));
    // Reese layering should have fired; osc[0] type stays Saw.
    REQUIRE(p.osc[0].type == OscType::Sawtooth);
}

TEST_CASE("augmenter FM-intent: 'format' / 'platform' do not false-positive on 'fm'",
          "[augmenter][phase27]") {
    PatchStruct p = singleOscPatch(OscType::Sawtooth);
    REQUIRE(augmentPatch(p, "warm pad platform"));
    REQUIRE(p.osc[0].type == OscType::Sawtooth);
}

// Phase 30 — cinematic pad coercion + cutoff guards + rationale invalidation.

TEST_CASE("augmenter cinematic: 'deep dark cinematic Kubrick pad' on single Saw → cinematic topology",
          "[augmenter][phase30]") {
    PatchStruct p = singleOscPatch(OscType::Sawtooth);
    p.filter.cutoff_hz = 400.0f; // LLM picked closed filter for "dark"
    REQUIRE(augmentPatch(p, "deep dark cinematic Kubrick pad"));
    // 3 audible oscs
    REQUIRE(p.osc[0].enabled);
    REQUIRE(p.osc[1].enabled);
    REQUIRE(p.osc[2].enabled);
    REQUIRE(p.osc[0].volume >= 0.15f);
    REQUIRE(p.osc[1].volume >= 0.15f);
    REQUIRE(p.osc[2].volume >= 0.15f);
    // panned wide
    REQUIRE(p.osc[0].pan < -0.3f);
    REQUIRE(p.osc[1].pan > 0.3f);
    // filter opened past bass territory
    REQUIRE(p.filter.cutoff_hz >= 1200.0f);
    // Phase 32 — POSITIVE env_mod (bloom OPENS on attack — cinematic reveal)
    REQUIRE(p.filter.env_mod > 0.0f);
    // two LFOs on DIFFERENT targets
    REQUIRE(p.lfo[0].target != LfoTarget::None);
    REQUIRE(p.lfo[1].target != LfoTarget::None);
    REQUIRE(p.lfo[0].target != p.lfo[1].target);
    REQUIRE(p.lfo[0].depth > 0.2f);
    REQUIRE(p.lfo[1].depth > 0.0f);
    // long envelope
    REQUIRE(p.amp_env.attack_s >= 2.0f);
    REQUIRE(p.amp_env.release_s >= 5.0f);
    // Phase 32 — cathedral reverb floor 0.30, capped at 0.45 (above is mud)
    REQUIRE(p.reverb.mix >= 0.3f);
    REQUIRE(p.reverb.mix <= 0.45f);
}

TEST_CASE("augmenter cinematic: 'spooky drone ever-changing soundscape' routes to cinematic",
          "[augmenter][phase30]") {
    PatchStruct p = singleOscPatch(OscType::Sawtooth);
    p.filter.cutoff_hz = 350.0f;
    REQUIRE(augmentPatch(p, "spooky drone ever-changing soundscape"));
    // Phase 32 — POSITIVE env_mod (cinematic reveal blooms OPEN on attack)
    REQUIRE(p.filter.env_mod > 0.0f);
    REQUIRE(p.lfo[0].rate_hz < 0.5f);
    REQUIRE(p.lfo[1].rate_hz < 0.5f);
}

TEST_CASE("augmenter cinematic: bass prompt does NOT trigger cinematic path",
          "[augmenter][phase30]") {
    PatchStruct p = singleOscPatch(OscType::Sawtooth);
    p.filter.cutoff_hz = 250.0f;
    REQUIRE(augmentPatch(p, "deep dark dubstep bass"));
    // Reese path — osc[1] detune partner at -10c, NOT panned wide
    REQUIRE(p.osc[1].detune_cents == -10.0f);
    REQUIRE(p.filter.env_mod >= 0.0f); // Reese doesn't set negative env_mod
}

TEST_CASE("augmenter Reese cutoff guard: closed filter is opened to 800+ Hz",
          "[augmenter][phase30]") {
    PatchStruct p = singleOscPatch(OscType::Sawtooth);
    p.filter.cutoff_hz = 200.0f; // would silence layers
    REQUIRE(augmentPatch(p, "thick bass"));
    REQUIRE(p.filter.cutoff_hz >= 800.0f);
}

TEST_CASE("augmenter Pad cutoff guard: closed filter is opened to 1500+ Hz",
          "[augmenter][phase30]") {
    PatchStruct p = singleOscPatch(OscType::Triangle);
    p.filter.cutoff_hz = 400.0f;
    REQUIRE(augmentPatch(p, "lush warm pad"));
    REQUIRE(p.filter.cutoff_hz >= 1500.0f);
}

TEST_CASE("augmenter rationale invalidation: stale LLM rationale cleared on mutation",
          "[augmenter][phase30]") {
    PatchStruct p = singleOscPatch(OscType::Sawtooth);
    // Simulate LLM having written a rationale that talks about a single saw.
    std::strncpy(p.rationale, "I chose a sawtooth oscillator with a closed filter.", sizeof(p.rationale) - 1);
    p.rationale[sizeof(p.rationale) - 1] = '\0';
    REQUIRE(augmentPatch(p, "huge cinematic kubrick pad"));
    // Augmenter must have cleared the lying rationale so the heuristic
    // regenerates from the post-augment topology.
    REQUIRE(p.rationale[0] == '\0');
    // Audit trail still present in augmenter_actions.
    REQUIRE(p.augmenter_actions[0] != '\0');
}

TEST_CASE("augmenter cinematic: already-good cinematic patch passes through",
          "[augmenter][phase30]") {
    // 3-osc patch with cinematic structure already in place.
    PatchStruct p = make_default_patch();
    for (auto& o : p.osc) {
        o.enabled = 1;
        o.volume = 0.5f;
        o.type = OscType::Sawtooth;
    }
    p.filter.cutoff_hz = 1800.0f;
    p.filter.env_mod = 0.35f; // Phase 32 — positive env_mod is now correct
    REQUIRE_FALSE(augmentPatch(p, "cinematic dark pad"));
    REQUIRE(p.augmenter_actions[0] == '\0');
}

// Phase 32 — augmenter-only DSP refresh on the cinematic pad recipe.
//
// The Phase 30 recipe shipped with three audible-but-thin choices that the
// Audio Engineer panel flagged: (a) NEGATIVE filter env_mod (filter closes
// on attack — wrong direction for the cinematic "reveal" effect),
// (b) plain sine sub at -24 (too low to be heard, too generic to be
// interesting), (c) symmetric ±7c detune (beating centers, patch reads as
// a single tone). The DSP changes flip env_mod positive, swap sub for
// inharmonic FM (2.73 ratio at low index — Vangelis monolith move), and
// shift to asymmetric -11c/+13c so the beating never resolves.

TEST_CASE("augmenter cinematic phase32: filter env_mod is POSITIVE (bloom opens on attack)",
          "[augmenter][phase32]") {
    PatchStruct p = singleOscPatch(OscType::Sawtooth);
    p.filter.cutoff_hz = 400.0f;
    REQUIRE(augmentPatch(p, "deep dark cinematic Kubrick pad"));
    REQUIRE(p.filter.env_mod > 0.0f);
    REQUIRE(p.filter.env_mod >= 0.3f); // meaningful depth, not a token positive
}

TEST_CASE("augmenter cinematic phase32: osc[2] is inharmonic FM, not plain sine",
          "[augmenter][phase32]") {
    PatchStruct p = singleOscPatch(OscType::Sawtooth);
    p.filter.cutoff_hz = 400.0f;
    REQUIRE(augmentPatch(p, "ominous cinematic drone"));
    REQUIRE(p.osc[2].type == OscType::FM);
    REQUIRE(p.osc[2].fm_ratio == 2.73f);
    REQUIRE(p.osc[2].fm_depth > 0.0f);
    REQUIRE(p.osc[2].semitone_offset == -12.0f); // not -24
    REQUIRE(p.osc[2].volume >= 0.15f);
}

TEST_CASE("augmenter cinematic phase32: detune is asymmetric (beating never centers)",
          "[augmenter][phase32]") {
    PatchStruct p = singleOscPatch(OscType::Sawtooth);
    p.filter.cutoff_hz = 400.0f;
    REQUIRE(augmentPatch(p, "evolving cinematic pad"));
    // osc[0] and osc[1] detune are NOT mirror images.
    REQUIRE(p.osc[0].detune_cents != -p.osc[1].detune_cents);
    // Both nontrivial detunes (>= 10c each side for the Vangelis width).
    REQUIRE(std::abs(p.osc[0].detune_cents) >= 10.0f);
    REQUIRE(std::abs(p.osc[1].detune_cents) >= 10.0f);
}

TEST_CASE("augmenter cinematic phase32: reverb mix is capped at 0.45 (no mud)",
          "[augmenter][phase32]") {
    PatchStruct p = singleOscPatch(OscType::Sawtooth);
    p.filter.cutoff_hz = 400.0f;
    // Simulate the LLM picking an over-wet reverb.
    p.reverb.mix = 0.85f;
    REQUIRE(augmentPatch(p, "vangelis cinematic pad"));
    REQUIRE(p.reverb.mix <= 0.45f);
    REQUIRE(p.reverb.mix >= 0.30f);
}

TEST_CASE("augmenter cinematic phase32: filter drive bumped to >= 0.35 (Vangelis glue)",
          "[augmenter][phase32]") {
    PatchStruct p = singleOscPatch(OscType::Sawtooth);
    p.filter.cutoff_hz = 400.0f;
    p.filter.drive = 0.05f; // low LLM value
    REQUIRE(augmentPatch(p, "cinematic kubrick pad"));
    REQUIRE(p.filter.drive >= 0.35f);
}

TEST_CASE("augmenter cinematic phase32: filter env attack shortened to ~1.8s for bloom",
          "[augmenter][phase32]") {
    PatchStruct p = singleOscPatch(OscType::Sawtooth);
    p.filter.cutoff_hz = 400.0f;
    p.filter_env.attack_s = 0.1f;
    REQUIRE(augmentPatch(p, "atmospheric cinematic pad"));
    REQUIRE(p.filter_env.attack_s >= 1.8f);
    REQUIRE(p.filter_env.decay_s >= 5.0f);
}

TEST_CASE("augmenter cinematic phase32: wavetable mainosc retargets LFO1 to WavetablePos",
          "[augmenter][phase32]") {
    PatchStruct p = singleOscPatch(OscType::Wavetable);
    p.filter.cutoff_hz = 400.0f;
    REQUIRE(augmentPatch(p, "ever-changing cinematic soundscape"));
    REQUIRE(p.lfo[0].target == LfoTarget::WavetablePos);
    REQUIRE(p.lfo[1].target == LfoTarget::Pitch);
}

// Phase E (#265) — new DSP modules participate in the cinematic recipe.
//
// The Audio Engineer panel flagged that 80% of Vangelis lushness lives in
// chorus + tube saturation pre-filter, not in reverb. The cinematic
// augmenter recipe now sets:
//   • tubesat.drive >= 0.30 (harmonic glue)
//   • chorus.mix >= 0.40 (Juno-ensemble)
//   • reverb_send_hpf_hz == 100 (cathedral tail loses sub energy)

TEST_CASE("augmenter cinematic phaseE: tubesat.drive set to >= 0.30 for harmonic glue",
          "[augmenter][phaseE]") {
    PatchStruct p = singleOscPatch(OscType::Sawtooth);
    p.filter.cutoff_hz = 400.0f;
    p.tubesat.drive = 0.0f;
    REQUIRE(augmentPatch(p, "deep dark cinematic Kubrick pad"));
    REQUIRE(p.tubesat.drive >= 0.30f);
    REQUIRE(p.tubesat.mix > 0.0f);
}

TEST_CASE("augmenter cinematic phaseE: chorus.mix set to >= 0.40 for Juno ensemble",
          "[augmenter][phaseE]") {
    PatchStruct p = singleOscPatch(OscType::Sawtooth);
    p.filter.cutoff_hz = 400.0f;
    p.chorus.mix = 0.0f;
    REQUIRE(augmentPatch(p, "vangelis cinematic pad"));
    REQUIRE(p.chorus.mix >= 0.40f);
    REQUIRE(p.chorus.rate_hz > 0.0f);
    REQUIRE(p.chorus.depth > 0.0f);
}

TEST_CASE("augmenter cinematic phaseE: reverb_send_hpf_hz set to 100 (no sub smear)",
          "[augmenter][phaseE]") {
    PatchStruct p = singleOscPatch(OscType::Sawtooth);
    p.filter.cutoff_hz = 400.0f;
    p.reverb_send_hpf_hz = 0.0f;
    REQUIRE(augmentPatch(p, "ominous cinematic drone"));
    REQUIRE(p.reverb_send_hpf_hz > 0.0f);
    REQUIRE(p.reverb_send_hpf_hz >= 60.0f);
    REQUIRE(p.reverb_send_hpf_hz <= 200.0f);
}
