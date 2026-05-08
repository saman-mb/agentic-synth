#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "engine/VariationEngine.h"

using namespace agentic_synth;
using namespace agentic_synth::engine;

namespace {
// Measure perceptual distance: sum of squared differences on key parameters.
float patchDistance(const PatchStruct& a, const PatchStruct& b) noexcept {
    auto sq = [](float x) { return x * x; };
    return sq(a.filter.cutoff_hz / 18000.0f - b.filter.cutoff_hz / 18000.0f) +
           sq(a.filter.resonance   - b.filter.resonance) +
           sq(a.amp_env.attack_s / 10.0f - b.amp_env.attack_s / 10.0f) +
           sq(a.amp_env.sustain    - b.amp_env.sustain) +
           sq(a.reverb.mix         - b.reverb.mix) +
           sq(a.lfo[0].depth       - b.lfo[0].depth) +
           sq(a.master_gain        - b.master_gain);
}
} // namespace

TEST_CASE("temperatureSweep returns kVariationCount patches") {
    VariationEngine eng;
    const auto base = make_default_patch();
    const auto vars = eng.temperatureSweep(base);
    CHECK(static_cast<int>(vars.size()) == VariationEngine::kVariationCount);
}

TEST_CASE("temperatureSweep variations are distinct from base") {
    VariationEngine eng;
    const auto base = make_default_patch();
    const auto vars = eng.temperatureSweep(base);
    for (const auto& v : vars) {
        CHECK(patchDistance(base, v) > 0.001f);
    }
}

TEST_CASE("temperatureSweep variations increase in divergence from base") {
    VariationEngine eng;
    const auto base = make_default_patch();
    const auto vars = eng.temperatureSweep(base);
    float prev = 0.0f;
    for (const auto& v : vars) {
        float d = patchDistance(base, v);
        CHECK(d >= prev - 1e-6f); // monotonically increasing
        prev = d;
    }
}

TEST_CASE("perturbation returns kVariationCount distinct patches") {
    VariationEngine eng;
    const auto base = make_default_patch();
    const auto vars = eng.perturbation(base);
    CHECK(static_cast<int>(vars.size()) == VariationEngine::kVariationCount);
    for (const auto& v : vars) {
        CHECK(patchDistance(base, v) > 0.0f);
    }
}

TEST_CASE("perturbation patches are distinct from each other") {
    VariationEngine eng;
    const auto base = make_default_patch();
    const auto vars = eng.perturbation(base);
    for (int i = 0; i < VariationEngine::kVariationCount; ++i) {
        for (int j = i + 1; j < VariationEngine::kVariationCount; ++j) {
            CHECK(patchDistance(vars[i], vars[j]) > 0.0f);
        }
    }
}

TEST_CASE("morph at t=0.2 is closer to base than t=0.8") {
    VariationEngine eng;
    const auto base = make_default_patch();
    PatchStruct target = make_default_patch();
    target.filter.cutoff_hz = 500.0f;
    target.reverb.mix = 0.9f;

    const auto vars = eng.morph(base, target);
    // vars[0] = t=0.2, vars[4] = t=1.0 (= target)
    CHECK(patchDistance(base, vars[0]) < patchDistance(base, vars[4]));
}

TEST_CASE("morph at t=1.0 equals target") {
    VariationEngine eng;
    const auto base = make_default_patch();
    PatchStruct target = make_default_patch();
    target.filter.cutoff_hz = 300.0f;
    target.reverb.mix = 0.8f;
    target.lfo[0].depth = 0.7f;

    const auto vars = eng.morph(base, target);
    // vars[4] is at t=1.0
    CHECK(vars[4].filter.cutoff_hz == Catch::Approx(target.filter.cutoff_hz).epsilon(0.01f));
    CHECK(vars[4].reverb.mix == Catch::Approx(target.reverb.mix).epsilon(0.01f));
    CHECK(vars[4].lfo[0].depth == Catch::Approx(target.lfo[0].depth).epsilon(0.01f));
}

TEST_CASE("generateVariations returns 5 patches") {
    VariationEngine eng;
    const auto base = make_default_patch();
    const auto vars = eng.generateVariations(base);
    CHECK(static_cast<int>(vars.size()) == 5);
}

TEST_CASE("generateVariations: all 5 variations differ from base") {
    VariationEngine eng;
    const auto base = make_default_patch();
    const auto vars = eng.generateVariations(base);
    for (const auto& v : vars) {
        CHECK(patchDistance(base, v) > 0.0f);
    }
}

TEST_CASE("generateVariations: all 5 variations are mutually distinct") {
    VariationEngine eng;
    const auto base = make_default_patch();
    const auto vars = eng.generateVariations(base);
    for (int i = 0; i < 5; ++i) {
        for (int j = i + 1; j < 5; ++j) {
            // At least one key parameter must differ.
            CHECK(patchDistance(vars[i], vars[j]) > 0.0f);
        }
    }
}

TEST_CASE("generateVariations: all patches have valid (finite) parameters") {
    VariationEngine eng;
    const auto base = make_default_patch();
    const auto vars = eng.generateVariations(base);
    for (const auto& v : vars) {
        CHECK(std::isfinite(v.filter.cutoff_hz));
        CHECK(std::isfinite(v.reverb.mix));
        CHECK(std::isfinite(v.amp_env.attack_s));
        CHECK(std::isfinite(v.master_gain));
        CHECK(v.filter.cutoff_hz >= 20.0f);
        CHECK(v.filter.cutoff_hz <= 18000.0f);
    }
}

TEST_CASE("generateVariationsWithSeed: same seed yields identical results") {
    VariationEngine eng;
    const auto base = make_default_patch();
    const auto v1 = eng.generateVariationsWithSeed(base, 123);
    const auto v2 = eng.generateVariationsWithSeed(base, 123);
    for (int i = 0; i < VariationEngine::kVariationCount; ++i) {
        CHECK(v1[i].filter.cutoff_hz == Catch::Approx(v2[i].filter.cutoff_hz));
        CHECK(v1[i].reverb.mix       == Catch::Approx(v2[i].reverb.mix));
        CHECK(v1[i].amp_env.attack_s == Catch::Approx(v2[i].amp_env.attack_s));
        CHECK(v1[i].master_gain      == Catch::Approx(v2[i].master_gain));
    }
}

TEST_CASE("generateVariationsWithSeed: different seeds produce different perturbations") {
    VariationEngine eng;
    const auto base = make_default_patch();
    const auto v1 = eng.generateVariationsWithSeed(base, 42);
    const auto v2 = eng.generateVariationsWithSeed(base, 9999);
    // Perturbation slots are indices 2 and 3; at least one must differ.
    bool anyDiff = (v1[2].filter.cutoff_hz != v2[2].filter.cutoff_hz) ||
                   (v1[3].reverb.mix       != v2[3].reverb.mix);
    CHECK(anyDiff);
}

TEST_CASE("generateVariationsWithSeed: all 5 variations are valid") {
    VariationEngine eng;
    const auto base = make_default_patch();
    const auto vars = eng.generateVariationsWithSeed(base, 777);
    for (const auto& v : vars) {
        CHECK(std::isfinite(v.filter.cutoff_hz));
        CHECK(std::isfinite(v.master_gain));
        CHECK(v.filter.cutoff_hz >= 20.0f);
        CHECK(v.filter.cutoff_hz <= 18000.0f);
    }
}
