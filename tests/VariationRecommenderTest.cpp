#include "agent/VariationRecommender.h"
#include "engine/PatchStruct.h"
#include <catch2/catch_test_macros.hpp>

using namespace agentic_synth;
using namespace agentic_synth::agent;

TEST_CASE("VariationRecommender: suggest returns 3 variations", "[variation-recommender]") {
    VariationRecommender recommender;
    PatchStruct current = make_default_patch();

    auto suggestions = recommender.suggest(current);
    REQUIRE(suggestions.size() == 3);
}

TEST_CASE("VariationRecommender: each suggestion has non-empty label and rationale", "[variation-recommender]") {
    VariationRecommender recommender;
    PatchStruct current = make_default_patch();

    auto suggestions = recommender.suggest(current);
    for (const auto& s : suggestions) {
        CHECK_FALSE(s.label.empty());
        CHECK_FALSE(s.rationale.empty());
    }
}

TEST_CASE("VariationRecommender: at least one suggestion differs from current patch", "[variation-recommender]") {
    VariationRecommender recommender;
    PatchStruct current = make_default_patch();

    auto suggestions = recommender.suggest(current);
    int differing = 0;
    for (const auto& s : suggestions) {
        if (s.patch.filter.cutoff_hz != current.filter.cutoff_hz || s.patch.osc[0].volume != current.osc[0].volume ||
            s.patch.amp_env.attack_s != current.amp_env.attack_s) {
            ++differing;
        }
    }
    REQUIRE(differing > 0);
}
