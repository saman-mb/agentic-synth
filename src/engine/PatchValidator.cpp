#include "engine/PatchValidator.h"

#include <algorithm>
#include <cmath>

namespace agentic_synth {

namespace {

inline float safe_or(float v, float def) noexcept { return std::isfinite(v) ? v : def; }

inline float clamp_f(float v, float lo, float hi, float def) noexcept { return std::clamp(safe_or(v, def), lo, hi); }

void validate_env(EnvParams& e) noexcept {
    e.attack_s = clamp_f(e.attack_s, 0.0f, 10.0f, 0.01f);
    e.decay_s = clamp_f(e.decay_s, 0.0f, 10.0f, 0.1f);
    e.sustain = clamp_f(e.sustain, 0.0f, 1.0f, 1.0f);
    e.release_s = clamp_f(e.release_s, 0.0f, 20.0f, 0.1f);
}

void validate_osc(OscParams& o) noexcept {
    o.semitone_offset = clamp_f(o.semitone_offset, -48.0f, 48.0f, 0.0f);
    o.detune_cents = clamp_f(o.detune_cents, -100.0f, 100.0f, 0.0f);
    o.wavetable_pos = clamp_f(o.wavetable_pos, 0.0f, 1.0f, 0.0f);
    o.fm_ratio = clamp_f(o.fm_ratio, 0.5f, 16.0f, 1.0f);
    o.fm_depth = clamp_f(o.fm_depth, 0.0f, 1.0f, 0.0f);
    o.volume = clamp_f(o.volume, 0.0f, 1.0f, 1.0f);
    o.pan = clamp_f(o.pan, -1.0f, 1.0f, 0.0f);
    o.pulse_width = clamp_f(o.pulse_width, 0.01f, 0.99f, 0.5f);
    o.enabled = (o.enabled != 0) ? 1u : 0u;
}

} // namespace

PatchStruct validate_patch(PatchStruct p, UnsafeModeFlags flags) noexcept {
    p.version = kPatchStructVersion;

    for (int i = 0; i < kMaxOscillators; ++i)
        validate_osc(p.osc[i]);

    p.filter.cutoff_hz = clamp_f(p.filter.cutoff_hz, kFilterCutoffFloor, kFilterCutoffCeiling, 1000.0f);
    const float res_ceil = flags.allow_self_oscillation ? 1.0f : kSafeResonanceCeiling;
    p.filter.resonance = clamp_f(p.filter.resonance, 0.0f, res_ceil, 0.0f);
    p.filter.env_mod = clamp_f(p.filter.env_mod, -1.0f, 1.0f, 0.0f);
    p.filter.key_track = clamp_f(p.filter.key_track, 0.0f, 1.0f, 0.0f);
    p.filter.drive = clamp_f(p.filter.drive, 0.0f, 1.0f, 0.0f);

    validate_env(p.filter_env);
    validate_env(p.amp_env);

    for (int i = 0; i < kMaxLfos; ++i) {
        p.lfo[i].rate_hz = clamp_f(p.lfo[i].rate_hz, 0.01f, 20.0f, 1.0f);
        p.lfo[i].depth = clamp_f(p.lfo[i].depth, 0.0f, 1.0f, 0.0f);
        p.lfo[i].phase_offset = clamp_f(p.lfo[i].phase_offset, 0.0f, 1.0f, 0.0f);
        p.lfo[i].bpm_sync = (p.lfo[i].bpm_sync != 0) ? 1u : 0u;
    }

    p.reverb.size = clamp_f(p.reverb.size, 0.0f, 1.0f, 0.5f);
    p.reverb.damping = clamp_f(p.reverb.damping, 0.0f, 1.0f, 0.5f);
    p.reverb.width = clamp_f(p.reverb.width, 0.0f, 1.0f, 1.0f);
    p.reverb.mix = clamp_f(p.reverb.mix, 0.0f, 1.0f, 0.0f);

    p.delay.time_s = clamp_f(p.delay.time_s, 0.0f, 2.0f, 0.5f);
    p.delay.feedback = clamp_f(p.delay.feedback, 0.0f, 0.99f, 0.0f);
    p.delay.mix = clamp_f(p.delay.mix, 0.0f, 1.0f, 0.0f);
    p.delay.stereo = clamp_f(p.delay.stereo, 0.0f, 1.0f, 0.5f);
    p.delay.bpm_sync = (p.delay.bpm_sync != 0) ? 1u : 0u;

    p.master_gain = clamp_f(p.master_gain, 0.0f, 1.0f, 1.0f);
    p.portamento_s = clamp_f(p.portamento_s, 0.0f, 10.0f, 0.0f);

    if (p.voice_count < 1 || p.voice_count > 16)
        p.voice_count = 8;

    return p;
}

bool patch_is_finite(const PatchStruct& p) noexcept {
    auto ok = [](float v) noexcept { return std::isfinite(v); };

    for (int i = 0; i < kMaxOscillators; ++i) {
        const auto& o = p.osc[i];
        if (!ok(o.semitone_offset) || !ok(o.detune_cents) || !ok(o.wavetable_pos) || !ok(o.fm_ratio) ||
            !ok(o.fm_depth) || !ok(o.volume) || !ok(o.pan) || !ok(o.pulse_width))
            return false;
    }

    if (!ok(p.filter.cutoff_hz) || !ok(p.filter.resonance) || !ok(p.filter.env_mod) || !ok(p.filter.key_track) ||
        !ok(p.filter.drive))
        return false;

    auto env_ok = [&ok](const EnvParams& e) noexcept {
        return ok(e.attack_s) && ok(e.decay_s) && ok(e.sustain) && ok(e.release_s);
    };
    if (!env_ok(p.filter_env) || !env_ok(p.amp_env))
        return false;

    for (int i = 0; i < kMaxLfos; ++i) {
        if (!ok(p.lfo[i].rate_hz) || !ok(p.lfo[i].depth) || !ok(p.lfo[i].phase_offset))
            return false;
    }

    if (!ok(p.reverb.size) || !ok(p.reverb.damping) || !ok(p.reverb.width) || !ok(p.reverb.mix))
        return false;

    if (!ok(p.delay.time_s) || !ok(p.delay.feedback) || !ok(p.delay.mix) || !ok(p.delay.stereo))
        return false;

    return ok(p.master_gain) && ok(p.portamento_s);
}

} // namespace agentic_synth
