#include "engine/MorphEngine.h"

#include <algorithm>
#include <cmath>
#include <functional>

namespace agentic_synth::engine {

// ---------------------------------------------------------------------------
// Target management
// ---------------------------------------------------------------------------

int MorphEngine::saveTarget(const PatchStruct& patch, int index) noexcept {
    if (index >= 0 && index < kMaxTargets) {
        targets_[static_cast<size_t>(index)] = patch;
        return index;
    }
    // Auto-assign to first empty slot.
    for (int i = 0; i < kMaxTargets; ++i) {
        if (!targets_[static_cast<size_t>(i)]) {
            targets_[static_cast<size_t>(i)] = patch;
            return i;
        }
    }
    return -1;
}

void MorphEngine::clearTarget(int index) noexcept {
    if (index >= 0 && index < kMaxTargets)
        targets_[static_cast<size_t>(index)].reset();
}

void MorphEngine::clearAll() noexcept {
    for (auto& t : targets_)
        t.reset();
}

int MorphEngine::targetCount() const noexcept {
    int count = 0;
    for (const auto& t : targets_)
        if (t)
            ++count;
    return count;
}

std::optional<PatchStruct> MorphEngine::target(int index) const noexcept {
    if (index < 0 || index >= kMaxTargets)
        return std::nullopt;
    return targets_[static_cast<size_t>(index)];
}

// ---------------------------------------------------------------------------
// Morph position
// ---------------------------------------------------------------------------

void MorphEngine::setPosition(float pos) noexcept {
    position_ = std::clamp(pos, 0.0f, 1.0f);
    // Always mark dirty without touching callback_ — std::function is not RT-safe.
    // pollCallback() checks callback_ on the UI thread.
    positionDirty_.store(true, std::memory_order_release);
}

bool MorphEngine::pollCallback() noexcept {
    if (!positionDirty_.load(std::memory_order_acquire))
        return false;
    positionDirty_.store(false, std::memory_order_relaxed);
    if (callback_)
        callback_(morphedPatch());
    return true;
}

// ---------------------------------------------------------------------------
// Interpolation
// ---------------------------------------------------------------------------

PatchStruct MorphEngine::lerp(const PatchStruct& a, const PatchStruct& b, float t) noexcept {
    PatchStruct out = a;
    const float u = 1.0f - t;

    // Oscillators
    for (int i = 0; i < kMaxOscillators; ++i) {
        out.osc[i].semitone_offset = u * a.osc[i].semitone_offset + t * b.osc[i].semitone_offset;
        out.osc[i].detune_cents = u * a.osc[i].detune_cents + t * b.osc[i].detune_cents;
        out.osc[i].wavetable_pos = u * a.osc[i].wavetable_pos + t * b.osc[i].wavetable_pos;
        out.osc[i].fm_ratio = u * a.osc[i].fm_ratio + t * b.osc[i].fm_ratio;
        out.osc[i].fm_depth = u * a.osc[i].fm_depth + t * b.osc[i].fm_depth;
        out.osc[i].volume = u * a.osc[i].volume + t * b.osc[i].volume;
        out.osc[i].pan = u * a.osc[i].pan + t * b.osc[i].pan;
        out.osc[i].pulse_width = u * a.osc[i].pulse_width + t * b.osc[i].pulse_width;
        // Discrete fields snap at midpoint
        if (t >= 0.5f) {
            out.osc[i].type = b.osc[i].type;
            out.osc[i].enabled = b.osc[i].enabled;
        }
    }

    // Filter
    out.filter.cutoff_hz = u * a.filter.cutoff_hz + t * b.filter.cutoff_hz;
    out.filter.resonance = u * a.filter.resonance + t * b.filter.resonance;
    out.filter.env_mod = u * a.filter.env_mod + t * b.filter.env_mod;
    out.filter.key_track = u * a.filter.key_track + t * b.filter.key_track;
    out.filter.drive = u * a.filter.drive + t * b.filter.drive;
    if (t >= 0.5f)
        out.filter.type = b.filter.type;

    // Filter envelope
    out.filter_env.attack_s = u * a.filter_env.attack_s + t * b.filter_env.attack_s;
    out.filter_env.decay_s = u * a.filter_env.decay_s + t * b.filter_env.decay_s;
    out.filter_env.sustain = u * a.filter_env.sustain + t * b.filter_env.sustain;
    out.filter_env.release_s = u * a.filter_env.release_s + t * b.filter_env.release_s;

    // Amp envelope
    out.amp_env.attack_s = u * a.amp_env.attack_s + t * b.amp_env.attack_s;
    out.amp_env.decay_s = u * a.amp_env.decay_s + t * b.amp_env.decay_s;
    out.amp_env.sustain = u * a.amp_env.sustain + t * b.amp_env.sustain;
    out.amp_env.release_s = u * a.amp_env.release_s + t * b.amp_env.release_s;

    // LFOs
    for (int i = 0; i < kMaxLfos; ++i) {
        out.lfo[i].rate_hz = u * a.lfo[i].rate_hz + t * b.lfo[i].rate_hz;
        out.lfo[i].depth = u * a.lfo[i].depth + t * b.lfo[i].depth;
        out.lfo[i].phase_offset = u * a.lfo[i].phase_offset + t * b.lfo[i].phase_offset;
        if (t >= 0.5f) {
            out.lfo[i].waveform = b.lfo[i].waveform;
            out.lfo[i].target = b.lfo[i].target;
            out.lfo[i].bpm_sync = b.lfo[i].bpm_sync;
        }
    }

    // Reverb
    out.reverb.size = u * a.reverb.size + t * b.reverb.size;
    out.reverb.damping = u * a.reverb.damping + t * b.reverb.damping;
    out.reverb.width = u * a.reverb.width + t * b.reverb.width;
    out.reverb.mix = u * a.reverb.mix + t * b.reverb.mix;

    // Delay
    out.delay.time_s = u * a.delay.time_s + t * b.delay.time_s;
    out.delay.feedback = u * a.delay.feedback + t * b.delay.feedback;
    out.delay.mix = u * a.delay.mix + t * b.delay.mix;
    if (t >= 0.5f)
        out.delay.bpm_sync = b.delay.bpm_sync;

    // Global
    out.master_gain = u * a.master_gain + t * b.master_gain;
    out.portamento_s = u * a.portamento_s + t * b.portamento_s;
    if (t >= 0.5f)
        out.voice_count = b.voice_count;

    return out;
}

PatchStruct MorphEngine::morphedPatchAt(float pos) const noexcept {
    // Collect active targets in slot order.
    PatchStruct active[kMaxTargets];
    int n = 0;
    for (int i = 0; i < kMaxTargets; ++i) {
        if (targets_[static_cast<size_t>(i)])
            active[n++] = *targets_[static_cast<size_t>(i)];
    }

    if (n == 0)
        return make_default_patch();
    if (n == 1)
        return active[0];

    const float clamped = std::clamp(pos, 0.0f, 1.0f);
    // Map [0,1] across (n-1) segments.
    const float scaled = clamped * static_cast<float>(n - 1);
    const int seg = std::min(static_cast<int>(scaled), n - 2);
    const float t = scaled - static_cast<float>(seg);

    return lerp(active[seg], active[seg + 1], t);
}

PatchStruct MorphEngine::morphedPatch() const noexcept { return morphedPatchAt(position_); }

// ---------------------------------------------------------------------------
// MIDI CC routing
// ---------------------------------------------------------------------------

bool MorphEngine::onMidiCC(int controller, int value) noexcept {
    if (controller != morphCc_)
        return false;
    setPosition(static_cast<float>(value) / 127.0f);
    return true;
}

} // namespace agentic_synth::engine
