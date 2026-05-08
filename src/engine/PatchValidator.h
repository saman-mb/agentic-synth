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

} // namespace agentic_synth
