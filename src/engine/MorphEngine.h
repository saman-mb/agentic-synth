#pragma once

#include <array>
#include <cstdint>
#include <optional>

#include "engine/PatchStruct.h"

namespace agentic_synth::engine {

// ---------------------------------------------------------------------------
// MorphEngine — Issue #86
//
// Stores up to 4 named patch "morph targets" and linearly interpolates all
// float parameters across them based on a normalised morph position [0, 1].
//
// Position semantics with N saved targets:
//   - Position 0.0  → target[0]
//   - Position 1.0  → target[N-1]
//   - Intermediate  → piecewise linear between adjacent targets
//
// MIDI CC binding: assign a CC number via setMorphCc(). MidiHandler calls
// onMidiCC() for every CC message; the engine updates the morph position and
// fires a callback so the audio pipeline can pick up the morphed patch.
// ---------------------------------------------------------------------------

class MorphEngine {
public:
    static constexpr int kMaxTargets = 4;

    using MorphCallback = std::function<void(const PatchStruct&)>;

    // Register a callback fired whenever the morph position changes.
    void setCallback(MorphCallback cb) { callback_ = std::move(cb); }

    // Save the current patch as morph target at slot [0..kMaxTargets-1].
    // Slot is automatically chosen (next free) if index == -1.
    // Returns the slot index used, or -1 if all slots are full and no index given.
    int saveTarget(const PatchStruct& patch, int index = -1) noexcept;

    // Clear a specific target slot.
    void clearTarget(int index) noexcept;

    // Clear all targets.
    void clearAll() noexcept;

    // Number of active targets (contiguous from slot 0).
    [[nodiscard]] int targetCount() const noexcept;

    // Get a saved target (nullopt if slot empty).
    [[nodiscard]] std::optional<PatchStruct> target(int index) const noexcept;

    // Set morph position [0, 1].  Fires callback if targets are available.
    void setPosition(float pos) noexcept;

    [[nodiscard]] float position() const noexcept { return position_; }

    // Compute the interpolated patch at the current position (or at `pos`).
    [[nodiscard]] PatchStruct morphedPatch() const noexcept;
    [[nodiscard]] PatchStruct morphedPatchAt(float pos) const noexcept;

    // MIDI CC routing — call from MidiHandler.
    // Only responds to the configured CC number (default 2 = Breath).
    void setMorphCc(int cc) noexcept { morphCc_ = cc; }
    [[nodiscard]] int morphCc() const noexcept { return morphCc_; }

    // Returns true if the CC was consumed (matches morphCc_).
    bool onMidiCC(int controller, int value) noexcept;

private:
    std::array<std::optional<PatchStruct>, kMaxTargets> targets_{};
    float position_{0.0f};
    int   morphCc_{2};  // CC#2 = Breath Controller
    MorphCallback callback_;

    static PatchStruct lerp(const PatchStruct& a, const PatchStruct& b, float t) noexcept;
};

} // namespace agentic_synth::engine
