#include "VariationRecommender.h"

namespace agentic_synth::agent {

std::vector<VariationSuggestion> VariationRecommender::suggest(const PatchStruct& current) {
    std::vector<VariationSuggestion> suggestions;

    // Strategy 1: Temperature sweep — slightly more aggressive
    {
        auto variants = engine_.generate(current, 3, VariationEngine::Strategy::TemperatureSweep, 1);
        if (!variants.empty()) {
            suggestions.push_back({"More Aggressive", "Boosted resonance, sharper filter, punchier envelope",
                                   variants[0], VariationEngine::Strategy::TemperatureSweep});
        }
    }

    // Strategy 2: Parameter perturbation — darker/warmer variant
    {
        PatchStruct darker = current;
        darker.filterCutoffHz = std::max(20.0f, current.filterCutoffHz * 0.5f);
        darker.filterResonance = std::min(0.9f, current.filterResonance + 0.2f);
        darker.ampAttackMs = std::min(5000.0f, current.ampAttackMs * 1.5f);
        suggestions.push_back({"Warmer / Darker", "Lowered cutoff, increased resonance, slower attack", darker,
                               VariationEngine::Strategy::Perturbation});
    }

    // Strategy 3: Morph toward bright/plucky
    {
        PatchStruct brighter = current;
        brighter.filterCutoffHz = std::min(18000.0f, current.filterCutoffHz * 3.0f);
        brighter.ampAttackMs = std::max(1.0f, current.ampAttackMs * 0.3f);
        brighter.ampDecayMs = std::max(1.0f, current.ampDecayMs * 0.5f);
        brighter.ampSustainLevel = 0.8f;
        brighter.oscillatorMix[0] = std::min(1.0f, current.oscillatorMix[0] + 0.3f);
        suggestions.push_back({"Brighter / Pluckier", "Higher cutoff, faster attack/decay, more saw wave", brighter,
                               VariationEngine::Strategy::Perturbation});
    }

    return suggestions;
}

} // namespace agentic_synth::agent
