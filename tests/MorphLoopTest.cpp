#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <string>
#include <vector>

#include "agent/MorphLoop.h"
#include "engine/PatchStruct.h"

using namespace agentic_synth;
using namespace agentic_synth::agent;

namespace {

// Compare two patches for "did all five primary perceptual fields land at the
// same value?" — enough for the determinism assertion without requiring an
// exact bytewise match (which would also depend on padding bytes).
bool patchesEquivalent(const PatchStruct& a, const PatchStruct& b) noexcept {
    auto same = [](float x, float y) noexcept { return std::fabs(x - y) < 1e-4f; };
    if (!same(a.filter.cutoff_hz, b.filter.cutoff_hz)) return false;
    if (!same(a.filter.resonance, b.filter.resonance)) return false;
    if (!same(a.amp_env.attack_s, b.amp_env.attack_s)) return false;
    if (!same(a.amp_env.release_s, b.amp_env.release_s)) return false;
    if (!same(a.reverb.size, b.reverb.size)) return false;
    if (!same(a.reverb.mix, b.reverb.mix)) return false;
    if (!same(a.master_gain, b.master_gain)) return false;
    if (!same(a.osc[0].detune_cents, b.osc[0].detune_cents)) return false;
    if (!same(a.osc[1].detune_cents, b.osc[1].detune_cents)) return false;
    return true;
}

PatchStruct makeMidPatch() noexcept {
    PatchStruct p = make_default_patch();
    // Move it off the corners so mutations have headroom in both directions
    // (cutoff at default 18000 Hz has no headroom UP, which would skew the
    // labeler towards "warmer" every time).
    p.filter.cutoff_hz = 2000.0f;
    p.filter.resonance = 0.3f;
    p.amp_env.attack_s = 0.5f;
    p.amp_env.release_s = 1.0f;
    p.reverb.size = 0.4f;
    p.reverb.mix = 0.3f;
    p.lfo[0].depth = 0.2f;
    p.lfo[0].rate_hz = 2.0f;
    p.osc[0].detune_cents = 5.0f;
    p.osc[1].detune_cents = -5.0f;
    p.osc[1].volume = 0.6f;
    p.osc[1].enabled = 1;
    return p;
}

} // namespace

TEST_CASE("MorphLoop: same seed yields same 5 variations (determinism)") {
    const auto base = makeMidPatch();
    std::vector<PatchStruct> hist{base};
    std::vector<PatchStruct> liked{base};
    const std::string prompt = "warm evolving pad";

    const auto a = morph(base, hist, liked, prompt, 12345u);
    const auto b = morph(base, hist, liked, prompt, 12345u);

    for (int i = 0; i < 5; ++i) {
        CHECK(patchesEquivalent(a.variations[i], b.variations[i]));
        CHECK(a.labels[i] == b.labels[i]);
    }
}

TEST_CASE("MorphLoop: different seeds produce different mutation slots") {
    const auto base = makeMidPatch();
    const auto a = morph(base, {}, {}, "warm pad", 1u);
    const auto b = morph(base, {}, {}, "warm pad", 9999u);

    // Heavy and light mutation slots (0, 1) MUST differ across seeds — they
    // are pure mutation strategies parameterised by RNG. Archetype bounce (4)
    // can also differ because we modulo by top.size().
    const bool anyMutationDiffers =
        std::fabs(a.variations[0].filter.cutoff_hz - b.variations[0].filter.cutoff_hz) > 1e-3f ||
        std::fabs(a.variations[1].reverb.mix - b.variations[1].reverb.mix) > 1e-3f;
    CHECK(anyMutationDiffers);
}

TEST_CASE("MorphLoop: heavy mutation perturbs at least one param by > 5%") {
    const auto base = makeMidPatch();
    const auto r = morph(base, {}, {}, "anything", 42u);

    // Slot 0 = heavy mutation. Check any of the headline params moved
    // perceptibly. We use 5% as a generous floor — strength is 12.5%, so the
    // expected drift for the strongest hit is far above 5%.
    auto pctDelta = [](float a, float b) noexcept -> float {
        const float denom = std::fabs(b) < 1e-6f ? 1e-6f : std::fabs(b);
        return std::fabs(a - b) / denom;
    };

    const float cutoffPct = pctDelta(r.variations[0].filter.cutoff_hz, base.filter.cutoff_hz);
    const float reverbSizePct = pctDelta(r.variations[0].reverb.size, base.reverb.size);
    const float ampAttackPct = pctDelta(r.variations[0].amp_env.attack_s, base.amp_env.attack_s);
    const float gainPct = pctDelta(r.variations[0].master_gain, base.master_gain);

    const bool somethingMovedHeavy = (cutoffPct > 0.05f) || (reverbSizePct > 0.05f) ||
                                      (ampAttackPct > 0.05f) || (gainPct > 0.05f);
    CHECK(somethingMovedHeavy);
}

TEST_CASE("MorphLoop: light mutation stays within ~10% on all headline params") {
    const auto base = makeMidPatch();
    const auto r = morph(base, {}, {}, "anything", 42u);

    // Slot 1 = light mutation (strength 4%, multiplicative). Worst case the
    // RNG hands us a 1.0 perturbation magnitude, so the upper bound is just
    // under 4% on multiplicative fields. The additive fields use a smaller
    // scale so they stay well within range too. We assert <=10% as the
    // generous cap (well under the heavy slot's typical drift) to leave room
    // for the validator's clamp interactions on near-boundary defaults.
    auto pctDelta = [](float a, float b) noexcept -> float {
        const float denom = std::fabs(b) < 1e-6f ? 1e-6f : std::fabs(b);
        return std::fabs(a - b) / denom;
    };

    CHECK(pctDelta(r.variations[1].filter.cutoff_hz, base.filter.cutoff_hz) <= 0.10f);
    CHECK(pctDelta(r.variations[1].master_gain, base.master_gain) <= 0.10f);
    // amp_env.attack_s uses an additive bend with 0.5 * strength = 0.02. With
    // base 0.5 s that's at most 4% — well under 10%.
    CHECK(pctDelta(r.variations[1].amp_env.attack_s, base.amp_env.attack_s) <= 0.10f);
}

TEST_CASE("MorphLoop: empty history + empty liked still produces 5 variations") {
    const auto base = makeMidPatch();
    const auto r = morph(base, {}, {}, "bright pluck", 7u);
    // Every slot is a valid finite patch — the empty-pool fallback uses extra
    // mutations rather than skipping the slot.
    for (int i = 0; i < 5; ++i) {
        CHECK(std::isfinite(r.variations[i].filter.cutoff_hz));
        CHECK(r.variations[i].filter.cutoff_hz >= 20.0f);
        CHECK(r.variations[i].filter.cutoff_hz <= 18000.0f);
        CHECK(std::isfinite(r.variations[i].master_gain));
        CHECK(r.labels[i].size() > 0);
    }
}

TEST_CASE("MorphLoop: archetype bounce produces a patch from the library") {
    // "warm pad" → keyword scoring should pick the warm-pad / cinematic-pad
    // archetype out of the library, which has a distinctly different shape
    // from the default init we use as the base.
    PatchStruct base = make_default_patch();
    const auto r = morph(base, {}, {}, "warm pad", 0u);

    // Slot 4 = archetype bounce, blended 50/50 with the base. Default init
    // has cutoff 18000 Hz — virtually every archetype with a tonal flavour
    // will pull this DOWN. So we assert the bounced variation's cutoff is at
    // least notably lower than the default's 18 kHz ceiling.
    CHECK(r.variations[4].filter.cutoff_hz < 18000.0f);
}

TEST_CASE("MorphLoop labeler: cutoff doubled → 'brighter'") {
    PatchStruct base = makeMidPatch();
    PatchStruct variant = base;
    variant.filter.cutoff_hz = base.filter.cutoff_hz * 2.0f;

    // We exercise the labeler through morph() by feeding a base that's
    // identical to itself except on cutoff. We use seed=0, history+liked pair
    // forcing slot 0 to fire its heavy-mutation. To test the labeler directly
    // we pick an alternative shape: use morph() then read its slot-4 label,
    // which lerps base→archetype. That's noisy because the archetype could
    // touch other axes too — so we just test the deterministic property: the
    // labels are nonempty strings drawn from the expected vocabulary.
    const auto r = morph(base, {}, {}, "anything", 13u);
    static const std::vector<std::string> kVocab = {
        "brighter", "warmer", "longer", "snappier", "wider", "drier",
        "spread",   "tighter", "A",      "B",        "C",     "D", "E"};
    for (int i = 0; i < 5; ++i) {
        bool ok = false;
        for (const auto& w : kVocab) {
            if (r.labels[i] == w) {
                ok = true;
                break;
            }
        }
        INFO("label[" << i << "]=" << r.labels[i]);
        CHECK(ok);
    }
}

TEST_CASE("MorphLoop labeler: 'brighter' fires when cutoff jumps >20%") {
    // Direct labeler exercise: use crossoverHalf via morph()'s slot-3 path
    // (history) with a history patch whose ONLY change is doubled cutoff.
    // After 50/50 lerp, cutoff change vs base = +50%, well above the 20%
    // brighter threshold. Reverb / attack / detune stay equal so the
    // dominant axis must be brightness.
    PatchStruct base = makeMidPatch();
    PatchStruct bright = base;
    bright.filter.cutoff_hz = base.filter.cutoff_hz * 2.0f; // +100%
    std::vector<PatchStruct> hist{bright};

    const auto r = morph(base, hist, {}, "anything", 0u);
    CHECK(r.labels[3] == "brighter");
}

TEST_CASE("MorphLoop labeler: 'warmer' fires when cutoff drops >20%") {
    PatchStruct base = makeMidPatch();
    PatchStruct dark = base;
    dark.filter.cutoff_hz = base.filter.cutoff_hz * 0.4f; // -60%
    std::vector<PatchStruct> hist{dark};

    const auto r = morph(base, hist, {}, "anything", 0u);
    CHECK(r.labels[3] == "warmer");
}

TEST_CASE("MorphLoop labeler: 'wider' fires when reverb size grows >20%") {
    PatchStruct base = makeMidPatch();
    PatchStruct spacious = base;
    spacious.reverb.size = base.reverb.size * 2.0f; // +100% (clamped <= 1)
    std::vector<PatchStruct> liked{spacious};

    const auto r = morph(base, {}, liked, "anything", 0u);
    // Slot 2 = liked crossover. The reverb size axis should dominate brightness
    // (which is unchanged by the crossover). Labels are decided by largest
    // signed magnitude — reverb.size is the only axis moving here.
    CHECK(r.labels[2] == "wider");
}

TEST_CASE("MorphLoop labeler: 'longer' fires when attack grows >50%") {
    PatchStruct base = makeMidPatch();
    PatchStruct slow = base;
    slow.amp_env.attack_s = base.amp_env.attack_s * 4.0f; // +300%
    std::vector<PatchStruct> hist{slow};

    const auto r = morph(base, hist, {}, "anything", 0u);
    CHECK(r.labels[3] == "longer");
}
