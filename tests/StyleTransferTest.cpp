#include "engine/StyleTransfer.h"
#include "engine/PatchStruct.h"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace agentic_synth::engine;
using namespace agentic_synth;
using Catch::Approx;

TEST_CASE("StyleTransfer: extract returns profile in [0,1] range", "[style-transfer]") {
    PatchStruct ref = make_default_patch();
    ref.filter.cutoff_hz = 500.0f;
    ref.filter.resonance = 0.7f;
    ref.amp_env.attack_s = 1.0f;
    ref.lfo[0].depth = 0.6f;

    auto profile = StyleTransfer::extract(ref);

    CHECK(profile.filterCharacter >= 0.0f);
    CHECK(profile.filterCharacter <= 1.0f);
    CHECK(profile.envelopeShape >= 0.0f);
    CHECK(profile.envelopeShape <= 1.0f);
    CHECK(profile.modulationFeel >= 0.0f);
    CHECK(profile.modulationFeel <= 1.0f);
    CHECK(profile.brightness >= 0.0f);
    CHECK(profile.brightness <= 1.0f);
}

TEST_CASE("StyleTransfer: bright reference produces higher brightness than dark", "[style-transfer]") {
    PatchStruct bright = make_default_patch();
    bright.filter.cutoff_hz = 15000.0f;

    PatchStruct dark = make_default_patch();
    dark.filter.cutoff_hz = 100.0f;

    auto brightProfile = StyleTransfer::extract(bright);
    auto darkProfile = StyleTransfer::extract(dark);

    CHECK(brightProfile.brightness > darkProfile.brightness);
}

TEST_CASE("StyleTransfer: apply with blend=0 leaves target unchanged", "[style-transfer]") {
    PatchStruct ref = make_default_patch();
    ref.filter.cutoff_hz = 100.0f;

    PatchStruct target = make_default_patch();
    target.filter.cutoff_hz = 8000.0f;

    auto style = StyleTransfer::extract(ref);
    PatchStruct result = StyleTransfer::apply(target, style, 0.0f);

    // blend=0 → result should equal target
    CHECK(result.filter.cutoff_hz == Approx(target.filter.cutoff_hz).margin(1.0f));
}

TEST_CASE("StyleTransfer: apply with blend=1 shifts result toward reference style", "[style-transfer]") {
    PatchStruct ref = make_default_patch();
    ref.filter.cutoff_hz = 200.0f; // dark
    ref.amp_env.attack_s = 2.0f;   // slow

    PatchStruct target = make_default_patch();
    target.filter.cutoff_hz = 10000.0f; // bright
    target.amp_env.attack_s = 0.005f;   // fast

    auto style = StyleTransfer::extract(ref);
    PatchStruct result = StyleTransfer::apply(target, style, 1.0f);

    // Full blend: result should differ from unblended target
    bool changed = (result.filter.cutoff_hz != target.filter.cutoff_hz) ||
                   (result.amp_env.attack_s != target.amp_env.attack_s);
    CHECK(changed);
}

TEST_CASE("StyleTransfer: transfer convenience wrapper produces same result as extract+apply", "[style-transfer]") {
    PatchStruct ref = make_default_patch();
    ref.filter.cutoff_hz = 300.0f;

    PatchStruct target = make_default_patch();
    target.filter.cutoff_hz = 5000.0f;

    auto style = StyleTransfer::extract(ref);
    PatchStruct viaApply = StyleTransfer::apply(target, style, 0.5f);
    PatchStruct viaTransfer = StyleTransfer::transfer(ref, target, 0.5f);

    CHECK(viaApply.filter.cutoff_hz == Approx(viaTransfer.filter.cutoff_hz).margin(0.1f));
}
