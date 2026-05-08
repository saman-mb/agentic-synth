#include "agent/PrePatchPipeline.h"

namespace agentic_synth::agent {

namespace {
float lerp(float a, float b, float t) noexcept { return a + (b - a) * t; }
} // namespace

PatchStruct lerpPatch(const PatchStruct& a, const PatchStruct& b, float t) noexcept {
    // Non-float fields (enums, flags, counters) snap to b at the midpoint.
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

PatchStruct PrePatchPipeline::submit(const std::string& prompt) {
    submitTime_ = std::chrono::steady_clock::now();
    const PatchStruct p = validate_patch(parser_.parse(prompt));
    {
        std::lock_guard lock(producerMutex_);
        lastHeuristicPatch_ = p;
        if (!queue_.push(p))
            droppedPatches_.fetch_add(1, std::memory_order_relaxed);
    }
    dispatchLatencyMs_ =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - submitTime_).count();
    return p;
}

void PrePatchPipeline::refinePatch(const PatchStruct& llmPatch) {
    const PatchStruct safeTarget = validate_patch(llmPatch);
    std::lock_guard lock(producerMutex_);
    for (int step = 1; step <= kTransitionSteps; ++step) {
        const float t = static_cast<float>(step) / kTransitionSteps;
        if (!queue_.push(validate_patch(lerpPatch(lastHeuristicPatch_, safeTarget, t))))
            droppedPatches_.fetch_add(1, std::memory_order_relaxed);
    }
}

std::optional<PatchStruct> PrePatchPipeline::poll() noexcept { return queue_.pop(); }

double PrePatchPipeline::lastDispatchLatencyMs() const noexcept { return dispatchLatencyMs_; }

void PrePatchPipeline::injectPatch(const PatchStruct& patch) {
    std::lock_guard lock(producerMutex_);
    lastHeuristicPatch_ = validate_patch(patch);
    if (!queue_.push(lastHeuristicPatch_))
        droppedPatches_.fetch_add(1, std::memory_order_relaxed);
}

PatchStruct PrePatchPipeline::currentPatch() const {
    std::lock_guard lock(producerMutex_);
    return lastHeuristicPatch_;
}

} // namespace agentic_synth::agent
