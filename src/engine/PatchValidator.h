#pragma once

#include "engine/PatchStruct.h"
#include "mapper/UnsafeMode.h"

namespace agentic_synth {

inline constexpr float kFilterCutoffFloor = 20.0f;      // Hz
inline constexpr float kFilterCutoffCeiling = 18000.0f; // Hz
inline constexpr float kSafeResonanceCeiling = 0.85f;   // below Moog self-oscillation

// Clamp and sanitize all PatchStruct fields. NaN/Inf replaced with safe
// defaults. Call before pushing any PatchStruct to the audio-thread SPSC queue.
[[nodiscard]] PatchStruct validate_patch(PatchStruct p, UnsafeModeFlags flags = {}) noexcept;

// Returns false if any float field is NaN or Inf.
[[nodiscard]] bool patch_is_finite(const PatchStruct& p) noexcept;

// One-pole DC-blocking HPF at ~20 Hz.
// Topology: y[n] = x[n] - x[n-1] + R * y[n-1]
// Apply per-voice on the audio output to reject DC from oscillators or FM.
struct DcBlocker {
    float x1 = 0.0f;
    float y1 = 0.0f;
    float R = 0.99715f; // 20 Hz at 44100 Hz

    void prepare(float sampleRate, float cutoffHz = 20.0f) noexcept {
        constexpr float kTwoPi = 6.28318530718f;
        R = 1.0f - (kTwoPi * cutoffHz / sampleRate);
        if (R < 0.0f)
            R = 0.0f;
        if (R > 0.9999f)
            R = 0.9999f;
        x1 = y1 = 0.0f;
    }

    [[nodiscard]] float process(float x) noexcept {
        const float y = x - x1 + R * y1;
        x1 = x;
        y1 = y;
        return y;
    }

    void reset() noexcept { x1 = y1 = 0.0f; }
};

} // namespace agentic_synth
