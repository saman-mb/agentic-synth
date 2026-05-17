#pragma once

#include <cstdint>
#include <cstring>
#include <type_traits>

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

// Phase E (#265): pre-filter chorus + tube saturation + reverb-send HPF.
//
// The augmenter writes these for the cinematic recipe; LLM grammar emits
// them as well so the same patch JSON round-trips. All POD, fixed size,
// trivially copyable — no allocation, no juce::var, no std::string.

struct ChorusParams {
    float rate_hz; // 0.1 .. 5.0 (default 0.4)
    float depth;   // 0 .. 1     (default 0.35)
    float mix;     // 0 .. 1     (default 0.0 — bit-exact bypass when off)
    uint8_t _pad[8];
};
static_assert(sizeof(ChorusParams) == 20);

struct TubeSatParams {
    float drive;   // 0 .. 0.5 (default 0.0 — bit-exact bypass when off)
    float mix;     // 0 .. 1   (default 1.0 when active — full wet)
    uint8_t _pad[8];
};
static_assert(sizeof(TubeSatParams) == 16);

// delay.bpm_sync semantics:
//   - When false: time_s is the delay length in seconds.
//   - When true:  time_s is reinterpreted as BEATS (not seconds). The
//                 convention is: 1.0 = quarter note, 0.5 = eighth,
//                 0.25 = sixteenth, 2.0 = half, 4.0 = whole. Conversion:
//                     delay_seconds = time_s * 60.0 / host_bpm
//                 The engine clamps beats to [0.0625, 4.0] and the resulting
//                 seconds value to [0.001, 2.0].
//   - Null AudioPlayHead (no host tempo reported) → engine falls back to
//     120 BPM so plug-in standalone hosts still produce a deterministic
//     beat length.
struct DelayParams {
    float time_s;     // 0 .. 2 seconds  (or beats when bpm_sync=true; see above)
    float feedback;   // 0 .. 0.99
    float mix;        // 0 .. 1
    float stereo;     // 0 .. 1 (0 = parallel, 1 = ping-pong cross-feed)
    uint8_t bpm_sync; // bool
    uint8_t _pad[3];
};
static_assert(sizeof(DelayParams) == 20);

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

    // Phase E (#265): pre-filter chorus + tube saturation. Inserted in the
    // per-voice signal chain BEFORE the filter (see VoiceManager.cpp). Both
    // default to bypass (chorus.mix==0, tubesat.drive==0) so older patches
    // and the augmenter's non-cinematic paths leave the audio untouched.
    ChorusParams chorus;
    TubeSatParams tubesat;
    // Reverb auxiliary-send HPF cutoff (Hz). 0 = bypass; 60..200 Hz = clean
    // sub energy off the reverb feed so the cathedral tail does not smear
    // the low end. Filter is per-voice on the send only — the dry path is
    // untouched. Applied in VoiceManager renderBlock before the master
    // reverb stage.
    float reverb_send_hpf_hz;
    uint8_t _pad_reverb_send[4];

    // Global
    float master_gain;   // 0 .. 1
    float portamento_s;  // 0 = off, >0 = glide time
    uint8_t voice_count; // 1 .. 16
    uint8_t _pad[3];

    // Phase 21: LLM-authored sensory rationale string. Populated by the
    // grammar-sampled / Gemini path when the model emits a "rationale" field
    // alongside the patch params. Empty for heuristic / legacy patches —
    // PromptHandler::generateRationale falls back to the templated heuristic
    // when this buffer is empty. POD-clean: fixed-size char array, zero-init.
    // 256 bytes leaves room for ~1-2 prose sentences after JSON escaping
    // without blowing the lock-free SPSC patch transfer budget.
    char rationale[256];

    // Phase 26: PatchAugmenter action log. Pipe-separated short strings
    // describing each runtime guardrail mutation (e.g.
    // "added sub-octave sine for depth|swapped noise-only for pitched saw").
    // Empty when augmentPatch was a no-op or skipped (refinement / simple
    // prompt). Surfaced to the UI so users see what got auto-corrected and
    // don't blame the LLM for the layered patch.
    char augmenter_actions[256];
};

// Ensure the struct is trivially copyable (required for lock-free queue transfer)
static_assert(std::is_trivially_copyable_v<PatchStruct>);

inline PatchStruct make_default_patch() noexcept {
    PatchStruct p{};
    std::memset(&p, 0, sizeof(p));

    p.version = kPatchStructVersion;
    p.patch_id = 0;

    // Per-osc sensible non-zero defaults so APVTS layout, make_default_patch
    // and AI-written patches all agree on what "unset" means. Phase 2
    // follow-up (Code Fix 3): defaults live here exclusively — APVTS layout
    // reads from this function, and writePatchToApvts no longer rewrites
    // zero values to "sensible" replacements behind the AI's back.
    for (auto& o : p.osc) {
        o.pulse_width = 0.5f; // square at 50% duty
        o.fm_ratio = 1.0f;    // 1:1 carrier:modulator
    }

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

    // Default delay stereo to moderate ping-pong (matches prior hardcoded VoiceManager value).
    p.delay.time_s = 0.25f;
    p.delay.stereo = 0.5f;

    // Default reverb width to full stereo (1.0). Phase 3 wires this to a
    // post-Reverb M/S blend: width=0 collapses wet to mono, width=1 leaves
    // the full stereo image. Older code ignored the field, so historical
    // default of 0 (memset) would now silently make every patch mono. 1.0
    // matches what Freeverb produced pre-Phase-3.
    p.reverb.width = 1.0f;

    // Phase E (#265): chorus + tubesat default to OFF so any patch authored
    // before these fields existed (zero-filled by memset above) sounds
    // identical to its pre-Phase-E behaviour. The augmenter cinematic recipe
    // raises chorus.mix / tubesat.drive when the prompt asks for lushness.
    p.chorus.rate_hz = 0.4f;
    p.chorus.depth = 0.35f;
    p.chorus.mix = 0.0f;
    p.tubesat.drive = 0.0f;
    p.tubesat.mix = 1.0f;
    p.reverb_send_hpf_hz = 0.0f;

    p.master_gain = 1.0f;
    p.voice_count = 8;

    return p;
}

} // namespace agentic_synth
