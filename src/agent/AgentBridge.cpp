#include "agent/AgentBridge.h"

namespace agentic_synth::agent {

std::string AgentBridge::status() const { return "agent-bridge-v2"; }

PatchStruct AgentBridge::submitPrompt(const std::string& prompt) {
    return pipeline_.submit(prompt);
}

void AgentBridge::refinePatch(const PatchStruct& llmPatch) {
    pipeline_.refinePatch(llmPatch);
}

std::optional<PatchStruct> AgentBridge::pollPatch() noexcept {
    return pipeline_.poll();
}

std::array<PatchStruct, engine::VariationEngine::kVariationCount>
AgentBridge::generateVariations(const PatchStruct& base) const {
    return variationEngine_.generateVariations(base);
}

void AgentBridge::recordFeedback(FeedbackKind kind, const std::string& prompt, const PatchStruct& patch) {
    memory_.recordFeedback(kind, prompt, patch);
}

std::string AgentBridge::buildSystemPrompt(const std::string& userPrompt) const {
    std::string base = "You are a synthesizer patch designer. Generate synth parameters as structured JSON.\n";
    std::string recap = memory_.buildRecap(userPrompt);
    if (!recap.empty()) {
        base += "\n" + recap + "\nUse the above feedback to guide parameter choices.\n";
    }
    return base;
}

PatchVector AgentBridge::getParameterBias(const std::string& userPrompt) const {
    return memory_.computeParameterBias(userPrompt);
}

} // namespace agentic_synth::agent
