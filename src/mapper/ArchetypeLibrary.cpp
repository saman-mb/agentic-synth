#include "mapper/ArchetypeLibrary.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <iostream>
#include <mutex>

#include "mapper/GrammarSampler.h"

namespace agentic_synth::mapper {

// ─── Embedded archetype JSON literals ───────────────────────────────────────
//
// One R"json(...)json" raw literal per archetype. Each must round-trip through
// stripTagsAndName + GrammarSampler::parse_patch_json, so the field order
// matches what parse_patch_json reads (version, patch_id, osc, filter,
// filter_env, amp_env, lfo, reverb, delay, master_gain, portamento_s,
// voice_count). The non-PatchStruct fields (`name`, `tags`) are stripped by
// the loader before parsing — see stripTagsAndName below.
//
// Values are hand-curated for §3 system-prompt fidelity + Phase 32's
// cinematic gold standard (positive env_mod, asymmetric detune, inharmonic
// FM anchor, cathedral verb mix 0.30..0.45).

namespace {

constexpr const char* kCinematicKubrickPad = R"json(
{
  "name": "cinematic_kubrick_pad",
  "tags": ["cinematic", "kubrick", "spooky", "evolving", "dark pad", "drone-pad", "2001", "vangelis", "horror"],
  "version": 1,
  "patch_id": 0,
  "osc": [
    {"type": "Sawtooth", "semitone_offset": -12.0, "detune_cents": -11.0, "wavetable_pos": 0.0, "fm_ratio": 1.0, "fm_depth": 0.0, "volume": 0.75, "pan": -0.6, "pulse_width": 0.5, "enabled": true},
    {"type": "Sawtooth", "semitone_offset": 0.0,   "detune_cents":  13.0, "wavetable_pos": 0.30, "fm_ratio": 1.0, "fm_depth": 0.0, "volume": 0.70, "pan": 0.6, "pulse_width": 0.5, "enabled": true},
    {"type": "FM",       "semitone_offset": -12.0, "detune_cents": 0.0, "wavetable_pos": 0.0, "fm_ratio": 2.73, "fm_depth": 0.35, "volume": 0.40, "pan": 0.0, "pulse_width": 0.5, "enabled": true}
  ],
  "filter": {"type": "LowPass", "cutoff_hz": 1400.0, "resonance": 0.35, "env_mod": 0.40, "key_track": 0.0, "drive": 0.35},
  "filter_env": {"attack_s": 1.8, "decay_s": 5.0, "sustain": 0.55, "release_s": 5.0},
  "amp_env":    {"attack_s": 2.2, "decay_s": 1.5, "sustain": 0.85, "release_s": 6.0},
  "lfo": [
    {"waveform": "Sine",     "target": "FilterCutoff", "rate_hz": 0.06, "depth": 0.55, "phase_offset": 0.0,  "bpm_sync": false},
    {"waveform": "Triangle", "target": "Pitch",        "rate_hz": 0.10, "depth": 0.04, "phase_offset": 0.25, "bpm_sync": false}
  ],
  "reverb": {"size": 0.92, "damping": 0.55, "width": 1.0, "mix": 0.42},
  "delay":  {"time_s": 0.5, "feedback": 0.25, "mix": 0.10, "stereo": 0.6, "bpm_sync": false},
  "master_gain": 0.85,
  "portamento_s": 0.0,
  "voice_count": 8
}
)json";

constexpr const char* kVangelisBladeRunnerPad = R"json(
{
  "name": "vangelis_blade_runner_pad",
  "tags": ["vangelis", "blade runner", "cinematic", "warm pad", "cs-80"],
  "version": 1,
  "patch_id": 0,
  "osc": [
    {"type": "Sawtooth", "semitone_offset": 0.0,   "detune_cents": -9.0, "wavetable_pos": 0.0, "fm_ratio": 1.0, "fm_depth": 0.0, "volume": 0.80, "pan": -0.5, "pulse_width": 0.5, "enabled": true},
    {"type": "Sawtooth", "semitone_offset": 0.0,   "detune_cents":  9.0, "wavetable_pos": 0.0, "fm_ratio": 1.0, "fm_depth": 0.0, "volume": 0.75, "pan":  0.5, "pulse_width": 0.5, "enabled": true},
    {"type": "Sine",     "semitone_offset": -12.0, "detune_cents":  0.0, "wavetable_pos": 0.0, "fm_ratio": 1.0, "fm_depth": 0.0, "volume": 0.45, "pan":  0.0, "pulse_width": 0.5, "enabled": true}
  ],
  "filter": {"type": "LowPass", "cutoff_hz": 900.0, "resonance": 0.30, "env_mod": 0.45, "key_track": 0.1, "drive": 0.25},
  "filter_env": {"attack_s": 2.0, "decay_s": 4.0, "sustain": 0.50, "release_s": 4.0},
  "amp_env":    {"attack_s": 2.0, "decay_s": 1.2, "sustain": 0.90, "release_s": 8.0},
  "lfo": [
    {"waveform": "Sine",     "target": "FilterCutoff", "rate_hz": 0.08, "depth": 0.30,  "phase_offset": 0.0,  "bpm_sync": false},
    {"waveform": "Triangle", "target": "Pitch",        "rate_hz": 0.13, "depth": 0.025, "phase_offset": 0.25, "bpm_sync": false}
  ],
  "reverb": {"size": 0.95, "damping": 0.50, "width": 1.0, "mix": 0.40},
  "delay":  {"time_s": 0.45, "feedback": 0.20, "mix": 0.08, "stereo": 0.6, "bpm_sync": false},
  "master_gain": 0.85,
  "portamento_s": 0.05,
  "voice_count": 8
}
)json";

constexpr const char* kAmbientDronePad = R"json(
{
  "name": "ambient_drone_pad",
  "tags": ["ambient", "drone", "evolving", "atmospheric", "soundscape", "ethereal"],
  "version": 1,
  "patch_id": 0,
  "osc": [
    {"type": "Wavetable", "semitone_offset": 0.0,   "detune_cents": -7.0, "wavetable_pos": 0.30, "fm_ratio": 1.0, "fm_depth": 0.0, "volume": 0.75, "pan": -0.4, "pulse_width": 0.5, "enabled": true},
    {"type": "Sawtooth",  "semitone_offset": 0.0,   "detune_cents":  7.0, "wavetable_pos": 0.0,  "fm_ratio": 1.0, "fm_depth": 0.0, "volume": 0.55, "pan":  0.4, "pulse_width": 0.5, "enabled": true},
    {"type": "Sine",      "semitone_offset": -12.0, "detune_cents":  0.0, "wavetable_pos": 0.0,  "fm_ratio": 1.0, "fm_depth": 0.0, "volume": 0.50, "pan":  0.0, "pulse_width": 0.5, "enabled": true}
  ],
  "filter": {"type": "LowPass", "cutoff_hz": 2200.0, "resonance": 0.20, "env_mod": 0.25, "key_track": 0.0, "drive": 0.15},
  "filter_env": {"attack_s": 3.0, "decay_s": 4.0, "sustain": 0.70, "release_s": 6.0},
  "amp_env":    {"attack_s": 3.5, "decay_s": 1.5, "sustain": 0.90, "release_s": 9.0},
  "lfo": [
    {"waveform": "Sine",     "target": "WavetablePos", "rate_hz": 0.05, "depth": 0.70, "phase_offset": 0.0,  "bpm_sync": false},
    {"waveform": "Triangle", "target": "FilterCutoff", "rate_hz": 0.09, "depth": 0.30, "phase_offset": 0.5,  "bpm_sync": false}
  ],
  "reverb": {"size": 0.95, "damping": 0.45, "width": 1.0, "mix": 0.45},
  "delay":  {"time_s": 0.75, "feedback": 0.35, "mix": 0.20, "stereo": 0.7, "bpm_sync": false},
  "master_gain": 0.80,
  "portamento_s": 0.0,
  "voice_count": 6
}
)json";

constexpr const char* kReeseDubstepBass = R"json(
{
  "name": "reese_dubstep_bass",
  "tags": ["dubstep", "reese", "bass", "wobble", "heavy", "deep bass"],
  "version": 1,
  "patch_id": 0,
  "osc": [
    {"type": "Sawtooth", "semitone_offset": 0.0,   "detune_cents": -15.0, "wavetable_pos": 0.0, "fm_ratio": 1.0, "fm_depth": 0.0, "volume": 0.85, "pan": -0.2, "pulse_width": 0.5, "enabled": true},
    {"type": "Sawtooth", "semitone_offset": 0.0,   "detune_cents":  15.0, "wavetable_pos": 0.0, "fm_ratio": 1.0, "fm_depth": 0.0, "volume": 0.85, "pan":  0.2, "pulse_width": 0.5, "enabled": true},
    {"type": "Sine",     "semitone_offset": -12.0, "detune_cents": 0.0,   "wavetable_pos": 0.0, "fm_ratio": 1.0, "fm_depth": 0.0, "volume": 0.65, "pan": 0.0, "pulse_width": 0.5, "enabled": true}
  ],
  "filter": {"type": "LowPass", "cutoff_hz": 350.0, "resonance": 0.50, "env_mod": 0.30, "key_track": 0.2, "drive": 0.50},
  "filter_env": {"attack_s": 0.01, "decay_s": 0.25, "sustain": 0.20, "release_s": 0.15},
  "amp_env":    {"attack_s": 0.005, "decay_s": 0.20, "sustain": 0.90, "release_s": 0.20},
  "lfo": [
    {"waveform": "Sine",     "target": "FilterCutoff", "rate_hz": 0.5, "depth": 0.70, "phase_offset": 0.0, "bpm_sync": true},
    {"waveform": "Triangle", "target": "None",         "rate_hz": 1.0, "depth": 0.0,  "phase_offset": 0.0, "bpm_sync": false}
  ],
  "reverb": {"size": 0.30, "damping": 0.50, "width": 0.5, "mix": 0.08},
  "delay":  {"time_s": 0.25, "feedback": 0.20, "mix": 0.05, "stereo": 0.3, "bpm_sync": false},
  "master_gain": 0.90,
  "portamento_s": 0.04,
  "voice_count": 2
}
)json";

constexpr const char* kSub808Bass = R"json(
{
  "name": "sub_808_bass",
  "tags": ["808", "sub", "trap", "deep sub", "pure sub"],
  "version": 1,
  "patch_id": 0,
  "osc": [
    {"type": "Sine", "semitone_offset": -12.0, "detune_cents": 0.0, "wavetable_pos": 0.0, "fm_ratio": 1.0, "fm_depth": 0.0, "volume": 1.0, "pan": 0.0, "pulse_width": 0.5, "enabled": true},
    {"type": "Sine", "semitone_offset": 0.0,   "detune_cents": 0.0, "wavetable_pos": 0.0, "fm_ratio": 1.0, "fm_depth": 0.0, "volume": 0.0, "pan": 0.0, "pulse_width": 0.5, "enabled": false},
    {"type": "Sine", "semitone_offset": 0.0,   "detune_cents": 0.0, "wavetable_pos": 0.0, "fm_ratio": 1.0, "fm_depth": 0.0, "volume": 0.0, "pan": 0.0, "pulse_width": 0.5, "enabled": false}
  ],
  "filter": {"type": "LowPass", "cutoff_hz": 18000.0, "resonance": 0.0, "env_mod": 0.0, "key_track": 0.0, "drive": 0.0},
  "filter_env": {"attack_s": 0.01, "decay_s": 0.10, "sustain": 0.0, "release_s": 0.10},
  "amp_env":    {"attack_s": 0.005, "decay_s": 0.50, "sustain": 0.80, "release_s": 0.30},
  "lfo": [
    {"waveform": "Sine", "target": "None", "rate_hz": 1.0, "depth": 0.0, "phase_offset": 0.0, "bpm_sync": false},
    {"waveform": "Sine", "target": "None", "rate_hz": 1.0, "depth": 0.0, "phase_offset": 0.0, "bpm_sync": false}
  ],
  "reverb": {"size": 0.0, "damping": 0.0, "width": 0.0, "mix": 0.0},
  "delay":  {"time_s": 0.25, "feedback": 0.0, "mix": 0.0, "stereo": 0.5, "bpm_sync": false},
  "master_gain": 0.95,
  "portamento_s": 0.06,
  "voice_count": 1
}
)json";

constexpr const char* kAcid303Bass = R"json(
{
  "name": "acid_303_bass",
  "tags": ["acid", "303", "acid bass", "squelch"],
  "version": 1,
  "patch_id": 0,
  "osc": [
    {"type": "Sawtooth", "semitone_offset": 0.0,   "detune_cents": 0.0, "wavetable_pos": 0.0, "fm_ratio": 1.0, "fm_depth": 0.0, "volume": 0.95, "pan": 0.0, "pulse_width": 0.5, "enabled": true},
    {"type": "Sawtooth", "semitone_offset": -12.0, "detune_cents": 0.0, "wavetable_pos": 0.0, "fm_ratio": 1.0, "fm_depth": 0.0, "volume": 0.35, "pan": 0.0, "pulse_width": 0.5, "enabled": true},
    {"type": "Sine",     "semitone_offset": -12.0, "detune_cents": 0.0, "wavetable_pos": 0.0, "fm_ratio": 1.0, "fm_depth": 0.0, "volume": 0.30, "pan": 0.0, "pulse_width": 0.5, "enabled": true}
  ],
  "filter": {"type": "LowPass", "cutoff_hz": 400.0, "resonance": 0.85, "env_mod": 0.85, "key_track": 0.40, "drive": 0.50},
  "filter_env": {"attack_s": 0.001, "decay_s": 0.15, "sustain": 0.0, "release_s": 0.08},
  "amp_env":    {"attack_s": 0.001, "decay_s": 0.10, "sustain": 0.60, "release_s": 0.10},
  "lfo": [
    {"waveform": "Sine", "target": "None", "rate_hz": 1.0, "depth": 0.0, "phase_offset": 0.0, "bpm_sync": false},
    {"waveform": "Sine", "target": "None", "rate_hz": 1.0, "depth": 0.0, "phase_offset": 0.0, "bpm_sync": false}
  ],
  "reverb": {"size": 0.20, "damping": 0.50, "width": 0.5, "mix": 0.08},
  "delay":  {"time_s": 0.25, "feedback": 0.20, "mix": 0.10, "stereo": 0.3, "bpm_sync": false},
  "master_gain": 0.90,
  "portamento_s": 0.08,
  "voice_count": 1
}
)json";

constexpr const char* kSupersawLead = R"json(
{
  "name": "supersaw_lead",
  "tags": ["supersaw", "lead", "trance", "bright lead"],
  "version": 1,
  "patch_id": 0,
  "osc": [
    {"type": "Sawtooth", "semitone_offset": 0.0, "detune_cents":   0.0, "wavetable_pos": 0.0, "fm_ratio": 1.0, "fm_depth": 0.0, "volume": 0.80, "pan":  0.0, "pulse_width": 0.5, "enabled": true},
    {"type": "Sawtooth", "semitone_offset": 0.0, "detune_cents": -15.0, "wavetable_pos": 0.0, "fm_ratio": 1.0, "fm_depth": 0.0, "volume": 0.75, "pan": -0.5, "pulse_width": 0.5, "enabled": true},
    {"type": "Sawtooth", "semitone_offset": 0.0, "detune_cents":  15.0, "wavetable_pos": 0.0, "fm_ratio": 1.0, "fm_depth": 0.0, "volume": 0.75, "pan":  0.5, "pulse_width": 0.5, "enabled": true}
  ],
  "filter": {"type": "LowPass", "cutoff_hz": 4500.0, "resonance": 0.30, "env_mod": 0.30, "key_track": 0.2, "drive": 0.15},
  "filter_env": {"attack_s": 0.01, "decay_s": 0.25, "sustain": 0.40, "release_s": 0.20},
  "amp_env":    {"attack_s": 0.005, "decay_s": 0.20, "sustain": 0.75, "release_s": 0.30},
  "lfo": [
    {"waveform": "Sine", "target": "Pitch", "rate_hz": 5.0, "depth": 0.02, "phase_offset": 0.0, "bpm_sync": false},
    {"waveform": "Sine", "target": "None",  "rate_hz": 1.0, "depth": 0.0,  "phase_offset": 0.0, "bpm_sync": false}
  ],
  "reverb": {"size": 0.45, "damping": 0.50, "width": 1.0, "mix": 0.20},
  "delay":  {"time_s": 0.375, "feedback": 0.40, "mix": 0.25, "stereo": 0.7, "bpm_sync": true},
  "master_gain": 0.85,
  "portamento_s": 0.04,
  "voice_count": 1
}
)json";

constexpr const char* kDx7TineEp = R"json(
{
  "name": "dx7_tine_ep",
  "tags": ["fm", "dx", "dx7", "tine", "rhodes", "electric piano", "ep"],
  "version": 1,
  "patch_id": 0,
  "osc": [
    {"type": "FM",   "semitone_offset": 0.0,  "detune_cents": 0.0, "wavetable_pos": 0.0, "fm_ratio": 14.0, "fm_depth": 0.55, "volume": 0.85, "pan": 0.0, "pulse_width": 0.5, "enabled": true},
    {"type": "Sine", "semitone_offset": 0.0,  "detune_cents": 0.0, "wavetable_pos": 0.0, "fm_ratio": 1.0,  "fm_depth": 0.0,  "volume": 0.55, "pan": 0.0, "pulse_width": 0.5, "enabled": true},
    {"type": "Sine", "semitone_offset": 12.0, "detune_cents": 0.0, "wavetable_pos": 0.0, "fm_ratio": 1.0,  "fm_depth": 0.0,  "volume": 0.20, "pan": 0.0, "pulse_width": 0.5, "enabled": true}
  ],
  "filter": {"type": "LowPass", "cutoff_hz": 14000.0, "resonance": 0.10, "env_mod": 0.10, "key_track": 0.2, "drive": 0.0},
  "filter_env": {"attack_s": 0.001, "decay_s": 0.30, "sustain": 0.10, "release_s": 0.20},
  "amp_env":    {"attack_s": 0.001, "decay_s": 0.40, "sustain": 0.40, "release_s": 0.50},
  "lfo": [
    {"waveform": "Sine", "target": "None", "rate_hz": 1.0, "depth": 0.0, "phase_offset": 0.0, "bpm_sync": false},
    {"waveform": "Sine", "target": "None", "rate_hz": 1.0, "depth": 0.0, "phase_offset": 0.0, "bpm_sync": false}
  ],
  "reverb": {"size": 0.55, "damping": 0.50, "width": 1.0, "mix": 0.25},
  "delay":  {"time_s": 0.25, "feedback": 0.20, "mix": 0.08, "stereo": 0.5, "bpm_sync": false},
  "master_gain": 0.85,
  "portamento_s": 0.0,
  "voice_count": 8
}
)json";

constexpr const char* kGlassBell = R"json(
{
  "name": "glass_bell",
  "tags": ["bell", "glass", "chime", "fm bell"],
  "version": 1,
  "patch_id": 0,
  "osc": [
    {"type": "FM",   "semitone_offset": 0.0,  "detune_cents": 0.0, "wavetable_pos": 0.0, "fm_ratio": 3.14, "fm_depth": 0.45, "volume": 0.85, "pan": 0.0, "pulse_width": 0.5, "enabled": true},
    {"type": "Sine", "semitone_offset": 0.0,  "detune_cents": 0.0, "wavetable_pos": 0.0, "fm_ratio": 1.0,  "fm_depth": 0.0,  "volume": 0.55, "pan": 0.0, "pulse_width": 0.5, "enabled": true},
    {"type": "Sine", "semitone_offset": 19.0, "detune_cents": 0.0, "wavetable_pos": 0.0, "fm_ratio": 1.0,  "fm_depth": 0.0,  "volume": 0.30, "pan": 0.0, "pulse_width": 0.5, "enabled": true}
  ],
  "filter": {"type": "LowPass", "cutoff_hz": 8000.0, "resonance": 0.10, "env_mod": 0.20, "key_track": 0.2, "drive": 0.0},
  "filter_env": {"attack_s": 0.001, "decay_s": 0.50, "sustain": 0.10, "release_s": 0.50},
  "amp_env":    {"attack_s": 0.001, "decay_s": 1.50, "sustain": 0.0,  "release_s": 1.50},
  "lfo": [
    {"waveform": "Sine", "target": "None", "rate_hz": 1.0, "depth": 0.0, "phase_offset": 0.0, "bpm_sync": false},
    {"waveform": "Sine", "target": "None", "rate_hz": 1.0, "depth": 0.0, "phase_offset": 0.0, "bpm_sync": false}
  ],
  "reverb": {"size": 0.75, "damping": 0.45, "width": 1.0, "mix": 0.55},
  "delay":  {"time_s": 0.5, "feedback": 0.30, "mix": 0.20, "stereo": 0.6, "bpm_sync": false},
  "master_gain": 0.80,
  "portamento_s": 0.0,
  "voice_count": 8
}
)json";

constexpr const char* kWarmAnalogPad = R"json(
{
  "name": "warm_analog_pad",
  "tags": ["warm pad", "lush pad", "soft pad", "analog pad"],
  "version": 1,
  "patch_id": 0,
  "osc": [
    {"type": "Triangle", "semitone_offset": 0.0,  "detune_cents": -9.0, "wavetable_pos": 0.0, "fm_ratio": 1.0, "fm_depth": 0.0, "volume": 0.80, "pan": -0.4, "pulse_width": 0.5, "enabled": true},
    {"type": "Triangle", "semitone_offset": 0.0,  "detune_cents":  9.0, "wavetable_pos": 0.0, "fm_ratio": 1.0, "fm_depth": 0.0, "volume": 0.75, "pan":  0.4, "pulse_width": 0.5, "enabled": true},
    {"type": "Sine",     "semitone_offset": 12.0, "detune_cents":  0.0, "wavetable_pos": 0.0, "fm_ratio": 1.0, "fm_depth": 0.0, "volume": 0.40, "pan":  0.0, "pulse_width": 0.5, "enabled": true}
  ],
  "filter": {"type": "LowPass", "cutoff_hz": 2500.0, "resonance": 0.15, "env_mod": 0.25, "key_track": 0.1, "drive": 0.10},
  "filter_env": {"attack_s": 1.5, "decay_s": 2.0, "sustain": 0.60, "release_s": 2.5},
  "amp_env":    {"attack_s": 1.2, "decay_s": 1.0, "sustain": 0.85, "release_s": 3.0},
  "lfo": [
    {"waveform": "Sine", "target": "FilterCutoff", "rate_hz": 0.20, "depth": 0.20, "phase_offset": 0.0, "bpm_sync": false},
    {"waveform": "Sine", "target": "None",         "rate_hz": 1.0,  "depth": 0.0,  "phase_offset": 0.0, "bpm_sync": false}
  ],
  "reverb": {"size": 0.70, "damping": 0.50, "width": 1.0, "mix": 0.35},
  "delay":  {"time_s": 0.375, "feedback": 0.25, "mix": 0.12, "stereo": 0.6, "bpm_sync": false},
  "master_gain": 0.85,
  "portamento_s": 0.0,
  "voice_count": 8
}
)json";

constexpr const char* kPluckKeys = R"json(
{
  "name": "pluck_keys",
  "tags": ["pluck", "keys", "harp", "plucky"],
  "version": 1,
  "patch_id": 0,
  "osc": [
    {"type": "Sawtooth", "semitone_offset": 0.0,   "detune_cents":  0.0, "wavetable_pos": 0.0, "fm_ratio": 1.0, "fm_depth": 0.0, "volume": 0.80, "pan":  0.0, "pulse_width": 0.5, "enabled": true},
    {"type": "Triangle", "semitone_offset": -12.0, "detune_cents":  0.0, "wavetable_pos": 0.0, "fm_ratio": 1.0, "fm_depth": 0.0, "volume": 0.45, "pan":  0.0, "pulse_width": 0.5, "enabled": true},
    {"type": "Sine",     "semitone_offset": 12.0,  "detune_cents":  0.0, "wavetable_pos": 0.0, "fm_ratio": 1.0, "fm_depth": 0.0, "volume": 0.25, "pan":  0.0, "pulse_width": 0.5, "enabled": true}
  ],
  "filter": {"type": "LowPass", "cutoff_hz": 1800.0, "resonance": 0.30, "env_mod": 0.60, "key_track": 0.3, "drive": 0.05},
  "filter_env": {"attack_s": 0.001, "decay_s": 0.18, "sustain": 0.0, "release_s": 0.10},
  "amp_env":    {"attack_s": 0.001, "decay_s": 0.30, "sustain": 0.0, "release_s": 0.20},
  "lfo": [
    {"waveform": "Sine", "target": "None", "rate_hz": 1.0, "depth": 0.0, "phase_offset": 0.0, "bpm_sync": false},
    {"waveform": "Sine", "target": "None", "rate_hz": 1.0, "depth": 0.0, "phase_offset": 0.0, "bpm_sync": false}
  ],
  "reverb": {"size": 0.40, "damping": 0.55, "width": 1.0, "mix": 0.20},
  "delay":  {"time_s": 0.25, "feedback": 0.25, "mix": 0.15, "stereo": 0.5, "bpm_sync": false},
  "master_gain": 0.90,
  "portamento_s": 0.0,
  "voice_count": 8
}
)json";

constexpr const char* kGrittyLead = R"json(
{
  "name": "gritty_lead",
  "tags": ["gritty", "snarl", "growl", "aggressive lead", "neuro"],
  "version": 1,
  "patch_id": 0,
  "osc": [
    {"type": "Sawtooth", "semitone_offset": 0.0,   "detune_cents":  -8.0, "wavetable_pos": 0.0, "fm_ratio": 1.0, "fm_depth": 0.0, "volume": 0.85, "pan": -0.3, "pulse_width": 0.5, "enabled": true},
    {"type": "Square",   "semitone_offset": 0.0,   "detune_cents":   8.0, "wavetable_pos": 0.0, "fm_ratio": 1.0, "fm_depth": 0.0, "volume": 0.55, "pan":  0.3, "pulse_width": 0.45, "enabled": true},
    {"type": "Sine",     "semitone_offset": -12.0, "detune_cents":   0.0, "wavetable_pos": 0.0, "fm_ratio": 1.0, "fm_depth": 0.0, "volume": 0.45, "pan":  0.0, "pulse_width": 0.5,  "enabled": true}
  ],
  "filter": {"type": "LowPass", "cutoff_hz": 1200.0, "resonance": 0.55, "env_mod": 0.55, "key_track": 0.2, "drive": 0.50},
  "filter_env": {"attack_s": 0.005, "decay_s": 0.25, "sustain": 0.40, "release_s": 0.20},
  "amp_env":    {"attack_s": 0.005, "decay_s": 0.20, "sustain": 0.80, "release_s": 0.25},
  "lfo": [
    {"waveform": "Triangle", "target": "FilterCutoff", "rate_hz": 0.25, "depth": 0.30, "phase_offset": 0.0, "bpm_sync": true},
    {"waveform": "Sine",     "target": "Pitch",        "rate_hz": 5.0,  "depth": 0.02, "phase_offset": 0.0, "bpm_sync": false}
  ],
  "reverb": {"size": 0.40, "damping": 0.55, "width": 0.7, "mix": 0.15},
  "delay":  {"time_s": 0.375, "feedback": 0.35, "mix": 0.20, "stereo": 0.6, "bpm_sync": true},
  "master_gain": 0.88,
  "portamento_s": 0.03,
  "voice_count": 1
}
)json";

constexpr const char* kEtherealChoirPad = R"json(
{
  "name": "ethereal_choir_pad",
  "tags": ["choir", "vocal", "angelic", "ethereal", "heavenly"],
  "version": 1,
  "patch_id": 0,
  "osc": [
    {"type": "Wavetable", "semitone_offset": 0.0,  "detune_cents": -5.0, "wavetable_pos": 0.55, "fm_ratio": 1.0, "fm_depth": 0.0, "volume": 0.80, "pan": -0.4, "pulse_width": 0.5, "enabled": true},
    {"type": "Sine",      "semitone_offset": 12.0, "detune_cents":  0.0, "wavetable_pos": 0.0,  "fm_ratio": 1.0, "fm_depth": 0.0, "volume": 0.45, "pan":  0.4, "pulse_width": 0.5, "enabled": true},
    {"type": "Noise",     "semitone_offset": 0.0,  "detune_cents":  0.0, "wavetable_pos": 0.0,  "fm_ratio": 1.0, "fm_depth": 0.0, "volume": 0.18, "pan":  0.0, "pulse_width": 0.5, "enabled": true}
  ],
  "filter": {"type": "LowPass", "cutoff_hz": 3500.0, "resonance": 0.20, "env_mod": 0.30, "key_track": 0.1, "drive": 0.05},
  "filter_env": {"attack_s": 2.0, "decay_s": 3.0, "sustain": 0.70, "release_s": 4.0},
  "amp_env":    {"attack_s": 2.5, "decay_s": 1.5, "sustain": 0.90, "release_s": 5.0},
  "lfo": [
    {"waveform": "Sine",     "target": "WavetablePos", "rate_hz": 0.12, "depth": 0.40, "phase_offset": 0.0, "bpm_sync": false},
    {"waveform": "Triangle", "target": "Pitch",        "rate_hz": 0.18, "depth": 0.02, "phase_offset": 0.5, "bpm_sync": false}
  ],
  "reverb": {"size": 0.90, "damping": 0.45, "width": 1.0, "mix": 0.50},
  "delay":  {"time_s": 0.5, "feedback": 0.30, "mix": 0.15, "stereo": 0.7, "bpm_sync": false},
  "master_gain": 0.80,
  "portamento_s": 0.0,
  "voice_count": 8
}
)json";

constexpr const char* kRiserSwell = R"json(
{
  "name": "riser_swell",
  "tags": ["riser", "swell", "build-up", "transition"],
  "version": 1,
  "patch_id": 0,
  "osc": [
    {"type": "Sawtooth", "semitone_offset": 0.0,   "detune_cents":  0.0, "wavetable_pos": 0.0, "fm_ratio": 1.0, "fm_depth": 0.0, "volume": 0.85, "pan": 0.0, "pulse_width": 0.5, "enabled": true},
    {"type": "Sawtooth", "semitone_offset": 12.0,  "detune_cents":  7.0, "wavetable_pos": 0.0, "fm_ratio": 1.0, "fm_depth": 0.0, "volume": 0.60, "pan": -0.3, "pulse_width": 0.5, "enabled": true},
    {"type": "Noise",    "semitone_offset": 0.0,   "detune_cents":  0.0, "wavetable_pos": 0.0, "fm_ratio": 1.0, "fm_depth": 0.0, "volume": 0.30, "pan": 0.3,  "pulse_width": 0.5, "enabled": true}
  ],
  "filter": {"type": "LowPass", "cutoff_hz": 80.0, "resonance": 0.40, "env_mod": 1.0, "key_track": 0.0, "drive": 0.20},
  "filter_env": {"attack_s": 4.0, "decay_s": 0.10, "sustain": 1.0, "release_s": 0.50},
  "amp_env":    {"attack_s": 4.0, "decay_s": 0.10, "sustain": 1.0, "release_s": 0.50},
  "lfo": [
    {"waveform": "Sine", "target": "Pitch", "rate_hz": 0.20, "depth": 0.10, "phase_offset": 0.0, "bpm_sync": false},
    {"waveform": "Sine", "target": "None",  "rate_hz": 1.0,  "depth": 0.0,  "phase_offset": 0.0, "bpm_sync": false}
  ],
  "reverb": {"size": 0.65, "damping": 0.50, "width": 1.0, "mix": 0.30},
  "delay":  {"time_s": 0.5, "feedback": 0.50, "mix": 0.30, "stereo": 0.7, "bpm_sync": true},
  "master_gain": 0.85,
  "portamento_s": 0.0,
  "voice_count": 1
}
)json";

constexpr const char* kDefaultInit = R"json(
{
  "name": "default_init",
  "tags": ["default", "init", "blank"],
  "version": 1,
  "patch_id": 0,
  "osc": [
    {"type": "Sawtooth", "semitone_offset": 0.0, "detune_cents": 0.0, "wavetable_pos": 0.0, "fm_ratio": 1.0, "fm_depth": 0.0, "volume": 1.0, "pan": 0.0, "pulse_width": 0.5, "enabled": true},
    {"type": "Sine",     "semitone_offset": 0.0, "detune_cents": 0.0, "wavetable_pos": 0.0, "fm_ratio": 1.0, "fm_depth": 0.0, "volume": 0.0, "pan": 0.0, "pulse_width": 0.5, "enabled": false},
    {"type": "Sine",     "semitone_offset": 0.0, "detune_cents": 0.0, "wavetable_pos": 0.0, "fm_ratio": 1.0, "fm_depth": 0.0, "volume": 0.0, "pan": 0.0, "pulse_width": 0.5, "enabled": false}
  ],
  "filter": {"type": "LowPass", "cutoff_hz": 18000.0, "resonance": 0.0, "env_mod": 0.0, "key_track": 0.0, "drive": 0.0},
  "filter_env": {"attack_s": 0.01, "decay_s": 0.20, "sustain": 0.0, "release_s": 0.10},
  "amp_env":    {"attack_s": 0.005, "decay_s": 0.10, "sustain": 1.0, "release_s": 0.10},
  "lfo": [
    {"waveform": "Sine", "target": "None", "rate_hz": 1.0, "depth": 0.0, "phase_offset": 0.0, "bpm_sync": false},
    {"waveform": "Sine", "target": "None", "rate_hz": 1.0, "depth": 0.0, "phase_offset": 0.0, "bpm_sync": false}
  ],
  "reverb": {"size": 0.0, "damping": 0.0, "width": 1.0, "mix": 0.0},
  "delay":  {"time_s": 0.25, "feedback": 0.0, "mix": 0.0, "stereo": 0.5, "bpm_sync": false},
  "master_gain": 1.0,
  "portamento_s": 0.0,
  "voice_count": 8
}
)json";

// All archetype source literals — order is the canonical library order.
constexpr const char* kAllArchetypeJson[] = {
    kCinematicKubrickPad,
    kVangelisBladeRunnerPad,
    kAmbientDronePad,
    kReeseDubstepBass,
    kSub808Bass,
    kAcid303Bass,
    kSupersawLead,
    kDx7TineEp,
    kGlassBell,
    kWarmAnalogPad,
    kPluckKeys,
    kGrittyLead,
    kEtherealChoirPad,
    kRiserSwell,
    kDefaultInit,
};

// ─── Loader helpers ─────────────────────────────────────────────────────────
//
// PatchStruct JSON parsing is shared with the LLM pipeline
// (GrammarSampler::parse_patch_json). The only structural difference is the
// extra top-level `name` + `tags` fields the loader carries — those are
// stripped from the input string before parse_patch_json sees it. The order
// the parser expects begins with "version": that's the splice point we use
// to keep the rest of the input byte-identical to what the grammar would
// emit.

// Skips ASCII whitespace.
const char* skipWs(const char* p, const char* end) noexcept {
    while (p < end && std::isspace(static_cast<unsigned char>(*p)))
        ++p;
    return p;
}

// Reads a top-level JSON-style string into out. Pointer p must be at the
// opening quote. Returns the position past the closing quote, or nullptr on
// error. Only used at load time on hand-curated literals — no need for full
// JSON escape support (the literals don't include escaped quotes inside
// names/tags).
const char* readJsonString(const char* p, const char* end, std::string& out) {
    if (p >= end || *p != '"')
        return nullptr;
    ++p;
    out.clear();
    while (p < end && *p != '"') {
        if (*p == '\\' && p + 1 < end) {
            out += *(p + 1);
            p += 2;
        } else {
            out += *p++;
        }
    }
    if (p >= end)
        return nullptr;
    return p + 1; // past the closing quote
}

// Parse the `name` and `tags` fields from a top-level JSON object literal +
// produce a transformed JSON string with those two fields removed (so what
// remains is a clean PatchStruct JSON ready for GrammarSampler).
//
// We don't run a full JSON parser — the input is hand-authored and the two
// fields always appear before "version" in the canonical order. A targeted
// extraction is safer and faster than re-implementing JSON.
struct StripResult {
    std::string name;
    std::vector<std::string> tags;
    std::string patchJson;
};

bool stripTagsAndName(const std::string& source, StripResult& result) {
    const char* p = source.data();
    const char* end = p + source.size();

    p = skipWs(p, end);
    if (p >= end || *p++ != '{')
        return false;

    // Skip until "name":, then read the string value.
    auto findKey = [&](std::string_view key) -> const char* {
        const std::string pattern = "\"" + std::string(key) + "\"";
        const char* hit = std::search(p, end, pattern.begin(), pattern.end());
        if (hit == end)
            return nullptr;
        const char* after = hit + pattern.size();
        after = skipWs(after, end);
        if (after >= end || *after != ':')
            return nullptr;
        return skipWs(after + 1, end);
    };

    // name
    const char* nameVal = findKey("name");
    if (!nameVal)
        return false;
    const char* afterName = readJsonString(nameVal, end, result.name);
    if (!afterName)
        return false;

    // tags — array of strings
    const char* tagsVal = findKey("tags");
    if (!tagsVal || *tagsVal != '[')
        return false;
    const char* tagP = tagsVal + 1;
    tagP = skipWs(tagP, end);
    while (tagP < end && *tagP != ']') {
        std::string t;
        const char* nextP = readJsonString(tagP, end, t);
        if (!nextP)
            return false;
        result.tags.push_back(std::move(t));
        tagP = skipWs(nextP, end);
        if (tagP < end && *tagP == ',')
            tagP = skipWs(tagP + 1, end);
    }
    if (tagP >= end)
        return false;
    const char* afterTags = tagP + 1; // past ']'

    // The patch body starts at the next key after `tags` — find "version".
    p = afterTags;
    p = skipWs(p, end);
    if (p < end && *p == ',')
        p = skipWs(p + 1, end);

    // The remainder of the source from "version" through the final '}' is
    // the patch JSON. Wrap it in '{' + remainder so parse_patch_json sees
    // the same shape it expects.
    if (p >= end)
        return false;
    result.patchJson = "{";
    result.patchJson.append(p, static_cast<size_t>(end - p));
    return true;
}

std::vector<Archetype> buildLibrary() {
    std::vector<Archetype> library;
    library.reserve(std::size(kAllArchetypeJson));
    for (const char* json : kAllArchetypeJson) {
        StripResult sr;
        if (!stripTagsAndName(json, sr)) {
            std::cerr << "[ArchetypeLibrary] Failed to parse archetype "
                         "(name+tags extraction)\n";
            continue;
        }
        auto parsed = GrammarSampler::parse_patch_json(sr.patchJson);
        if (!parsed) {
            std::cerr << "[ArchetypeLibrary] Failed to parse PatchStruct for "
                         "archetype '" << sr.name << "'\n";
            continue;
        }
        Archetype a;
        a.name = std::move(sr.name);
        a.tags = std::move(sr.tags);
        a.patch = *parsed;
        library.push_back(std::move(a));
    }
    return library;
}

} // namespace

const std::vector<Archetype>& ArchetypeLibrary::all() {
    static const std::vector<Archetype> kLibrary = buildLibrary();
    return kLibrary;
}

const Archetype* ArchetypeLibrary::byName(const std::string& name) {
    const auto& lib = all();
    for (const auto& a : lib)
        if (a.name == name)
            return &a;
    return nullptr;
}

} // namespace agentic_synth::mapper
