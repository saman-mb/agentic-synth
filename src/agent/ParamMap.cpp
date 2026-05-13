#include "agent/ParamMap.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace agentic_synth::agent {

namespace {

int roundedInt(float v) noexcept {
    if (!std::isfinite(v))
        return 0;
    return static_cast<int>(std::lround(v));
}

OscType oscTypeFromFloat(float v) noexcept {
    return static_cast<OscType>(std::clamp(roundedInt(v), 0, 7));
}

FilterType filterTypeFromFloat(float v) noexcept {
    return static_cast<FilterType>(std::clamp(roundedInt(v), 0, 4));
}

LfoWaveform lfoWaveformFromFloat(float v) noexcept {
    return static_cast<LfoWaveform>(std::clamp(roundedInt(v), 0, 4));
}

LfoTarget lfoTargetFromFloat(float v) noexcept {
    return static_cast<LfoTarget>(std::clamp(roundedInt(v), 0, 6));
}

bool boolFromFloat(float v) noexcept { return v >= 0.5f; }

uint8_t voiceCountFromFloat(float v) noexcept {
    return static_cast<uint8_t>(std::clamp(roundedInt(v), 1, 16));
}

// One row per UI param path. This is the C++ side of the React PatchParams
// control surface: every editable synth field gets one literal dotted path.
//
// Keep alphabetised within each group; group order matches the original
// cascade so a diff reviewer can pair rows 1:1.
// Size is deduced from the initialiser so adding a row is a single-line
// edit; no parallel count to keep in sync.
constexpr ParamSlot kParamSlots[] = {
    // Filter
    {"filter.type", [](mapper::PatchDelta& d, float v) { d.filter_type = filterTypeFromFloat(v); }},
    {"filter.cutoff_hz", [](mapper::PatchDelta& d, float v) { d.filter_cutoff = v; }},
    {"filter.resonance", [](mapper::PatchDelta& d, float v) { d.filter_resonance = v; }},
    {"filter.env_mod", [](mapper::PatchDelta& d, float v) { d.filter_env_mod = v; }},
    {"filter.key_track", [](mapper::PatchDelta& d, float v) { d.filter_key_track = v; }},
    {"filter.drive", [](mapper::PatchDelta& d, float v) { d.filter_drive = v; }},
    // Amp envelope
    {"amp_env.attack_s", [](mapper::PatchDelta& d, float v) { d.amp_attack = v; }},
    {"amp_env.decay_s", [](mapper::PatchDelta& d, float v) { d.amp_decay = v; }},
    {"amp_env.sustain", [](mapper::PatchDelta& d, float v) { d.amp_sustain = v; }},
    {"amp_env.release_s", [](mapper::PatchDelta& d, float v) { d.amp_release = v; }},
    // Filter envelope
    {"filter_env.attack_s", [](mapper::PatchDelta& d, float v) { d.flt_attack = v; }},
    {"filter_env.decay_s", [](mapper::PatchDelta& d, float v) { d.flt_decay = v; }},
    {"filter_env.sustain", [](mapper::PatchDelta& d, float v) { d.flt_sustain = v; }},
    {"filter_env.release_s", [](mapper::PatchDelta& d, float v) { d.flt_release = v; }},
    // LFO 0
    {"lfo.0.waveform", [](mapper::PatchDelta& d, float v) { d.lfo0_waveform = lfoWaveformFromFloat(v); }},
    {"lfo.0.target", [](mapper::PatchDelta& d, float v) { d.lfo0_target = lfoTargetFromFloat(v); }},
    {"lfo.0.rate_hz", [](mapper::PatchDelta& d, float v) { d.lfo0_rate = v; }},
    {"lfo.0.depth", [](mapper::PatchDelta& d, float v) { d.lfo0_depth = v; }},
    {"lfo.0.phase_offset", [](mapper::PatchDelta& d, float v) { d.lfo0_phase_offset = v; }},
    {"lfo.0.bpm_sync", [](mapper::PatchDelta& d, float v) { d.lfo0_bpm_sync = boolFromFloat(v); }},
    // LFO 1
    {"lfo.1.waveform", [](mapper::PatchDelta& d, float v) { d.lfo1_waveform = lfoWaveformFromFloat(v); }},
    {"lfo.1.target", [](mapper::PatchDelta& d, float v) { d.lfo1_target = lfoTargetFromFloat(v); }},
    {"lfo.1.rate_hz", [](mapper::PatchDelta& d, float v) { d.lfo1_rate = v; }},
    {"lfo.1.depth", [](mapper::PatchDelta& d, float v) { d.lfo1_depth = v; }},
    {"lfo.1.phase_offset", [](mapper::PatchDelta& d, float v) { d.lfo1_phase_offset = v; }},
    {"lfo.1.bpm_sync", [](mapper::PatchDelta& d, float v) { d.lfo1_bpm_sync = boolFromFloat(v); }},
    // Reverb
    {"reverb.size", [](mapper::PatchDelta& d, float v) { d.reverb_size = v; }},
    {"reverb.damping", [](mapper::PatchDelta& d, float v) { d.reverb_damping = v; }},
    {"reverb.width", [](mapper::PatchDelta& d, float v) { d.reverb_width = v; }},
    {"reverb.mix", [](mapper::PatchDelta& d, float v) { d.reverb_mix = v; }},
    // Delay
    {"delay.time_s", [](mapper::PatchDelta& d, float v) { d.delay_time = v; }},
    {"delay.feedback", [](mapper::PatchDelta& d, float v) { d.delay_feedback = v; }},
    {"delay.mix", [](mapper::PatchDelta& d, float v) { d.delay_mix = v; }},
    {"delay.stereo", [](mapper::PatchDelta& d, float v) { d.delay_stereo = v; }},
    {"delay.bpm_sync", [](mapper::PatchDelta& d, float v) { d.delay_bpm_sync = boolFromFloat(v); }},
    // Global
    {"master_gain", [](mapper::PatchDelta& d, float v) { d.master_gain = v; }},
    {"portamento_s", [](mapper::PatchDelta& d, float v) { d.portamento = v; }},
    {"voice_count", [](mapper::PatchDelta& d, float v) { d.voice_count = voiceCountFromFloat(v); }},
    // Oscillators — paths are literal, so each (osc-index, field) gets its
    // own row. Previously parsed via std::stoi + nested if/else; the
    // try/catch was guarding that stoi.
    {"osc.0.type", [](mapper::PatchDelta& d, float v) { d.osc0_type = oscTypeFromFloat(v); }},
    {"osc.0.volume", [](mapper::PatchDelta& d, float v) { d.osc0_volume = v; }},
    {"osc.0.detune_cents", [](mapper::PatchDelta& d, float v) { d.osc0_detune = v; }},
    {"osc.0.semitone_offset", [](mapper::PatchDelta& d, float v) { d.osc0_semitone = v; }},
    {"osc.0.wavetable_pos", [](mapper::PatchDelta& d, float v) { d.osc0_wavetable_pos = v; }},
    {"osc.0.fm_ratio", [](mapper::PatchDelta& d, float v) { d.osc0_fm_ratio = v; }},
    {"osc.0.fm_depth", [](mapper::PatchDelta& d, float v) { d.osc0_fm_depth = v; }},
    {"osc.0.pan", [](mapper::PatchDelta& d, float v) { d.osc0_pan = v; }},
    {"osc.0.pulse_width", [](mapper::PatchDelta& d, float v) { d.osc0_pulse_width = v; }},
    {"osc.0.enabled", [](mapper::PatchDelta& d, float v) { d.osc0_enabled = boolFromFloat(v); }},
    {"osc.1.type", [](mapper::PatchDelta& d, float v) { d.osc1_type = oscTypeFromFloat(v); }},
    {"osc.1.volume", [](mapper::PatchDelta& d, float v) { d.osc1_volume = v; }},
    {"osc.1.detune_cents", [](mapper::PatchDelta& d, float v) { d.osc1_detune = v; }},
    {"osc.1.semitone_offset", [](mapper::PatchDelta& d, float v) { d.osc1_semitone = v; }},
    {"osc.1.wavetable_pos", [](mapper::PatchDelta& d, float v) { d.osc1_wavetable_pos = v; }},
    {"osc.1.fm_ratio", [](mapper::PatchDelta& d, float v) { d.osc1_fm_ratio = v; }},
    {"osc.1.fm_depth", [](mapper::PatchDelta& d, float v) { d.osc1_fm_depth = v; }},
    {"osc.1.pan", [](mapper::PatchDelta& d, float v) { d.osc1_pan = v; }},
    {"osc.1.pulse_width", [](mapper::PatchDelta& d, float v) { d.osc1_pulse_width = v; }},
    {"osc.1.enabled", [](mapper::PatchDelta& d, float v) { d.osc1_enabled = boolFromFloat(v); }},
    {"osc.2.type", [](mapper::PatchDelta& d, float v) { d.osc2_type = oscTypeFromFloat(v); }},
    {"osc.2.volume", [](mapper::PatchDelta& d, float v) { d.osc2_volume = v; }},
    {"osc.2.detune_cents", [](mapper::PatchDelta& d, float v) { d.osc2_detune = v; }},
    {"osc.2.semitone_offset", [](mapper::PatchDelta& d, float v) { d.osc2_semitone = v; }},
    {"osc.2.wavetable_pos", [](mapper::PatchDelta& d, float v) { d.osc2_wavetable_pos = v; }},
    {"osc.2.fm_ratio", [](mapper::PatchDelta& d, float v) { d.osc2_fm_ratio = v; }},
    {"osc.2.fm_depth", [](mapper::PatchDelta& d, float v) { d.osc2_fm_depth = v; }},
    {"osc.2.pan", [](mapper::PatchDelta& d, float v) { d.osc2_pan = v; }},
    {"osc.2.pulse_width", [](mapper::PatchDelta& d, float v) { d.osc2_pulse_width = v; }},
    {"osc.2.enabled", [](mapper::PatchDelta& d, float v) { d.osc2_enabled = boolFromFloat(v); }},
};

} // namespace

std::span<const ParamSlot> getParamMap() noexcept { return std::span<const ParamSlot>(kParamSlots); }

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
