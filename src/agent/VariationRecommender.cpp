#include "VariationRecommender.h"

namespace agentic_synth::agent {

std::vector<VariationSuggestion> VariationRecommender::suggest(const PatchStruct& current) {
    std::vector<VariationSuggestion> suggestions;

    // Strategy 1: Temperature sweep — slightly more aggressive
    {
        auto variants = engine_.temperatureSweep(current);
        suggestions.push_back({"More Aggressive", "Boosted resonance, sharper filter, punchier envelope",
                               variants[0], engine::VariationEngine::Strategy::TemperatureSweep});
    }

    // Strategy 2: Parameter perturbation — darker/warmer variant
    {
        PatchStruct darker = current;
        darker.filter.cutoff_hz  = std::max(20.0f, current.filter.cutoff_hz * 0.5f);
        darker.filter.resonance  = std::min(0.9f, current.filter.resonance + 0.2f);
        darker.amp_env.attack_s  = std::min(10.0f, current.amp_env.attack_s * 1.5f);
        suggestions.push_back({"Warmer / Darker", "Lowered cutoff, increased resonance, slower attack", darker,
                               engine::VariationEngine::Strategy::Perturbation});
    }

    // Strategy 3: Morph toward bright/plucky
    {
        PatchStruct brighter = current;
        brighter.filter.cutoff_hz  = std::min(18000.0f, current.filter.cutoff_hz * 3.0f);
        brighter.amp_env.attack_s  = std::max(0.001f, current.amp_env.attack_s * 0.3f);
        brighter.amp_env.decay_s   = std::max(0.001f, current.amp_env.decay_s * 0.5f);
        brighter.amp_env.sustain   = 0.8f;
        brighter.osc[0].volume     = std::min(1.0f, current.osc[0].volume + 0.3f);
        suggestions.push_back({"Brighter / Pluckier", "Higher cutoff, faster attack/decay, more saw wave", brighter,
                               engine::VariationEngine::Strategy::Perturbation});
    }

    return suggestions;
}

} // namespace agentic_synth::agent
