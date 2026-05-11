#include "agent/ParamMap.h"

#include <array>

namespace agentic_synth::agent {

namespace {

// One row per UI param path. Field assignments mirror the prior
// paramToDelta if/else cascade exactly — any reorder is cosmetic only,
// behaviour must be byte-identical.
//
// Keep alphabetised within each group; group order matches the original
// cascade so a diff reviewer can pair rows 1:1.
// Size is deduced from the initialiser so adding a row is a single-line
// edit; no parallel count to keep in sync.
constexpr ParamSlot kParamSlots[] = {
    // Filter
    {"filter.cutoff_hz",    [](mapper::PatchDelta& d, float v) { d.filter_cutoff = v; }},
    {"filter.resonance",    [](mapper::PatchDelta& d, float v) { d.filter_resonance = v; }},
    {"filter.env_mod",      [](mapper::PatchDelta& d, float v) { d.filter_env_mod = v; }},
    {"filter.drive",        [](mapper::PatchDelta& d, float v) { d.filter_drive = v; }},
    // Amp envelope
    {"amp_env.attack_s",    [](mapper::PatchDelta& d, float v) { d.amp_attack = v; }},
    {"amp_env.decay_s",     [](mapper::PatchDelta& d, float v) { d.amp_decay = v; }},
    {"amp_env.sustain",     [](mapper::PatchDelta& d, float v) { d.amp_sustain = v; }},
    {"amp_env.release_s",   [](mapper::PatchDelta& d, float v) { d.amp_release = v; }},
    // Filter envelope
    {"filter_env.attack_s", [](mapper::PatchDelta& d, float v) { d.flt_attack = v; }},
    {"filter_env.decay_s",  [](mapper::PatchDelta& d, float v) { d.flt_decay = v; }},
    {"filter_env.sustain",  [](mapper::PatchDelta& d, float v) { d.flt_sustain = v; }},
    {"filter_env.release_s",[](mapper::PatchDelta& d, float v) { d.flt_release = v; }},
    // LFO 0
    {"lfo.0.rate_hz",       [](mapper::PatchDelta& d, float v) { d.lfo0_rate = v; }},
    {"lfo.0.depth",         [](mapper::PatchDelta& d, float v) { d.lfo0_depth = v; }},
    // Reverb
    {"reverb.size",         [](mapper::PatchDelta& d, float v) { d.reverb_size = v; }},
    {"reverb.damping",      [](mapper::PatchDelta& d, float v) { d.reverb_damping = v; }},
    {"reverb.width",        [](mapper::PatchDelta& d, float v) { d.reverb_width = v; }},
    {"reverb.mix",          [](mapper::PatchDelta& d, float v) { d.reverb_mix = v; }},
    // Delay
    {"delay.time_s",        [](mapper::PatchDelta& d, float v) { d.delay_time = v; }},
    {"delay.feedback",      [](mapper::PatchDelta& d, float v) { d.delay_feedback = v; }},
    {"delay.mix",           [](mapper::PatchDelta& d, float v) { d.delay_mix = v; }},
    // Global
    {"master_gain",         [](mapper::PatchDelta& d, float v) { d.master_gain = v; }},
    {"portamento_s",        [](mapper::PatchDelta& d, float v) { d.portamento = v; }},
    // Oscillators — paths are literal, so each (osc-index, field) gets its
    // own row. Previously parsed via std::stoi + nested if/else; the
    // try/catch was guarding that stoi, not handling int-typed PatchDelta
    // fields. (voice_count, the only uint8_t in PatchDelta, was never
    // reachable through the UI path and stays unwired here.)
    {"osc.0.volume",          [](mapper::PatchDelta& d, float v) { d.osc0_volume = v; }},
    {"osc.0.detune_cents",    [](mapper::PatchDelta& d, float v) { d.osc0_detune = v; }},
    {"osc.0.semitone_offset", [](mapper::PatchDelta& d, float v) { d.osc0_semitone = v; }},
    {"osc.0.fm_ratio",        [](mapper::PatchDelta& d, float v) { d.osc0_fm_ratio = v; }},
    {"osc.0.fm_depth",        [](mapper::PatchDelta& d, float v) { d.osc0_fm_depth = v; }},
    {"osc.0.pulse_width",     [](mapper::PatchDelta& d, float v) { d.osc0_pulse_width = v; }},
    {"osc.1.volume",          [](mapper::PatchDelta& d, float v) { d.osc1_volume = v; }},
    {"osc.1.detune_cents",    [](mapper::PatchDelta& d, float v) { d.osc1_detune = v; }},
};

} // namespace

std::span<const ParamSlot> getParamMap() noexcept {
    return std::span<const ParamSlot>(kParamSlots);
}

mapper::PatchDelta paramToDelta(const std::string& param, float value) noexcept {
    mapper::PatchDelta d;
    for (const auto& slot : kParamSlots) {
        if (slot.path == param) {
            slot.assign(d, value);
            return d;
        }
    }
    return d; // unknown param → empty delta (no-op, matches prior behaviour)
}

} // namespace agentic_synth::agent
