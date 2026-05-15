// Phase 22 — refinement-context detection.
//
// Verifies the free static PromptHandler::isRelativePrompt classifier that
// gates whether a new user submission is a fresh patch ("warm pad") or a
// nudge against the prior patch ("darker", "more wobble"). The classifier
// runs before the prompt-enhancer + LLM call so the WebUiComponent worker
// can decide whether to skip enhancement and forward the previous patch.
//
// The detector is keyword-driven, case-insensitive, with word-boundary
// guards on single-token keywords (so "software" never matches "soft"-er).
// Multi-token fragments like "more " match by substring — these encode their
// own boundaries.

#include <catch2/catch_test_macros.hpp>

#include <string>

#include "agent/PromptHandler.h"

using agentic_synth::agent::PromptHandler;

TEST_CASE("isRelativePrompt: comparative single words", "[refinement][phase22]") {
    REQUIRE(PromptHandler::isRelativePrompt("darker"));
    REQUIRE(PromptHandler::isRelativePrompt("brighter"));
    REQUIRE(PromptHandler::isRelativePrompt("weirder"));
    REQUIRE(PromptHandler::isRelativePrompt("thicker"));
    REQUIRE(PromptHandler::isRelativePrompt("Punchier"));
    REQUIRE(PromptHandler::isRelativePrompt("DARKER AND MEANER"));
}

TEST_CASE("isRelativePrompt: 'more X' / 'less X' phrases", "[refinement][phase22]") {
    REQUIRE(PromptHandler::isRelativePrompt("more wobble"));
    REQUIRE(PromptHandler::isRelativePrompt("less of the reverb please"));
    REQUIRE(PromptHandler::isRelativePrompt("with more drive"));
    REQUIRE(PromptHandler::isRelativePrompt("a bit slower"));
    REQUIRE(PromptHandler::isRelativePrompt("slightly thicker low end"));
}

TEST_CASE("isRelativePrompt: fresh-patch prompts return false", "[refinement][phase22]") {
    REQUIRE_FALSE(PromptHandler::isRelativePrompt("warm pad"));
    REQUIRE_FALSE(PromptHandler::isRelativePrompt("dubstep bass"));
    REQUIRE_FALSE(PromptHandler::isRelativePrompt("happy synth"));
    REQUIRE_FALSE(PromptHandler::isRelativePrompt("acid lead"));
    REQUIRE_FALSE(PromptHandler::isRelativePrompt(""));
}

TEST_CASE("isRelativePrompt: edge case — 'more dubstep' is still refinement",
          "[refinement][phase22]") {
    // The user is saying "push it further in the dubstep direction" — the
    // word "more" is a directional cue regardless of what follows.
    REQUIRE(PromptHandler::isRelativePrompt("more dubstep"));
}

TEST_CASE("isRelativePrompt: edge case — 'wider snare' is refinement",
          "[refinement][phase22]") {
    // "wider" is in the §5.3 dictionary; the noun that follows is the
    // target dimension, not a category change.
    REQUIRE(PromptHandler::isRelativePrompt("wider snare"));
}

TEST_CASE("isRelativePrompt: edge case — descriptive 'soft' is NOT refinement",
          "[refinement][phase22]") {
    // Bare "soft" is a primary descriptor for the patch (like "warm" or
    // "lush"), not a comparative. Only "softer" / "softer than" should
    // trigger refinement mode.
    REQUIRE_FALSE(PromptHandler::isRelativePrompt("soft snare"));
    REQUIRE_FALSE(PromptHandler::isRelativePrompt("a soft warm pad"));
}

TEST_CASE("isRelativePrompt: word boundaries — 'software' must not match 'soft'",
          "[refinement][phase22]") {
    // Smoke test for the word-boundary guard: substring matching alone would
    // false-positive on words that happen to contain a comparative root.
    REQUIRE_FALSE(PromptHandler::isRelativePrompt("software synth"));
    REQUIRE_FALSE(PromptHandler::isRelativePrompt("hardier sound design"));
}

TEST_CASE("isRelativePrompt: 'darker and more ominous' (the canonical bug)",
          "[refinement][phase22]") {
    // The exact case Phase 22 was opened for: user types "darker and more
    // ominous" after generating a wobble bass. Pre-fix this regenerated from
    // scratch and stripped the wobble. Post-fix the classifier returns true,
    // PromptHandler wraps in §5.3 refinement mode, LLM nudges the existing
    // patch in the named direction, wobble is preserved.
    REQUIRE(PromptHandler::isRelativePrompt("darker and more ominous"));
    REQUIRE(PromptHandler::isRelativePrompt("make it evil"));
    REQUIRE(PromptHandler::isRelativePrompt("a bit weirder"));
}
