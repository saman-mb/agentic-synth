#include "agent/AgentBridge.h"

#include <fstream>
#include <sstream>

namespace agentic_synth::agent {

AgentBridge::AgentBridge() {
    // Wire stream parser: each completed field injects a partial patch
    // directly onto the audio SPSC queue for < 500 ms first-audible-change.
    streamParser_.setCallback([this](const PatchStruct& p) {
        pipeline_.injectPatch(p);
    });
}

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
    // Reset stream parser so field-complete callbacks from a prior LLM call
    // don't bleed into this submission's heuristic patch.
    streamParser_.reset();
    // Issue #65/#68: heuristic dispatched < 200 ms; semantic layer refines in place.
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

std::array<PatchStruct, engine::VariationEngine::kVariationCount>
AgentBridge::generateVariationsWithSeed(const PatchStruct& base, uint32_t perturbSeed) const {
    return variationEngine_.generateVariationsWithSeed(base, perturbSeed);
}

void AgentBridge::recordFeedback(FeedbackKind kind, const std::string& prompt, const PatchStruct& patch) {
    memory_.recordFeedback(kind, prompt, patch);
}

std::string AgentBridge::buildSystemPrompt(const std::string& userPrompt) const {
    const std::string& base = sampler_.systemPrompt().empty()
        ? std::string("You are a synthesizer patch designer. Generate synth parameters as structured JSON.\n")
        : sampler_.systemPrompt();

    std::string prompt = base;

    // Append MIDI CC context so the AI respects the user's current performance state.
    if (midiCutoffNorm_ < 0.25f)
        prompt += "MIDI context: filter is currently closed (dark sound).\n";
    else if (midiCutoffNorm_ > 0.75f)
        prompt += "MIDI context: filter is currently open (bright sound).\n";
    if (midiResonanceNorm_ > 0.5f)
        prompt += "MIDI context: high resonance is active.\n";

    std::string recap = memory_.buildRecap(userPrompt);
    if (recap.empty()) return prompt;
    return prompt + "\n## Session Feedback\n" + recap + "\nUse the above feedback to guide parameter choices.\n";
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

void AgentBridge::feedChunk(std::string_view chunk) {
    streamParser_.feedChunk(chunk);
}

void AgentBridge::onMidiCC(int controller, int value) noexcept {
    // Track CC74 (brightness/filter cutoff) and CC71 (resonance) so the
    // system prompt can reflect the user's current timbral preference.
    switch (controller) {
        case 71: midiResonanceNorm_ = static_cast<float>(value) / 127.0f; break;
        case 74: midiCutoffNorm_    = static_cast<float>(value) / 127.0f; break;
        default: break;
    }
}

} // namespace agentic_synth::agent
