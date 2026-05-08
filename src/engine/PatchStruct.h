#pragma once

#include <cstdint>
#include <cstring>

namespace agentic_synth {

// ---------------------------------------------------------------------------
// Enumerations
// ---------------------------------------------------------------------------

enum class OscType : uint8_t {
    Sine = 0,
    Triangle,
    Sawtooth,
    Square,
    Pulse,
    Wavetable,
    FM,
    Noise,
};

enum class FilterType : uint8_t {
    LowPass = 0,
    HighPass,
    BandPass,
    Notch,
    Peak,
};

enum class LfoWaveform : uint8_t {
    Sine = 0,
    Triangle,
    Sawtooth,
    Square,
    SampleAndHold,
};

enum class LfoTarget : uint8_t {
    None = 0,
    Pitch,
    FilterCutoff,
    Amplitude,
    Pan,
    WavetablePos,
    FmRatio,
};

// ---------------------------------------------------------------------------
// Sub-structures (all POD, fixed size, no heap)
// ---------------------------------------------------------------------------

struct OscParams {
    OscType type;
    uint8_t _pad[3];
    float semitone_offset; // -48 .. +48 semitones
    float detune_cents;    // -100 .. +100 cents
    float wavetable_pos;   // 0 .. 1 (wavetable frame)
    float fm_ratio;        // 0.5 .. 16 (FM only)
    float fm_depth;        // 0 .. 1
    float volume;          // 0 .. 1
    float pan;             // -1 (L) .. +1 (R)
    float pulse_width;     // 0.01 .. 0.99 (pulse/square)
    uint8_t enabled;       // bool flag
    uint8_t _pad2[3];
};
static_assert(sizeof(OscParams) == 40);

struct EnvParams {
    float attack_s;  // seconds, 0 .. 10
    float decay_s;   // seconds, 0 .. 10
    float sustain;   // 0 .. 1
    float release_s; // seconds, 0 .. 20
};
static_assert(sizeof(EnvParams) == 16);

struct FilterParams {
    FilterType type;
    uint8_t _pad[3];
    float cutoff_hz; // 20 .. 20000
    float resonance; // 0 .. 1
    float env_mod;   // -1 .. +1 (filter envelope depth)
    float key_track; // 0 .. 1
    float drive;     // 0 .. 1
};
static_assert(sizeof(FilterParams) == 24);

struct LfoParams {
    LfoWaveform waveform;
    LfoTarget target;
    uint8_t _pad[2];
    float rate_hz;      // 0.01 .. 20
    float depth;        // 0 .. 1
    float phase_offset; // 0 .. 1 (fraction of cycle)
    uint8_t bpm_sync;   // bool: sync to host tempo
    uint8_t _pad2[3];
};
static_assert(sizeof(LfoParams) == 20);

struct ReverbParams {
    float size;    // 0 .. 1
    float damping; // 0 .. 1
    float width;   // 0 .. 1
    float mix;     // 0 .. 1
};
static_assert(sizeof(ReverbParams) == 16);

struct DelayParams {
    float time_s;     // 0 .. 2
    float feedback;   // 0 .. 0.99
    float mix;        // 0 .. 1
    uint8_t bpm_sync; // bool
    uint8_t _pad[3];
};
static_assert(sizeof(DelayParams) == 16);

// ---------------------------------------------------------------------------
// Top-level PatchStruct
// POD, fixed size, safe to copy atomically via SPSC queue.
// ---------------------------------------------------------------------------

static constexpr uint32_t kPatchStructVersion = 1;
static constexpr int kMaxOscillators = 3;
static constexpr int kMaxLfos = 2;

struct PatchStruct {
    // Header
    uint32_t version;  // must equal kPatchStructVersion
    uint32_t patch_id; // monotonically increasing patch counter

    // Oscillators
    OscParams osc[kMaxOscillators];

    // Filter
    FilterParams filter;
    EnvParams filter_env;

    // Amplitude envelope
    EnvParams amp_env;

    // LFOs
    LfoParams lfo[kMaxLfos];

    // Effects
    ReverbParams reverb;
    DelayParams delay;

    // Global
    float master_gain;   // 0 .. 1
    float portamento_s;  // 0 = off, >0 = glide time
    uint8_t voice_count; // 1 .. 16
    uint8_t _pad[3];
};

// Ensure the struct is trivially copyable (required for lock-free queue transfer)
static_assert(std::is_trivially_copyable_v<PatchStruct>);

inline PatchStruct make_default_patch() noexcept {
    PatchStruct p{};
    std::memset(&p, 0, sizeof(p));

    p.version = kPatchStructVersion;
    p.patch_id = 0;

    // Osc 0: sawtooth, unity
    p.osc[0].type = OscType::Sawtooth;
    p.osc[0].volume = 1.0f;
    p.osc[0].enabled = 1;

    // Filter: low-pass, wide open
    p.filter.type = FilterType::LowPass;
    p.filter.cutoff_hz = 18000.0f;
    p.filter.resonance = 0.0f;

    // Amp envelope: instant attack, infinite sustain, short release
    p.amp_env.attack_s = 0.005f;
    p.amp_env.decay_s = 0.1f;
    p.amp_env.sustain = 1.0f;
    p.amp_env.release_s = 0.1f;

    // Filter envelope: fast, no mod
    p.filter_env.attack_s = 0.01f;
    p.filter_env.decay_s = 0.2f;
    p.filter_env.sustain = 0.0f;
    p.filter_env.release_s = 0.1f;

    // LFOs: off
    for (auto& lfo : p.lfo) {
        lfo.rate_hz = 1.0f;
        lfo.depth = 0.0f;
        lfo.target = LfoTarget::None;
    }

    p.master_gain = 1.0f;
    p.voice_count = 8;

    return p;
}

} // namespace agentic_synth
