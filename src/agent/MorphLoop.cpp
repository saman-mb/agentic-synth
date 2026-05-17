#include "agent/MorphLoop.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "engine/PatchValidator.h"
#include "mapper/ArchetypeLibrary.h"
#include "mapper/ArchetypeRetriever.h"

namespace agentic_synth::agent {

namespace {

// ── Deterministic LCG ────────────────────────────────────────────────────────
// Same LCG constants as VariationEngine.cpp so the family of generators behaves
// uniformly. seed → seed * 1664525 + 1013904223 (Numerical Recipes).
struct Lcg {
    uint32_t state;
    explicit Lcg(uint32_t s) noexcept : state(s) {}
    uint32_t next() noexcept {
        state = state * 1664525u + 1013904223u;
        return state;
    }
    // Uniform in [-1, +1].
    float pm1() noexcept { return static_cast<float>(next() >> 8) / 8388608.0f - 1.0f; }
    // Uniform in [0, 1).
    float u01() noexcept { return static_cast<float>(next() >> 8) / 16777216.0f; }
};

// ── lerpPatch (local copy — PrePatchPipeline.cpp keeps it in anonymous-NS) ──
// We duplicate the implementation rather than re-export it so this TU does not
// need to depend on PrePatchPipeline.cpp's internals. Same semantics: snap
// discrete fields at t>=0.5, lerp continuous params.
float lerpf(float a, float b, float t) noexcept { return a + (b - a) * t; }

PatchStruct lerpPatch(const PatchStruct& a, const PatchStruct& b, float t) noexcept {
    PatchStruct out = (t >= 0.5f) ? b : a;

    for (int i = 0; i < kMaxOscillators; ++i) {
        out.osc[i].semitone_offset = lerpf(a.osc[i].semitone_offset, b.osc[i].semitone_offset, t);
        out.osc[i].detune_cents = lerpf(a.osc[i].detune_cents, b.osc[i].detune_cents, t);
        out.osc[i].wavetable_pos = lerpf(a.osc[i].wavetable_pos, b.osc[i].wavetable_pos, t);
        out.osc[i].fm_ratio = lerpf(a.osc[i].fm_ratio, b.osc[i].fm_ratio, t);
        out.osc[i].fm_depth = lerpf(a.osc[i].fm_depth, b.osc[i].fm_depth, t);
        out.osc[i].volume = lerpf(a.osc[i].volume, b.osc[i].volume, t);
        out.osc[i].pan = lerpf(a.osc[i].pan, b.osc[i].pan, t);
        out.osc[i].pulse_width = lerpf(a.osc[i].pulse_width, b.osc[i].pulse_width, t);
    }

    out.filter.cutoff_hz = lerpf(a.filter.cutoff_hz, b.filter.cutoff_hz, t);
    out.filter.resonance = lerpf(a.filter.resonance, b.filter.resonance, t);
    out.filter.env_mod = lerpf(a.filter.env_mod, b.filter.env_mod, t);
    out.filter.key_track = lerpf(a.filter.key_track, b.filter.key_track, t);
    out.filter.drive = lerpf(a.filter.drive, b.filter.drive, t);

    auto lerpEnv = [&](const EnvParams& ea, const EnvParams& eb, EnvParams& eo) {
        eo.attack_s = lerpf(ea.attack_s, eb.attack_s, t);
        eo.decay_s = lerpf(ea.decay_s, eb.decay_s, t);
        eo.sustain = lerpf(ea.sustain, eb.sustain, t);
        eo.release_s = lerpf(ea.release_s, eb.release_s, t);
    };
    lerpEnv(a.filter_env, b.filter_env, out.filter_env);
    lerpEnv(a.amp_env, b.amp_env, out.amp_env);

    for (int i = 0; i < kMaxLfos; ++i) {
        out.lfo[i].rate_hz = lerpf(a.lfo[i].rate_hz, b.lfo[i].rate_hz, t);
        out.lfo[i].depth = lerpf(a.lfo[i].depth, b.lfo[i].depth, t);
        out.lfo[i].phase_offset = lerpf(a.lfo[i].phase_offset, b.lfo[i].phase_offset, t);
    }

    out.reverb.size = lerpf(a.reverb.size, b.reverb.size, t);
    out.reverb.damping = lerpf(a.reverb.damping, b.reverb.damping, t);
    out.reverb.width = lerpf(a.reverb.width, b.reverb.width, t);
    out.reverb.mix = lerpf(a.reverb.mix, b.reverb.mix, t);

    out.delay.time_s = lerpf(a.delay.time_s, b.delay.time_s, t);
    out.delay.feedback = lerpf(a.delay.feedback, b.delay.feedback, t);
    out.delay.mix = lerpf(a.delay.mix, b.delay.mix, t);
    out.delay.stereo = lerpf(a.delay.stereo, b.delay.stereo, t);

    out.master_gain = lerpf(a.master_gain, b.master_gain, t);
    out.portamento_s = lerpf(a.portamento_s, b.portamento_s, t);

    return out;
}

// ── Mutation strategy ────────────────────────────────────────────────────────
// Uniform random perturbation in [-strength, +strength] of each continuous
// param's current value. validate_patch (the caller's existing safety net)
// clamps anything that ends up out-of-range so we don't have to repeat the
// per-field min/max table here.
PatchStruct mutatePatch(const PatchStruct& base, float strength, Lcg& rng) noexcept {
    auto bend = [&](float v) noexcept -> float { return v + v * strength * rng.pm1(); };
    // Additive variant for params that can legitimately be zero — multiplying a
    // 0 sustains / 0 mix never escapes 0.
    auto bendAdd = [&](float v, float scale) noexcept -> float { return v + scale * rng.pm1(); };

    PatchStruct p = base;

    for (int i = 0; i < kMaxOscillators; ++i) {
        p.osc[i].detune_cents = bendAdd(base.osc[i].detune_cents, 25.0f * strength);
        p.osc[i].volume = bend(base.osc[i].volume);
        p.osc[i].pan = bendAdd(base.osc[i].pan, strength);
        p.osc[i].pulse_width = bendAdd(base.osc[i].pulse_width, 0.3f * strength);
        p.osc[i].wavetable_pos = bendAdd(base.osc[i].wavetable_pos, 0.3f * strength);
        p.osc[i].fm_ratio = bend(base.osc[i].fm_ratio);
        p.osc[i].fm_depth = bendAdd(base.osc[i].fm_depth, 0.3f * strength);
    }

    p.filter.cutoff_hz = bend(base.filter.cutoff_hz);
    p.filter.resonance = bendAdd(base.filter.resonance, 0.3f * strength);
    p.filter.drive = bendAdd(base.filter.drive, 0.3f * strength);
    p.filter.env_mod = bendAdd(base.filter.env_mod, 0.3f * strength);
    p.filter.key_track = bendAdd(base.filter.key_track, 0.3f * strength);

    auto bendEnv = [&](const EnvParams& src, EnvParams& dst) {
        dst.attack_s = bendAdd(src.attack_s, 0.5f * strength);
        dst.decay_s = bendAdd(src.decay_s, 0.5f * strength);
        dst.sustain = bendAdd(src.sustain, 0.3f * strength);
        dst.release_s = bendAdd(src.release_s, 1.0f * strength);
    };
    bendEnv(base.amp_env, p.amp_env);
    bendEnv(base.filter_env, p.filter_env);

    for (int i = 0; i < kMaxLfos; ++i) {
        p.lfo[i].rate_hz = bend(base.lfo[i].rate_hz);
        p.lfo[i].depth = bendAdd(base.lfo[i].depth, 0.3f * strength);
        p.lfo[i].phase_offset = bendAdd(base.lfo[i].phase_offset, 0.3f * strength);
    }

    p.reverb.size = bendAdd(base.reverb.size, 0.3f * strength);
    p.reverb.damping = bendAdd(base.reverb.damping, 0.3f * strength);
    p.reverb.width = bendAdd(base.reverb.width, 0.3f * strength);
    p.reverb.mix = bendAdd(base.reverb.mix, 0.3f * strength);

    p.delay.time_s = bend(base.delay.time_s);
    p.delay.feedback = bendAdd(base.delay.feedback, 0.3f * strength);
    p.delay.mix = bendAdd(base.delay.mix, 0.3f * strength);
    p.delay.stereo = bendAdd(base.delay.stereo, 0.3f * strength);

    p.master_gain = bend(base.master_gain);
    // portamento_s is gentle: small absolute shift.
    p.portamento_s = bendAdd(base.portamento_s, 0.1f * strength);

    return validate_patch(p);
}

// ── Crossover (lerp at t=0.5) ────────────────────────────────────────────────
PatchStruct crossoverHalf(const PatchStruct& a, const PatchStruct& b) noexcept {
    return validate_patch(lerpPatch(a, b, 0.5f));
}

// ── Average detune across enabled oscs ───────────────────────────────────────
float avgDetuneMagnitude(const PatchStruct& p) noexcept {
    float sum = 0.0f;
    for (int i = 0; i < kMaxOscillators; ++i)
        sum += std::fabs(p.osc[i].detune_cents);
    return sum;
}

// ── Sensory labeler ──────────────────────────────────────────────────────────
//
// Compare the new patch against the base on 4 perceptual axes:
//   • brightness (filter.cutoff_hz)
//   • envelope length (amp_env.attack_s)
//   • space (reverb.size)
//   • detune spread (sum |osc[i].detune_cents|)
//
// Each axis gets a normalised delta magnitude. The largest-magnitude axis wins
// and we return its sensory word (brighter/warmer/longer/snappier/wider/drier/
// spread/tighter). Below threshold → caller-supplied fallback ("spread" for the
// archetype-bounce strategy, otherwise a stable A/B/C/D/E letter).
std::string labelForDelta(const PatchStruct& base, const PatchStruct& variant, const std::string& fallback) noexcept {
    auto safeDiv = [](float num, float den) noexcept -> float {
        // Treat near-zero denominators as the threshold value itself so the
        // ratio doesn't blow up when the base is silent on that axis.
        const float denom = (std::fabs(den) < 1e-6f) ? 1e-6f : den;
        return num / denom;
    };

    const float cutoffRel = safeDiv(variant.filter.cutoff_hz - base.filter.cutoff_hz, base.filter.cutoff_hz);
    const float attackRel = safeDiv(variant.amp_env.attack_s - base.amp_env.attack_s, base.amp_env.attack_s);
    const float reverbRel = safeDiv(variant.reverb.size - base.reverb.size, base.reverb.size);

    const float baseDetune = avgDetuneMagnitude(base);
    const float varDetune = avgDetuneMagnitude(variant);
    const float detuneRel = (baseDetune < 1e-6f && varDetune < 1e-6f)
                                ? 0.0f
                                : safeDiv(varDetune - baseDetune, (baseDetune < 1e-6f) ? 50.0f : baseDetune);

    struct Axis {
        float mag;
        float signedVal;
        const char* posLabel;
        const char* negLabel;
        float threshold; // signed-delta threshold above which the axis "fires"
    };
    const Axis axes[] = {
        {std::fabs(cutoffRel), cutoffRel, "brighter", "warmer", 0.20f},
        {std::fabs(attackRel), attackRel, "longer", "snappier", 0.50f},
        {std::fabs(reverbRel), reverbRel, "wider", "drier", 0.20f},
        {std::fabs(detuneRel), detuneRel, "spread", "tighter", 0.50f},
    };

    int best = -1;
    float bestMag = 0.0f;
    for (int i = 0; i < 4; ++i) {
        // Axis only competes if its signed delta crosses the threshold.
        if (axes[i].mag > axes[i].threshold && axes[i].mag > bestMag) {
            bestMag = axes[i].mag;
            best = i;
        }
    }
    if (best < 0)
        return fallback;
    return axes[best].signedVal >= 0.0f ? axes[best].posLabel : axes[best].negLabel;
}

} // namespace

MorphResult morph(const PatchStruct& base,
                  const std::vector<PatchStruct>& history,
                  const std::vector<PatchStruct>& liked,
                  const std::string& prompt,
                  uint32_t seed) {
    MorphResult result;

    // Validate the base once so the validators don't bias the labels with
    // hidden clamp jumps when the caller hands us a slightly out-of-range patch.
    const PatchStruct cleanBase = validate_patch(base);

    // Strategy 1 — heavy mutation (~12.5% strength, midpoint of 10-15% spec).
    {
        Lcg rng(seed + 1u);
        result.variations[0] = mutatePatch(cleanBase, 0.125f, rng);
    }

    // Strategy 2 — light mutation (~4% strength, midpoint of 3-5% spec).
    {
        Lcg rng(seed + 2u);
        result.variations[1] = mutatePatch(cleanBase, 0.04f, rng);
    }

    // Strategy 3 — crossover with most recent liked patch (lerp t=0.5). When
    // the liked pool is empty we fall back to a fresh heavy mutation against a
    // distinct sub-seed so the slot still produces a usable, deterministic
    // variation rather than a clone of slot 0.
    if (!liked.empty()) {
        result.variations[2] = crossoverHalf(cleanBase, validate_patch(liked.back()));
    } else {
        Lcg rng(seed + 3u);
        result.variations[2] = mutatePatch(cleanBase, 0.10f, rng);
    }

    // Strategy 4 — crossover with most recent history patch. Same fallback as
    // strategy 3 with a different sub-seed.
    if (!history.empty()) {
        result.variations[3] = crossoverHalf(cleanBase, validate_patch(history.back()));
    } else {
        Lcg rng(seed + 4u);
        result.variations[3] = mutatePatch(cleanBase, 0.08f, rng);
    }

    // Strategy 5 — archetype bounce. Retrieve top-3 archetypes for the prompt
    // and pick one by seed%3. ArchetypeRetriever guarantees a non-empty result
    // (falls back to default_init) so this is total.
    {
        const auto top = mapper::ArchetypeRetriever::retrieveTopN(prompt, 3);
        const PatchStruct archetypePatch = top.empty() ? make_default_patch() : validate_patch(top[seed % top.size()]->patch);
        // Light blend so we keep some flavour of the user's current patch
        // (0.5 lerp = even mix) — pure-archetype was too aggressive in user
        // tests; this matches the rest of the strategies' "morph at 0.5" feel.
        result.variations[4] = crossoverHalf(cleanBase, archetypePatch);
    }

    // ── Labeling ─────────────────────────────────────────────────────────────
    // Use the largest-magnitude perceptual delta. Strategy 5 falls back to
    // "spread" when nothing crosses threshold, others fall back to stable A-E
    // letters so the UI never shows an empty label slot.
    const char* letterFallback[5] = {"A", "B", "C", "D", "E"};
    for (int i = 0; i < 5; ++i) {
        const std::string fallback = (i == 4) ? std::string("spread") : std::string(letterFallback[i]);
        result.labels[i] = labelForDelta(cleanBase, result.variations[i], fallback);
    }

    return result;
}

} // namespace agentic_synth::agent
