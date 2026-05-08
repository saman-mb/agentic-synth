#pragma once

#include <array>

#include "engine/PatchStruct.h"
#include "engine/VariationEngine.h"

namespace agentic_synth::agent {

// A/B variation grid: holds concurrent patch renders for UI display.
// Each slot corresponds to one cell in the 5-patch variation grid.
struct Previews {
    static constexpr int kCount = engine::VariationEngine::kVariationCount;

    std::array<PatchStruct, kCount> patches;

    // Populate all kCount previews concurrently via the variation engine.
    static Previews render(const engine::VariationEngine& eng, const PatchStruct& base) {
        return Previews{eng.generateVariations(base)};
    }
};

} // namespace agentic_synth::agent
