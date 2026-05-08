#include "agent/AgentBridge.h"

#include <fstream>
#include <sstream>

namespace agentic_synth::agent {

namespace {
// Load text file; returns empty string on failure.
std::string load_text_file(const std::string& path) {
    std::ifstream f(path);
    if (!f) return {};
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}
} // namespace

std::string AgentBridge::status() const { return "agent-bridge-v2"; }

PatchStruct AgentBridge::submitPrompt(const std::string& prompt) {
    // Issue #65: heuristic first for < 200 ms, then semantic layer refines in place.
    PatchStruct patch = pipeline_.submit(prompt);
    if (semanticMapper_.apply(prompt, patch) > 0)
        pipeline_.injectPatch(patch);
    return patch;
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
    // Use the synth-domain system prompt if loaded; fall back to a terse default.
    const std::string& base = sampler_.systemPrompt().empty()
        ? std::string("You are a synthesizer patch designer. Generate synth parameters as structured JSON.\n")
        : sampler_.systemPrompt();
    std::string recap = memory_.buildRecap(userPrompt);
    if (recap.empty()) return base;
    return base + "\n## Session Feedback\n" + recap + "\nUse the above feedback to guide parameter choices.\n";
}

PatchVector AgentBridge::getParameterBias(const std::string& userPrompt) const {
    return memory_.computeParameterBias(userPrompt);
}

std::optional<PatchStruct> AgentBridge::generateLlmPatch(const std::string& prompt,
                                                          uint32_t patch_id) {
    auto result = sampler_.generate(prompt, patch_id);
    if (result) refinePatch(*result);
    return result;
}

} // namespace agentic_synth::agent
