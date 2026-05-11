#pragma once
#include <string>

#include "engine/PatchStruct.h"

namespace agentsynth {

// Re-export agentic_synth types so callers can use agentsynth:: qualification
// (e.g. agentsynth::OscType, agentsynth::LfoTarget) without pulling in the full
// agentic_synth namespace.
using agentic_synth::FilterType;
using agentic_synth::LfoTarget;
using agentic_synth::LfoWaveform;
using agentic_synth::make_default_patch;
using agentic_synth::OscType;
using agentic_synth::PatchStruct;

// Phase 1 placeholder: pure rule-based NL → PatchStruct mapping.
// Maps ~50 semantic descriptor keywords to synth parameter presets
// without any LLM or embedding inference.
class HeuristicParser {
public:
    // Scan prompt for known keywords and compose a PatchStruct.
    // Multiple keywords combine additively; later rules overwrite
    // earlier ones when they touch the same field.
    [[nodiscard]] PatchStruct parse(const std::string& prompt) const;
};

} // namespace agentsynth
