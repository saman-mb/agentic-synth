#pragma once

#include "engine/PatchStruct.h"

#include <array>
#include <cmath>

namespace agentic_synth::engine {

// Style transfer: extract salient characteristics from a reference patch
// and apply them to a target patch while preserving target's identity.

class StyleTransfer {
public:
    struct StyleProfile {
        float filterCharacter; // 0 = open, 1 = closed
        float envelopeShape;   // 0 = percussive, 1 = sustained
        float modulationFeel;  // 0 = static, 1 = animated
        float brightness;      // 0 = dark, 1 = bright
    };

    // Extract a style profile from a reference patch
    static StyleProfile extract(const PatchStruct& reference);

    // Apply style profile to target patch, returning a new styled patch
    static PatchStruct apply(const PatchStruct& target, const StyleProfile& style, float blend = 1.0f);

    // Convenience: extract from reference, apply to target
    static PatchStruct transfer(const PatchStruct& reference, const PatchStruct& target, float blend = 1.0f);
};

} // namespace agentic_synth::engine
