     1|#pragma once
     2|
     3|#include <array>
     4|#include <optional>
     5|#include <string_view>
     6|
     7|#include "engine/PatchStruct.h"
     8|
     9|namespace agentic_synth::mapper {
    10|
    11|// ---------------------------------------------------------------------------
    12|// PatchDelta — sparse set of overrides applied to a base PatchStruct.
    13|// std::nullopt means "leave unchanged".
    14|// ---------------------------------------------------------------------------
    15|
    16|struct PatchDelta {
    17|    // Oscillator 0
    18|    std::optional<OscType> osc0_type;
    19|    std::optional<float> osc0_semitone; // semitones, -48..+48
    20|    std::optional<float> osc0_detune;   // cents
    21|    std::optional<float> osc0_volume;   // 0..1
    22|    std::optional<float> osc0_fm_ratio;
    23|    std::optional<float> osc0_fm_depth;
    24|    std::optional<float> osc0_pulse_width;
    25|    // Oscillator 1 (supersaw/unison layer)
    26|    std::optional<bool> osc1_enabled;
    27|    std::optional<float> osc1_detune;
    28|    std::optional<float> osc1_volume;
    29|    // Filter
    30|    std::optional<FilterType> filter_type;
    31|    std::optional<float> filter_cutoff;    // Hz
    32|    std::optional<float> filter_resonance; // 0..1
    33|    std::optional<float> filter_drive;     // 0..1
    34|    std::optional<float> filter_env_mod;   // -1..+1
    35|    // Amp envelope
    36|    std::optional<float> amp_attack;
    37|    std::optional<float> amp_decay;
    38|    std::optional<float> amp_sustain;
    39|    std::optional<float> amp_release;
    40|    // Filter envelope
    41|    std::optional<float> flt_attack;
    42|    std::optional<float> flt_decay;
    43|    std::optional<float> flt_sustain;
    44|    std::optional<float> flt_release;
    45|    // LFO 0
    46|    std::optional<LfoWaveform> lfo0_waveform;
    47|    std::optional<LfoTarget> lfo0_target;
    48|    std::optional<float> lfo0_rate;
    49|    std::optional<float> lfo0_depth;
    50|    // Reverb
    51|    std::optional<float> reverb_size;
    52|    std::optional<float> reverb_damping;
    53|    std::optional<float> reverb_width;
    54|    std::optional<float> reverb_mix;
    55|    // Delay
    56|    std::optional<float> delay_time;
    57|    std::optional<float> delay_feedback;
    58|    std::optional<float> delay_mix;
    59|    // Global
    60|    std::optional<float> master_gain;
    61|    std::optional<float> portamento;
    62|    std::optional<uint8_t> voice_count;
    63|};
    64|
    65|// Apply delta to patch in-place. Only set fields are written.
    66|inline void apply_delta(PatchStruct& p, const PatchDelta& d) noexcept {
    67|    if (d.osc0_type)
    68|        p.osc[0].type = *d.osc0_type;
    69|    if (d.osc0_semitone)
    70|        p.osc[0].semitone_offset = *d.osc0_semitone;
    71|    if (d.osc0_detune)
    72|        p.osc[0].detune_cents = *d.osc0_detune;
    73|    if (d.osc0_volume)
    74|        p.osc[0].volume = *d.osc0_volume;
    75|    if (d.osc0_fm_ratio)
    76|        p.osc[0].fm_ratio = *d.osc0_fm_ratio;
    77|    if (d.osc0_fm_depth)
    78|        p.osc[0].fm_depth = *d.osc0_fm_depth;
    79|    if (d.osc0_pulse_width)
    80|        p.osc[0].pulse_width = *d.osc0_pulse_width;
    81|    if (d.osc1_enabled)
    82|        p.osc[1].enabled = *d.osc1_enabled ? 1u : 0u;
    83|    if (d.osc1_detune)
    84|        p.osc[1].detune_cents = *d.osc1_detune;
    85|    if (d.osc1_volume)
    86|        p.osc[1].volume = *d.osc1_volume;
    87|    if (d.filter_type)
    88|        p.filter.type = *d.filter_type;
    89|    if (d.filter_cutoff)
    90|        p.filter.cutoff_hz = *d.filter_cutoff;
    91|    if (d.filter_resonance)
    92|        p.filter.resonance = *d.filter_resonance;
    93|    if (d.filter_drive)
    94|        p.filter.drive = *d.filter_drive;
    95|    if (d.filter_env_mod)
    96|        p.filter.env_mod = *d.filter_env_mod;
    97|    if (d.amp_attack)
    98|        p.amp_env.attack_s = *d.amp_attack;
    99|    if (d.amp_decay)
   100|        p.amp_env.decay_s = *d.amp_decay;
   101|    if (d.amp_sustain)
   102|        p.amp_env.sustain = *d.amp_sustain;
   103|    if (d.amp_release)
   104|        p.amp_env.release_s = *d.amp_release;
   105|    if (d.flt_attack)
   106|        p.filter_env.attack_s = *d.flt_attack;
   107|    if (d.flt_decay)
   108|        p.filter_env.decay_s = *d.flt_decay;
   109|    if (d.flt_sustain)
   110|        p.filter_env.sustain = *d.flt_sustain;
   111|    if (d.flt_release)
   112|        p.filter_env.release_s = *d.flt_release;
   113|    if (d.lfo0_waveform)
   114|        p.lfo[0].waveform = *d.lfo0_waveform;
   115|    if (d.lfo0_target)
   116|        p.lfo[0].target = *d.lfo0_target;
   117|    if (d.lfo0_rate)
   118|        p.lfo[0].rate_hz = *d.lfo0_rate;
   119|    if (d.lfo0_depth)
   120|        p.lfo[0].depth = *d.lfo0_depth;
   121|    if (d.reverb_size)
   122|        p.reverb.size = *d.reverb_size;
   123|    if (d.reverb_damping)
   124|        p.reverb.damping = *d.reverb_damping;
   125|    if (d.reverb_width)
   126|        p.reverb.width = *d.reverb_width;
   127|    if (d.reverb_mix)
   128|        p.reverb.mix = *d.reverb_mix;
   129|    if (d.delay_time)
   130|        p.delay.time_s = *d.delay_time;
   131|    if (d.delay_feedback)
   132|        p.delay.feedback = *d.delay_feedback;
   133|    if (d.delay_mix)
   134|        p.delay.mix = *d.delay_mix;
   135|    if (d.master_gain)
   136|        p.master_gain = *d.master_gain;
   137|    if (d.portamento)
   138|        p.portamento_s = *d.portamento;
   139|    if (d.voice_count)
   140|        p.voice_count = *d.voice_count;
   141|}
   142|
   143|// ---------------------------------------------------------------------------
   144|// Context categories for context-aware delta selection
   145|// ---------------------------------------------------------------------------
   146|
   147|enum class SoundContext : uint8_t {
   148|    Generic = 0,
   149|    Bass,
   150|    Pad,
   151|    Lead,
   152|    Keys,
   153|    Percussion,
   154|    Arp,
   155|    Texture,
   156|};
   157|
   158|// ---------------------------------------------------------------------------
   159|// DescriptorEntry — one row in the lookup table
   160|// ---------------------------------------------------------------------------
   161|
   162|struct DescriptorEntry {
   163|    std::string_view keyword;
   164|    SoundContext context; // Generic = applies to any context
   165|    PatchDelta delta;
   166|};
   167|
   168|// ---------------------------------------------------------------------------
   169|// Dataset — curated (descriptor, context, delta) triples
   170|// Descriptors are canonical lowercase forms; SemanticMapper resolves synonyms.
   171|// ---------------------------------------------------------------------------
   172|
   173|// clang-format off
   174|inline const std::array<DescriptorEntry, 74>& get_descriptor_dataset() {
   175|    static const std::array<DescriptorEntry, 74> data{{
   176|
   177|        // ── Brightness / Tone ─────────────────────────────────────────────────
   178|        {"dark",    SoundContext::Generic,    {.filter_cutoff=400.0f}},
   179|        {"dark",    SoundContext::Bass,       {.filter_cutoff=250.0f, .filter_resonance=0.1f}},
   180|        {"dark",    SoundContext::Pad,        {.filter_cutoff=600.0f, .amp_attack=0.4f}},
   181|        {"bright",  SoundContext::Generic,    {.filter_cutoff=8000.0f}},
   182|        {"bright",  SoundContext::Lead,       {.osc0_type=OscType::Sawtooth, .filter_cutoff=6000.0f}},
   183|        {"warm",    SoundContext::Generic,    {.osc0_type=OscType::Triangle, .filter_cutoff=2000.0f}},
   184|        {"cold",    SoundContext::Generic,    {.osc0_type=OscType::Square, .filter_cutoff=5000.0f}},
   185|        {"icy",     SoundContext::Generic,    {.filter_cutoff=6000.0f, .filter_resonance=0.75f}},
   186|        {"mellow",  SoundContext::Generic,    {.filter_cutoff=1200.0f, .filter_resonance=0.05f}},
   187|        {"harsh",   SoundContext::Generic,    {.filter_cutoff=7000.0f, .filter_resonance=0.8f}},
   188|        {"smooth",  SoundContext::Generic,    {.filter_cutoff=3000.0f, .filter_resonance=0.0f}},
   189|        {"gritty",  SoundContext::Generic,    {.filter_drive=0.65f}},
   190|        {"silky",   SoundContext::Generic,    {.osc0_type=OscType::Triangle, .filter_cutoff=2500.0f}},
   191|
   192|        // ── Space / Reverb ────────────────────────────────────────────────────
   193|        {"spacious", SoundContext::Generic,   {.reverb_size=0.85f, .reverb_mix=0.5f}},
   194|        {"ambient",  SoundContext::Generic,   {.reverb_size=0.9f, .reverb_mix=0.6f}},
   195|        {"ambient",  SoundContext::Pad,       {.amp_attack=0.6f, .reverb_size=0.95f, .reverb_mix=0.7f}},
   196|        {"dry",      SoundContext::Generic,   {.reverb_mix=0.0f}},
   197|        {"wet",      SoundContext::Generic,   {.reverb_mix=0.5f}},
   198|        {"cathedral",SoundContext::Generic,   {.reverb_size=1.0f, .reverb_mix=0.7f}},
   199|        {"room",     SoundContext::Generic,   {.reverb_size=0.35f}},
   200|        {"hall",     SoundContext::Generic,   {.reverb_size=0.75f}},
   201|        {"underwater",SoundContext::Generic,  {.filter_type=FilterType::LowPass, .filter_cutoff=300.0f, .filter_resonance=0.45f}},
   202|        {"wide",     SoundContext::Generic,   {.reverb_width=1.0f}},
   203|        {"narrow",   SoundContext::Generic,   {.reverb_width=0.0f}},
   204|
   205|        // ── Modulation / Motion ───────────────────────────────────────────────
   206|        {"evolving", SoundContext::Generic,   {.lfo0_target=LfoTarget::FilterCutoff, .lfo0_rate=0.1f}},
   207|        {"morphing", SoundContext::Generic,   {.lfo0_target=LfoTarget::FilterCutoff, .lfo0_rate=0.05f}},
   208|        {"tremolo",  SoundContext::Generic,   {.lfo0_target=LfoTarget::Amplitude, .lfo0_rate=5.0f}},
   209|        {"vibrato",  SoundContext::Generic,   {.lfo0_target=LfoTarget::Pitch, .lfo0_rate=5.5f}},
   210|        {"wobble",   SoundContext::Generic,   {.lfo0_target=LfoTarget::FilterCutoff, .lfo0_rate=3.0f}},
   211|        {"glitchy",  SoundContext::Generic,   {.lfo0_waveform=LfoWaveform::Square, .lfo0_rate=15.0f}},
   212|        {"pulsing",  SoundContext::Generic,   {.lfo0_target=LfoTarget::Amplitude, .lfo0_rate=2.0f}},
   213|        {"static",   SoundContext::Generic,   {.lfo0_depth=0.0f}},
   214|
   215|        // ── Envelope / Attack character ───────────────────────────────────────
   216|        {"plucky",   SoundContext::Generic,   {.amp_attack=0.001f, .amp_decay=0.08f, .amp_sustain=0.0f}},
   217|        {"plucky",   SoundContext::Bass,      {.filter_cutoff=600.0f, .amp_attack=0.001f, .amp_decay=0.12f, .amp_sustain=0.0f, .amp_release=0.15f, .flt_decay=0.1f}},
   218|        {"pad",      SoundContext::Generic,   {.amp_attack=0.5f, .amp_decay=0.5f, .amp_sustain=0.8f, .amp_release=1.5f}},
   219|        {"punchy",   SoundContext::Generic,   {.amp_attack=0.001f, .amp_decay=0.05f, .amp_sustain=0.3f}},
   220|        {"soft",     SoundContext::Generic,   {.amp_attack=0.15f, .amp_decay=0.2f, .amp_sustain=0.7f}},
   221|        {"sharp",    SoundContext::Generic,   {.amp_attack=0.001f, .amp_decay=0.03f}},
   222|        {"slow",     SoundContext::Generic,   {.amp_attack=0.8f}},
   223|        {"fast",     SoundContext::Generic,   {.amp_attack=0.001f}},
   224|        {"legato",   SoundContext::Generic,   {.amp_attack=0.05f}},
   225|        {"staccato", SoundContext::Generic,   {.amp_sustain=0.2f}},
   226|        {"swell",    SoundContext::Generic,   {.amp_attack=1.2f, .amp_decay=0.3f, .amp_sustain=0.9f}},
   227|
   228|        // ── Oscillator Waveform ───────────────────────────────────────────────
   229|        {"sine",     SoundContext::Generic,   {.osc0_type=OscType::Sine}},
   230|        {"pure",     SoundContext::Generic,   {.osc0_type=OscType::Sine}},
   231|        {"saw",      SoundContext::Generic,   {.osc0_type=OscType::Sawtooth}},
   232|        {"sawtooth", SoundContext::Generic,   {.osc0_type=OscType::Sawtooth}},
   233|        {"square",   SoundContext::Generic,   {.osc0_type=OscType::Square}},
   234|        {"hollow",   SoundContext::Generic,   {.osc0_type=OscType::Square}},
   235|        {"triangle", SoundContext::Generic,   {.osc0_type=OscType::Triangle}},
   236|        {"noise",    SoundContext::Generic,   {.osc0_type=OscType::Noise}},
   237|        {"metallic", SoundContext::Generic,   {.osc0_type=OscType::FM, .osc0_fm_ratio=3.5f, .osc0_fm_depth=0.7f}},
   238|        {"bell",     SoundContext::Generic,   {.osc0_type=OscType::FM, .osc0_fm_ratio=2.0f, .osc0_fm_depth=0.5f, .amp_decay=0.8f}},
   239|
   240|        // ── Register / Frequency range ────────────────────────────────────────
   241|        {"bass",     SoundContext::Generic,   {.osc0_semitone=-12.0f, .filter_cutoff=350.0f}},
   242|        {"sub",      SoundContext::Generic,   {.osc0_type=OscType::Sine}},
   243|        {"lead",     SoundContext::Generic,   {.osc0_type=OscType::Sawtooth, .filter_cutoff=4500.0f}},
   244|        {"high",     SoundContext::Generic,   {.osc0_semitone=12.0f}},
   245|        {"low",      SoundContext::Generic,   {.osc0_semitone=-12.0f}},
   246|        {"mid",      SoundContext::Generic,   {.filter_cutoff=1000.0f}},
   247|
   248|        // ── Polyphony / Voice ─────────────────────────────────────────────────
   249|        {"unison",   SoundContext::Generic,   {.osc1_enabled=true, .osc1_detune=12.0f}},
   250|        {"mono",     SoundContext::Generic,   {.voice_count=1}},
   251|        {"poly",     SoundContext::Generic,   {.voice_count=8}},
   252|
   253|        // ── Distortion / Drive ────────────────────────────────────────────────
   254|        {"distorted",SoundContext::Generic,   {.filter_drive=0.8f}},
   255|        {"fuzzy",    SoundContext::Generic,   {.filter_drive=0.75f}},
   256|        {"clean",    SoundContext::Generic,   {.filter_resonance=0.05f, .filter_drive=0.0f}},
   257|        {"saturated",SoundContext::Generic,   {.filter_drive=0.6f}},
   258|        {"overdriven",SoundContext::Generic,  {.filter_type=FilterType::LowPass, .filter_resonance=0.55f, .filter_drive=0.9f}},
   259|
   260|        // ── Mood / Character ──────────────────────────────────────────────────
   261|        {"aggressive",SoundContext::Generic,  {.osc0_type=OscType::Sawtooth, .filter_cutoff=5000.0f, .filter_resonance=0.7f}},
   262|        {"peaceful",  SoundContext::Generic,  {.osc0_type=OscType::Sine, .filter_cutoff=1500.0f, .reverb_mix=0.4f}},
   263|        {"mysterious",SoundContext::Generic,  {.filter_cutoff=600.0f, .lfo0_rate=0.08f, .reverb_mix=0.5f}},
   264|        {"organic",   SoundContext::Generic,  {.osc0_type=OscType::Triangle, .filter_resonance=0.08f}},
   265|        {"digital",   SoundContext::Generic,  {.osc0_type=OscType::Square, .filter_resonance=0.65f}},
   266|        {"ethereal",  SoundContext::Generic,  {.osc0_type=OscType::Sine, .filter_cutoff=3000.0f, .reverb_size=0.9f, .reverb_mix=0.65f}},
   267|        {"tense",     SoundContext::Generic,  {.filter_cutoff=2500.0f, .filter_resonance=0.6f, .amp_attack=0.001f}},
   268|    }};
   269|    return data;
   270|}
   271|// clang-format on
   272|
   273|} // namespace agentic_synth::mapper
   274|