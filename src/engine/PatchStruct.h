#pragma once
#include <cstdint>
#include <type_traits>

namespace agentsynth {

enum class OscType : uint8_t { Sine = 0, Saw, Square, Triangle, Noise };
enum class FilterType : uint8_t { LowPass = 0, HighPass, BandPass };
enum class LfoWaveform : uint8_t { Sine = 0, Triangle, Square, Saw };
enum class LfoTarget : uint8_t { Pitch = 0, Filter, Volume, Pan };

struct OscParams {
    OscType type;
    float   semitone_offset;  // -48 to +48 semitones
    float   detune_cents;     // -100 to +100 cents
    float   wavetable_pos;    // 0 to 1
    float   fm_ratio;         // 0.5 to 16
    float   fm_depth;         // 0 to 1
    float   volume;           // 0 to 1
    float   pan;              // -1 to +1
    float   pulse_width;      // 0.01 to 0.99
    uint8_t enabled;
};

struct EnvParams {
    float attack_s;   // 0 to 10 s
    float decay_s;    // 0 to 10 s
    float sustain;    // 0 to 1
    float release_s;  // 0 to 20 s
};

struct FilterParams {
    FilterType type;
    float      cutoff_hz;  // 20 to 20000 Hz
    float      resonance;  // 0 to 1
    float      env_mod;    // -1 to +1
    float      key_track;  // 0 to 1
    float      drive;      // 0 to 1
};

struct LfoParams {
    LfoWaveform waveform;
    LfoTarget   target;
    float       rate_hz;       // 0.01 to 20 Hz
    float       depth;         // 0 to 1
    float       phase_offset;  // 0 to 1
    uint8_t     bpm_sync;
};

struct ReverbParams {
    float size;
    float damping;
    float width;
    float mix;
};

struct DelayParams {
    float   time_s;    // 0 to 2 s
    float   feedback;  // 0 to 0.99
    float   mix;       // 0 to 1
    uint8_t bpm_sync;
};

constexpr uint32_t kPatchStructVersion = 1;

struct PatchStruct {
    uint32_t    version;
    uint32_t    patch_id;
    OscParams   osc[3];
    FilterParams filter;
    EnvParams   filter_env;
    EnvParams   amp_env;
    LfoParams   lfo[2];
    ReverbParams reverb;
    DelayParams  delay;
    float        master_gain;
    float        portamento_s;
    uint8_t      voice_count;  // 1 to 16
};

static_assert(std::is_trivially_copyable_v<PatchStruct>,
              "PatchStruct must be trivially copyable for lock-free transfer");

inline PatchStruct make_default_patch() noexcept {
    PatchStruct p{};
    p.version              = kPatchStructVersion;
    p.osc[0].type          = OscType::Saw;
    p.osc[0].volume        = 0.8f;
    p.osc[0].enabled       = 1;
    p.osc[0].fm_ratio      = 1.0f;
    p.osc[0].pulse_width   = 0.5f;
    p.osc[1].fm_ratio      = 1.0f;
    p.osc[1].pulse_width   = 0.5f;
    p.osc[2].fm_ratio      = 1.0f;
    p.osc[2].pulse_width   = 0.5f;
    p.filter.type          = FilterType::LowPass;
    p.filter.cutoff_hz     = 4000.0f;
    p.filter.resonance     = 0.2f;
    p.amp_env.attack_s     = 0.01f;
    p.amp_env.decay_s      = 0.1f;
    p.amp_env.sustain      = 0.7f;
    p.amp_env.release_s    = 0.3f;
    p.filter_env.attack_s  = 0.01f;
    p.filter_env.decay_s   = 0.2f;
    p.filter_env.sustain   = 0.5f;
    p.filter_env.release_s = 0.3f;
    p.lfo[0].waveform      = LfoWaveform::Sine;
    p.lfo[0].target        = LfoTarget::Filter;
    p.lfo[0].rate_hz       = 1.0f;
    p.lfo[1].waveform      = LfoWaveform::Sine;
    p.lfo[1].target        = LfoTarget::Volume;
    p.lfo[1].rate_hz       = 1.0f;
    p.reverb.size          = 0.3f;
    p.reverb.damping       = 0.5f;
    p.reverb.width         = 0.5f;
    p.reverb.mix           = 0.1f;
    p.delay.time_s         = 0.5f;
    p.delay.feedback       = 0.3f;
    p.master_gain          = 0.7f;
    p.voice_count          = 8;
    return p;
}

}  // namespace agentsynth
