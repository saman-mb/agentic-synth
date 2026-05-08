#pragma once

#include <array>
#include <optional>
#include <string_view>

#include "engine/PatchStruct.h"

namespace agentic_synth::mapper {

// ---------------------------------------------------------------------------
// PatchDelta — sparse set of overrides applied to a base PatchStruct.
// std::nullopt means "leave unchanged".
// ---------------------------------------------------------------------------

struct PatchDelta {
    // Oscillator 0
    std::optional<OscType> osc0_type;
    std::optional<float> osc0_semitone; // semitones, -48..+48
    std::optional<float> osc0_detune;   // cents
    std::optional<float> osc0_volume;   // 0..1
    std::optional<float> osc0_fm_ratio;
    std::optional<float> osc0_fm_depth;
    std::optional<float> osc0_pulse_width;
    // Oscillator 1 (supersaw/unison layer)
    std::optional<bool> osc1_enabled;
    std::optional<float> osc1_detune;
    std::optional<float> osc1_volume;
    // Filter
    std::optional<FilterType> filter_type;
    std::optional<float> filter_cutoff;    // Hz
    std::optional<float> filter_resonance; // 0..1
    std::optional<float> filter_drive;     // 0..1
    std::optional<float> filter_env_mod;   // -1..+1
    // Amp envelope
    std::optional<float> amp_attack;
    std::optional<float> amp_decay;
    std::optional<float> amp_sustain;
    std::optional<float> amp_release;
    // Filter envelope
    std::optional<float> flt_attack;
    std::optional<float> flt_decay;
    std::optional<float> flt_sustain;
    std::optional<float> flt_release;
    // LFO 0
    std::optional<LfoWaveform> lfo0_waveform;
    std::optional<LfoTarget> lfo0_target;
    std::optional<float> lfo0_rate;
    std::optional<float> lfo0_depth;
    // Reverb
    std::optional<float> reverb_size;
    std::optional<float> reverb_damping;
    std::optional<float> reverb_width;
    std::optional<float> reverb_mix;
    // Delay
    std::optional<float> delay_time;
    std::optional<float> delay_feedback;
    std::optional<float> delay_mix;
    // Global
    std::optional<float> master_gain;
    std::optional<float> portamento;
    std::optional<uint8_t> voice_count;
};

// Apply delta to patch in-place. Only set fields are written.
inline void apply_delta(PatchStruct& p, const PatchDelta& d) noexcept {
    if (d.osc0_type)
        p.osc[0].type = *d.osc0_type;
    if (d.osc0_semitone)
        p.osc[0].semitone_offset = *d.osc0_semitone;
    if (d.osc0_detune)
        p.osc[0].detune_cents = *d.osc0_detune;
    if (d.osc0_volume)
        p.osc[0].volume = *d.osc0_volume;
    if (d.osc0_fm_ratio)
        p.osc[0].fm_ratio = *d.osc0_fm_ratio;
    if (d.osc0_fm_depth)
        p.osc[0].fm_depth = *d.osc0_fm_depth;
    if (d.osc0_pulse_width)
        p.osc[0].pulse_width = *d.osc0_pulse_width;
    if (d.osc1_enabled)
        p.osc[1].enabled = *d.osc1_enabled ? 1u : 0u;
    if (d.osc1_detune)
        p.osc[1].detune_cents = *d.osc1_detune;
    if (d.osc1_volume)
        p.osc[1].volume = *d.osc1_volume;
    if (d.filter_type)
        p.filter.type = *d.filter_type;
    if (d.filter_cutoff)
        p.filter.cutoff_hz = *d.filter_cutoff;
    if (d.filter_resonance)
        p.filter.resonance = *d.filter_resonance;
    if (d.filter_drive)
        p.filter.drive = *d.filter_drive;
    if (d.filter_env_mod)
        p.filter.env_mod = *d.filter_env_mod;
    if (d.amp_attack)
        p.amp_env.attack_s = *d.amp_attack;
    if (d.amp_decay)
        p.amp_env.decay_s = *d.amp_decay;
    if (d.amp_sustain)
        p.amp_env.sustain = *d.amp_sustain;
    if (d.amp_release)
        p.amp_env.release_s = *d.amp_release;
    if (d.flt_attack)
        p.filter_env.attack_s = *d.flt_attack;
    if (d.flt_decay)
        p.filter_env.decay_s = *d.flt_decay;
    if (d.flt_sustain)
        p.filter_env.sustain = *d.flt_sustain;
    if (d.flt_release)
        p.filter_env.release_s = *d.flt_release;
    if (d.lfo0_waveform)
        p.lfo[0].waveform = *d.lfo0_waveform;
    if (d.lfo0_target)
        p.lfo[0].target = *d.lfo0_target;
    if (d.lfo0_rate)
        p.lfo[0].rate_hz = *d.lfo0_rate;
    if (d.lfo0_depth)
        p.lfo[0].depth = *d.lfo0_depth;
    if (d.reverb_size)
        p.reverb.size = *d.reverb_size;
    if (d.reverb_damping)
        p.reverb.damping = *d.reverb_damping;
    if (d.reverb_width)
        p.reverb.width = *d.reverb_width;
    if (d.reverb_mix)
        p.reverb.mix = *d.reverb_mix;
    if (d.delay_time)
        p.delay.time_s = *d.delay_time;
    if (d.delay_feedback)
        p.delay.feedback = *d.delay_feedback;
    if (d.delay_mix)
        p.delay.mix = *d.delay_mix;
    if (d.master_gain)
        p.master_gain = *d.master_gain;
    if (d.portamento)
        p.portamento_s = *d.portamento;
    if (d.voice_count)
        p.voice_count = *d.voice_count;
}

// ---------------------------------------------------------------------------
// Context categories for context-aware delta selection
// ---------------------------------------------------------------------------

enum class SoundContext : uint8_t {
    Generic = 0,
    Bass,
    Pad,
    Lead,
    Keys,
    Percussion,
    Arp,
    Texture,
};

// ---------------------------------------------------------------------------
// DescriptorEntry — one row in the lookup table
// ---------------------------------------------------------------------------

struct DescriptorEntry {
    std::string_view keyword;
    SoundContext context; // Generic = applies to any context
    PatchDelta delta;
};

// ---------------------------------------------------------------------------
// Dataset — curated (descriptor, context, delta) triples
// Descriptors are canonical lowercase forms; SemanticMapper resolves synonyms.
// ---------------------------------------------------------------------------

// clang-format off
inline const std::array<DescriptorEntry, 74>& get_descriptor_dataset() {
    static const std::array<DescriptorEntry, 74> data{{

        // ── Brightness / Tone ─────────────────────────────────────────────────
        {"dark",    SoundContext::Generic,    {.filter_cutoff=400.0f,   .filter_resonance=0.05f}},
        {"dark",    SoundContext::Bass,       {.filter_cutoff=250.0f,   .filter_resonance=0.1f,  .osc0_type=OscType::Sawtooth}},
        {"dark",    SoundContext::Pad,        {.filter_cutoff=600.0f,   .amp_attack=0.4f,         .reverb_mix=0.4f}},
        {"bright",  SoundContext::Generic,    {.filter_cutoff=8000.0f,  .filter_resonance=0.15f}},
        {"bright",  SoundContext::Lead,       {.osc0_type=OscType::Sawtooth, .filter_cutoff=6000.0f, .filter_resonance=0.3f}},
        {"warm",    SoundContext::Generic,    {.osc0_type=OscType::Triangle, .filter_cutoff=2000.0f, .filter_resonance=0.08f}},
        {"cold",    SoundContext::Generic,    {.osc0_type=OscType::Square,   .filter_cutoff=5000.0f, .filter_resonance=0.65f}},
        {"icy",     SoundContext::Generic,    {.filter_cutoff=6000.0f,  .filter_resonance=0.75f, .reverb_mix=0.3f}},
        {"mellow",  SoundContext::Generic,    {.filter_cutoff=1200.0f,  .filter_resonance=0.05f, .amp_attack=0.05f}},
        {"harsh",   SoundContext::Generic,    {.filter_cutoff=7000.0f,  .filter_resonance=0.8f,  .filter_drive=0.5f}},
        {"smooth",  SoundContext::Generic,    {.filter_cutoff=3000.0f,  .filter_resonance=0.0f,  .filter_drive=0.0f}},
        {"gritty",  SoundContext::Generic,    {.filter_drive=0.65f,     .filter_resonance=0.5f}},
        {"silky",   SoundContext::Generic,    {.osc0_type=OscType::Triangle, .filter_cutoff=2500.0f, .filter_resonance=0.0f}},

        // ── Space / Reverb ────────────────────────────────────────────────────
        {"spacious", SoundContext::Generic,   {.reverb_size=0.85f, .reverb_mix=0.5f, .reverb_width=0.9f}},
        {"ambient",  SoundContext::Generic,   {.reverb_size=0.9f,  .reverb_mix=0.6f, .amp_attack=0.3f}},
        {"ambient",  SoundContext::Pad,       {.reverb_size=0.95f, .reverb_mix=0.7f, .amp_attack=0.6f, .amp_release=2.5f}},
        {"dry",      SoundContext::Generic,   {.reverb_mix=0.0f,   .delay_mix=0.0f}},
        {"wet",      SoundContext::Generic,   {.reverb_mix=0.5f,   .delay_mix=0.3f}},
        {"cathedral",SoundContext::Generic,   {.reverb_size=1.0f,  .reverb_mix=0.7f, .reverb_damping=0.2f}},
        {"room",     SoundContext::Generic,   {.reverb_size=0.35f, .reverb_mix=0.25f}},
        {"hall",     SoundContext::Generic,   {.reverb_size=0.75f, .reverb_mix=0.4f}},
        {"underwater",SoundContext::Generic,  {.filter_type=FilterType::LowPass, .filter_cutoff=300.0f, .filter_resonance=0.45f, .reverb_mix=0.6f}},
        {"wide",     SoundContext::Generic,   {.reverb_width=1.0f, .reverb_mix=0.4f}},
        {"narrow",   SoundContext::Generic,   {.reverb_width=0.0f, .reverb_mix=0.0f}},

        // ── Modulation / Motion ───────────────────────────────────────────────
        {"evolving", SoundContext::Generic,   {.lfo0_target=LfoTarget::FilterCutoff, .lfo0_rate=0.1f, .lfo0_depth=0.7f}},
        {"morphing", SoundContext::Generic,   {.lfo0_target=LfoTarget::FilterCutoff, .lfo0_rate=0.05f,.lfo0_depth=0.9f}},
        {"tremolo",  SoundContext::Generic,   {.lfo0_target=LfoTarget::Amplitude,    .lfo0_rate=5.0f, .lfo0_depth=0.5f}},
        {"vibrato",  SoundContext::Generic,   {.lfo0_target=LfoTarget::Pitch,        .lfo0_rate=5.5f, .lfo0_depth=0.25f}},
        {"wobble",   SoundContext::Generic,   {.lfo0_target=LfoTarget::FilterCutoff, .lfo0_rate=3.0f, .lfo0_depth=0.8f}},
        {"glitchy",  SoundContext::Generic,   {.lfo0_waveform=LfoWaveform::Square,   .lfo0_rate=15.0f,.lfo0_depth=0.9f}},
        {"pulsing",  SoundContext::Generic,   {.lfo0_target=LfoTarget::Amplitude,    .lfo0_rate=2.0f, .lfo0_depth=0.4f}},
        {"static",   SoundContext::Generic,   {.lfo0_depth=0.0f}},

        // ── Envelope / Attack character ───────────────────────────────────────
        {"plucky",   SoundContext::Generic,   {.amp_attack=0.001f, .amp_decay=0.08f, .amp_sustain=0.0f, .amp_release=0.1f}},
        {"plucky",   SoundContext::Bass,      {.amp_attack=0.001f, .amp_decay=0.12f, .amp_sustain=0.0f, .amp_release=0.15f, .filter_cutoff=600.0f, .flt_decay=0.1f, .filter_env_mod=0.5f}},
        {"pad",      SoundContext::Generic,   {.amp_attack=0.5f,   .amp_decay=0.5f,  .amp_sustain=0.8f, .amp_release=1.5f,  .reverb_mix=0.35f}},
        {"punchy",   SoundContext::Generic,   {.amp_attack=0.001f, .amp_decay=0.05f, .amp_sustain=0.3f, .amp_release=0.1f}},
        {"soft",     SoundContext::Generic,   {.amp_attack=0.15f,  .amp_decay=0.2f,  .amp_sustain=0.7f, .amp_release=0.4f}},
        {"sharp",    SoundContext::Generic,   {.amp_attack=0.001f, .amp_decay=0.03f, .amp_sustain=0.0f}},
        {"slow",     SoundContext::Generic,   {.amp_attack=0.8f,   .amp_release=1.0f}},
        {"fast",     SoundContext::Generic,   {.amp_attack=0.001f, .amp_release=0.05f}},
        {"legato",   SoundContext::Generic,   {.amp_attack=0.05f,  .portamento=0.12f}},
        {"staccato", SoundContext::Generic,   {.amp_sustain=0.2f,  .amp_release=0.03f}},
        {"swell",    SoundContext::Generic,   {.amp_attack=1.2f,   .amp_decay=0.3f,  .amp_sustain=0.9f, .amp_release=2.0f}},

        // ── Oscillator Waveform ───────────────────────────────────────────────
        {"sine",     SoundContext::Generic,   {.osc0_type=OscType::Sine}},
        {"pure",     SoundContext::Generic,   {.osc0_type=OscType::Sine, .filter_resonance=0.0f}},
        {"saw",      SoundContext::Generic,   {.osc0_type=OscType::Sawtooth}},
        {"sawtooth", SoundContext::Generic,   {.osc0_type=OscType::Sawtooth}},
        {"square",   SoundContext::Generic,   {.osc0_type=OscType::Square, .osc0_pulse_width=0.5f}},
        {"hollow",   SoundContext::Generic,   {.osc0_type=OscType::Square, .osc0_pulse_width=0.25f}},
        {"triangle", SoundContext::Generic,   {.osc0_type=OscType::Triangle}},
        {"noise",    SoundContext::Generic,   {.osc0_type=OscType::Noise,  .filter_resonance=0.55f}},
        {"metallic", SoundContext::Generic,   {.osc0_type=OscType::FM, .osc0_fm_depth=0.7f, .osc0_fm_ratio=3.5f, .filter_resonance=0.4f}},
        {"bell",     SoundContext::Generic,   {.osc0_type=OscType::FM, .osc0_fm_ratio=2.0f, .osc0_fm_depth=0.5f, .amp_decay=0.8f, .amp_sustain=0.0f}},

        // ── Register / Frequency range ────────────────────────────────────────
        {"bass",     SoundContext::Generic,   {.osc0_semitone=-12.0f, .filter_cutoff=350.0f, .master_gain=0.85f}},
        {"sub",      SoundContext::Generic,   {.osc0_type=OscType::Sine, .osc0_semitone=-24.0f}},
        {"lead",     SoundContext::Generic,   {.osc0_type=OscType::Sawtooth, .filter_cutoff=4500.0f, .filter_resonance=0.35f}},
        {"high",     SoundContext::Generic,   {.osc0_semitone=12.0f,  .filter_cutoff=10000.0f}},
        {"low",      SoundContext::Generic,   {.osc0_semitone=-12.0f, .filter_cutoff=300.0f}},
        {"mid",      SoundContext::Generic,   {.filter_cutoff=1000.0f}},

        // ── Polyphony / Voice ─────────────────────────────────────────────────
        {"unison",   SoundContext::Generic,   {.osc1_enabled=true, .osc1_detune=12.0f, .osc1_volume=0.7f}},
        {"mono",     SoundContext::Generic,   {.voice_count=1, .portamento=0.08f}},
        {"poly",     SoundContext::Generic,   {.voice_count=8}},

        // ── Distortion / Drive ────────────────────────────────────────────────
        {"distorted",SoundContext::Generic,   {.filter_drive=0.8f, .filter_resonance=0.5f}},
        {"fuzzy",    SoundContext::Generic,   {.filter_drive=0.75f}},
        {"clean",    SoundContext::Generic,   {.filter_drive=0.0f, .filter_resonance=0.05f, .reverb_mix=0.05f}},
        {"saturated",SoundContext::Generic,   {.filter_drive=0.6f, .filter_resonance=0.3f}},
        {"overdriven",SoundContext::Generic,  {.filter_drive=0.9f, .filter_resonance=0.55f, .filter_type=FilterType::LowPass, .filter_cutoff=5000.0f}},

        // ── Mood / Character ──────────────────────────────────────────────────
        {"aggressive",SoundContext::Generic,  {.osc0_type=OscType::Sawtooth, .filter_cutoff=5000.0f, .filter_resonance=0.7f, .filter_drive=0.55f}},
        {"peaceful",  SoundContext::Generic,  {.osc0_type=OscType::Sine, .filter_cutoff=1500.0f, .reverb_mix=0.4f, .amp_attack=0.15f}},
        {"mysterious",SoundContext::Generic,  {.filter_cutoff=600.0f, .reverb_mix=0.5f, .lfo0_rate=0.08f, .lfo0_depth=0.5f}},
        {"organic",   SoundContext::Generic,  {.osc0_type=OscType::Triangle, .filter_resonance=0.08f, .amp_attack=0.015f}},
        {"digital",   SoundContext::Generic,  {.osc0_type=OscType::Square, .filter_resonance=0.65f, .filter_drive=0.3f}},
        {"ethereal",  SoundContext::Generic,  {.osc0_type=OscType::Sine, .filter_cutoff=3000.0f, .reverb_size=0.9f, .reverb_mix=0.65f, .amp_attack=0.5f}},
        {"tense",     SoundContext::Generic,  {.filter_cutoff=2500.0f, .filter_resonance=0.6f, .amp_attack=0.001f, .reverb_mix=0.1f}},
    }};
    return data;
}
// clang-format on

} // namespace agentic_synth::mapper
