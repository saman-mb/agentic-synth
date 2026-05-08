#include "engine/VariationEngine.h"

#include <algorithm>
#include <future>

namespace agentic_synth::engine {

namespace {

float lerp(float a, float b, float t) noexcept { return a + (b - a) * t; }

float clampf(float v, float lo, float hi) noexcept { return v < lo ? lo : (v > hi ? hi : v); }

// LCG PRNG — deterministic, seed-controlled, no heap.
uint32_t lcg_next(uint32_t& state) noexcept {
    state = state * 1664525u + 1013904223u;
    return state;
}

// Returns a value in [-1, 1].
float lcg_pm1(uint32_t& state) noexcept { return static_cast<float>(lcg_next(state) >> 8) / 8388608.0f - 1.0f; }

PatchStruct lerpPatch(const PatchStruct& a, const PatchStruct& b, float t) noexcept {
    PatchStruct out = (t >= 0.5f) ? b : a;

    for (int i = 0; i < kMaxOscillators; ++i) {
        out.osc[i].semitone_offset = lerp(a.osc[i].semitone_offset, b.osc[i].semitone_offset, t);
        out.osc[i].detune_cents = lerp(a.osc[i].detune_cents, b.osc[i].detune_cents, t);
        out.osc[i].wavetable_pos = lerp(a.osc[i].wavetable_pos, b.osc[i].wavetable_pos, t);
        out.osc[i].fm_ratio = lerp(a.osc[i].fm_ratio, b.osc[i].fm_ratio, t);
        out.osc[i].fm_depth = lerp(a.osc[i].fm_depth, b.osc[i].fm_depth, t);
        out.osc[i].volume = lerp(a.osc[i].volume, b.osc[i].volume, t);
        out.osc[i].pan = lerp(a.osc[i].pan, b.osc[i].pan, t);
        out.osc[i].pulse_width = lerp(a.osc[i].pulse_width, b.osc[i].pulse_width, t);
    }

    out.filter.cutoff_hz = lerp(a.filter.cutoff_hz, b.filter.cutoff_hz, t);
    out.filter.resonance = lerp(a.filter.resonance, b.filter.resonance, t);
    out.filter.env_mod = lerp(a.filter.env_mod, b.filter.env_mod, t);
    out.filter.key_track = lerp(a.filter.key_track, b.filter.key_track, t);
    out.filter.drive = lerp(a.filter.drive, b.filter.drive, t);

    auto lerpEnv = [&](const EnvParams& ea, const EnvParams& eb, EnvParams& eo) {
        eo.attack_s = lerp(ea.attack_s, eb.attack_s, t);
        eo.decay_s = lerp(ea.decay_s, eb.decay_s, t);
        eo.sustain = lerp(ea.sustain, eb.sustain, t);
        eo.release_s = lerp(ea.release_s, eb.release_s, t);
    };
    lerpEnv(a.filter_env, b.filter_env, out.filter_env);
    lerpEnv(a.amp_env, b.amp_env, out.amp_env);

    for (int i = 0; i < kMaxLfos; ++i) {
        out.lfo[i].rate_hz = lerp(a.lfo[i].rate_hz, b.lfo[i].rate_hz, t);
        out.lfo[i].depth = lerp(a.lfo[i].depth, b.lfo[i].depth, t);
        out.lfo[i].phase_offset = lerp(a.lfo[i].phase_offset, b.lfo[i].phase_offset, t);
    }

    out.reverb.size = lerp(a.reverb.size, b.reverb.size, t);
    out.reverb.damping = lerp(a.reverb.damping, b.reverb.damping, t);
    out.reverb.width = lerp(a.reverb.width, b.reverb.width, t);
    out.reverb.mix = lerp(a.reverb.mix, b.reverb.mix, t);

    out.delay.time_s = lerp(a.delay.time_s, b.delay.time_s, t);
    out.delay.feedback = lerp(a.delay.feedback, b.delay.feedback, t);
    out.delay.mix = lerp(a.delay.mix, b.delay.mix, t);

    out.master_gain = lerp(a.master_gain, b.master_gain, t);
    out.portamento_s = lerp(a.portamento_s, b.portamento_s, t);

    return out;
}

// Build a maximally contrasting "hot" patch from base for temperature sweep.
PatchStruct makeHotPatch(const PatchStruct& base) noexcept {
    PatchStruct hot = base;
    hot.filter.cutoff_hz = clampf(base.filter.cutoff_hz * 2.0f + 2000.0f, 20.0f, 18000.0f);
    hot.filter.resonance = clampf(base.filter.resonance + 0.5f, 0.0f, 0.95f);
    hot.filter.drive = clampf(base.filter.drive + 0.4f, 0.0f, 1.0f);
    hot.amp_env.attack_s = clampf(base.amp_env.attack_s * 3.0f + 0.05f, 0.0f, 10.0f);
    hot.amp_env.release_s = clampf(base.amp_env.release_s * 2.0f + 0.3f, 0.0f, 20.0f);
    hot.reverb.mix = clampf(base.reverb.mix + 0.45f, 0.0f, 1.0f);
    hot.reverb.size = clampf(base.reverb.size + 0.35f, 0.0f, 1.0f);
    hot.lfo[0].depth = clampf(base.lfo[0].depth + 0.6f, 0.0f, 1.0f);
    hot.lfo[0].rate_hz = clampf(base.lfo[0].rate_hz * 1.8f + 0.5f, 0.01f, 20.0f);
    hot.master_gain = clampf(base.master_gain * 0.9f, 0.0f, 1.0f);
    return hot;
}

// Apply ±15% perturbation to a single float, clamped within [lo, hi].
float perturb(float v, float scale, float rnd, float lo, float hi) noexcept {
    return clampf(v + v * 0.15f * scale * rnd, lo, hi);
}

} // namespace

std::array<PatchStruct, VariationEngine::kVariationCount>
VariationEngine::temperatureSweep(const PatchStruct& base) const noexcept {
    const PatchStruct hot = makeHotPatch(base);
    std::array<PatchStruct, kVariationCount> result;
    for (int i = 0; i < kVariationCount; ++i) {
        const float t = 0.2f * static_cast<float>(i + 1);
        result[i] = lerpPatch(base, hot, t);
    }
    return result;
}

std::array<PatchStruct, VariationEngine::kVariationCount> VariationEngine::perturbation(const PatchStruct& base,
                                                                                        uint32_t seed) const noexcept {
    std::array<PatchStruct, kVariationCount> result;
    for (int i = 0; i < kVariationCount; ++i) {
        uint32_t rng = seed + static_cast<uint32_t>(i) * 1337u;
        PatchStruct p = base;

        // Brightness group: filter tone controls
        p.filter.cutoff_hz = perturb(base.filter.cutoff_hz, 1.0f, lcg_pm1(rng), 20.0f, 18000.0f);
        p.filter.resonance = perturb(base.filter.resonance, 1.0f, lcg_pm1(rng), 0.0f, 0.95f);
        p.filter.drive = perturb(base.filter.drive, 1.0f, lcg_pm1(rng), 0.0f, 1.0f);

        // Space group: reverb and delay
        p.reverb.size = perturb(base.reverb.size, 1.0f, lcg_pm1(rng), 0.0f, 1.0f);
        p.reverb.mix = perturb(base.reverb.mix, 1.0f, lcg_pm1(rng), 0.0f, 1.0f);
        p.reverb.damping = perturb(base.reverb.damping, 0.67f, lcg_pm1(rng), 0.0f, 1.0f);
        p.delay.mix = perturb(base.delay.mix, 1.0f, lcg_pm1(rng), 0.0f, 1.0f);

        // Movement group: LFO and envelope
        p.lfo[0].rate_hz = perturb(base.lfo[0].rate_hz, 1.0f, lcg_pm1(rng), 0.01f, 20.0f);
        p.lfo[0].depth = perturb(base.lfo[0].depth, 1.0f, lcg_pm1(rng), 0.0f, 1.0f);
        p.amp_env.attack_s = perturb(base.amp_env.attack_s, 1.0f, lcg_pm1(rng), 0.0f, 10.0f);
        p.amp_env.release_s = perturb(base.amp_env.release_s, 1.0f, lcg_pm1(rng), 0.0f, 20.0f);

        // Character group: oscillator texture
        p.osc[0].detune_cents = clampf(base.osc[0].detune_cents + lcg_pm1(rng) * 15.0f, -100.0f, 100.0f);
        p.portamento_s = clampf(base.portamento_s + std::abs(lcg_pm1(rng)) * 0.15f, 0.0f, 2.0f);
        p.master_gain = perturb(base.master_gain, 0.67f, lcg_pm1(rng), 0.0f, 1.0f);

        result[i] = p;
    }
    return result;
}

std::array<PatchStruct, VariationEngine::kVariationCount>
VariationEngine::morph(const PatchStruct& base, const PatchStruct& target) const noexcept {
    std::array<PatchStruct, kVariationCount> result;
    for (int i = 0; i < kVariationCount; ++i) {
        const float t = 0.2f * static_cast<float>(i + 1);
        result[i] = lerpPatch(base, target, t);
    }
    return result;
}

std::array<PatchStruct, VariationEngine::kVariationCount>
VariationEngine::generateVariations(const PatchStruct& base) const {
    const PatchStruct morphTarget = makeHotPatch(make_default_patch());

    // Launch all three strategies concurrently.
    auto futTemp = std::async(std::launch::async, [&] { return temperatureSweep(base); });
    auto futPert = std::async(std::launch::async, [&] { return perturbation(base); });
    auto futMorph = std::async(std::launch::async, [&] { return morph(base, morphTarget); });

    const auto temps = futTemp.get();
    const auto perts = futPert.get();
    const auto morphs = futMorph.get();

    // 2 from temperature (moderate + strong), 2 from perturbation (different seeds),
    // 1 from morph (midpoint).
    std::array<PatchStruct, kVariationCount> result;
    result[0] = temps[1];  // temperature 0.4
    result[1] = temps[3];  // temperature 0.8
    result[2] = perts[0];  // perturbation seed 42+0
    result[3] = perts[2];  // perturbation seed 42+2*1337
    result[4] = morphs[2]; // morph at 0.6
    return result;
}

std::array<PatchStruct, VariationEngine::kVariationCount>
VariationEngine::generateVariationsWithSeed(const PatchStruct& base, uint32_t perturbSeed) const {
    const PatchStruct morphTarget = makeHotPatch(make_default_patch());

    auto futTemp  = std::async(std::launch::async, [&] { return temperatureSweep(base); });
    auto futPert  = std::async(std::launch::async, [&] { return perturbation(base, perturbSeed); });
    auto futMorph = std::async(std::launch::async, [&] { return morph(base, morphTarget); });

    const auto temps  = futTemp.get();
    const auto perts  = futPert.get();
    const auto morphs = futMorph.get();

    std::array<PatchStruct, kVariationCount> result;
    result[0] = temps[1];
    result[1] = temps[3];
    result[2] = perts[0];
    result[3] = perts[2];
    result[4] = morphs[2];
    return result;
}

} // namespace agentic_synth::engine
