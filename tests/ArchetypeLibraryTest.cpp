#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>

#include "mapper/ArchetypeLibrary.h"
#include "mapper/ArchetypeRetriever.h"

using agentic_synth::mapper::Archetype;
using agentic_synth::mapper::ArchetypeLibrary;
using agentic_synth::mapper::ArchetypeRetriever;

// ─── Library load / shape ────────────────────────────────────────────────────

TEST_CASE("ArchetypeLibrary: all archetypes parse successfully") {
    const auto& lib = ArchetypeLibrary::all();
    // Phase 34a ships 15 hand-curated archetypes.
    REQUIRE(lib.size() == 15);
}

TEST_CASE("ArchetypeLibrary: every archetype has a non-empty name and at least one tag") {
    const auto& lib = ArchetypeLibrary::all();
    REQUIRE_FALSE(lib.empty());
    for (const auto& a : lib) {
        INFO("archetype: " << a.name);
        CHECK_FALSE(a.name.empty());
        CHECK_FALSE(a.tags.empty());
        for (const auto& t : a.tags)
            CHECK_FALSE(t.empty());
    }
}

TEST_CASE("ArchetypeLibrary: every archetype embeds a valid PatchStruct") {
    const auto& lib = ArchetypeLibrary::all();
    for (const auto& a : lib) {
        INFO("archetype: " << a.name);
        // version field gets stamped by parse_patch_json/validate; presence
        // of the schema version is the strongest single signal that the
        // JSON literal parsed correctly.
        CHECK(a.patch.version == agentic_synth::kPatchStructVersion);
        // voice_count must be in 1..16 (validated by parse_patch_json).
        CHECK(a.patch.voice_count >= 1);
        CHECK(a.patch.voice_count <= 16);
        // Each archetype is expected to have at least one audible oscillator
        // — even default_init keeps osc[0] saw enabled.
        bool anyAudible = false;
        for (const auto& o : a.patch.osc)
            if (o.enabled && o.volume > 0.0f)
                anyAudible = true;
        CHECK(anyAudible);
    }
}

TEST_CASE("ArchetypeLibrary: default_init is present") {
    REQUIRE(ArchetypeLibrary::byName("default_init") != nullptr);
}

// ─── Retriever scoring ──────────────────────────────────────────────────────

TEST_CASE("ArchetypeRetriever: 'deep dark cinematic kubrick pad' → cinematic_kubrick_pad") {
    const auto* a = ArchetypeRetriever::retrieve("deep dark cinematic kubrick pad");
    REQUIRE(a != nullptr);
    CHECK(a->name == "cinematic_kubrick_pad");
}

TEST_CASE("ArchetypeRetriever: 'vangelis blade runner' → vangelis_blade_runner_pad") {
    const auto* a = ArchetypeRetriever::retrieve("vangelis blade runner");
    REQUIRE(a != nullptr);
    CHECK(a->name == "vangelis_blade_runner_pad");
}

TEST_CASE("ArchetypeRetriever: 'dubstep wobble bass' → reese_dubstep_bass") {
    const auto* a = ArchetypeRetriever::retrieve("dubstep wobble bass");
    REQUIRE(a != nullptr);
    CHECK(a->name == "reese_dubstep_bass");
}

TEST_CASE("ArchetypeRetriever: 'DX7 tine electric piano' → dx7_tine_ep") {
    const auto* a = ArchetypeRetriever::retrieve("DX7 tine electric piano");
    REQUIRE(a != nullptr);
    CHECK(a->name == "dx7_tine_ep");
}

TEST_CASE("ArchetypeRetriever: 'hello world' (no tag match) → default_init") {
    const auto* a = ArchetypeRetriever::retrieve("hello world");
    REQUIRE(a != nullptr);
    CHECK(a->name == "default_init");
}

TEST_CASE("ArchetypeRetriever: empty prompt → default_init") {
    const auto* a = ArchetypeRetriever::retrieve("");
    REQUIRE(a != nullptr);
    CHECK(a->name == "default_init");
}

// ─── retrieveTopN ───────────────────────────────────────────────────────────

TEST_CASE("ArchetypeRetriever::retrieveTopN: returns N entries by score (cinematic prompt)") {
    auto top = ArchetypeRetriever::retrieveTopN("vangelis cinematic kubrick pad", 3);
    REQUIRE(top.size() == 3);
    // Top match should be one of the two cinematic-tagged pads.
    const std::string firstName = top[0]->name;
    CHECK((firstName == "cinematic_kubrick_pad" || firstName == "vangelis_blade_runner_pad"));
    // n=1 reduces to a single-best retrieval (must match retrieve()).
    auto top1 = ArchetypeRetriever::retrieveTopN("vangelis cinematic kubrick pad", 1);
    REQUIRE(top1.size() == 1);
    const auto* single = ArchetypeRetriever::retrieve("vangelis cinematic kubrick pad");
    REQUIRE(single != nullptr);
    CHECK(top1[0]->name == single->name);
}
