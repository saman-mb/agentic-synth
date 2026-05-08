#pragma once
#include <string>

#include "engine/PatchStruct.h"

namespace agentsynth {

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
