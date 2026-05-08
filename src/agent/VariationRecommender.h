#pragma once

#include "engine/PatchStruct.h"
#include "engine/VariationEngine.h"

#include <string>
#include <vector>

namespace agentic_synth::agent {

// Proactive variation suggestions during looped playback.
// Analyzes the current patch and suggests 3 variations.

struct VariationSuggestion {
    std::string label;       // e.g. "More aggressive"
    std::string rationale;   // e.g. "Increase resonance and swap to saw wave"
    PatchStruct patch;
    VariationEngine::Strategy strategy;
};

class VariationRecommender {
public:
    // Generate 3 proactive variation suggestions
    std::vector<VariationSuggestion> suggest(const PatchStruct& current);

    // Set variation engine (with random seed for reproducibility)
    void setVariationEngine(const VariationEngine& engine) { engine_ = engine; }

private:
    VariationEngine engine_;
};

} // namespace agentic_synth::agent
