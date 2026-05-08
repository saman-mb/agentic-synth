#pragma once

#include <array>
#include <optional>
#include <string>

#include "agent/PrePatchPipeline.h"
#include "agent/SessionMemory.h"
#include "engine/PatchStruct.h"
#include "engine/VariationEngine.h"

namespace agentic_synth::agent {

class AgentBridge {
public:
    [[nodiscard]] std::string status() const;

    // Issue #68: pre-patch pipeline.
    // Parse heuristically and dispatch to audio thread immediately (< 200 ms).
    PatchStruct submitPrompt(const std::string& prompt);

    // Smoothly replace the heuristic patch with the LLM-refined result.
    void refinePatch(const PatchStruct& llmPatch);

    // Audio thread: pop the next pending patch.
    [[nodiscard]] std::optional<PatchStruct> pollPatch() noexcept;

    // Issue #69: variation engine.
    // Generate 5 audibly distinct variants from a base patch concurrently.
    [[nodiscard]] std::array<PatchStruct, engine::VariationEngine::kVariationCount>
    generateVariations(const PatchStruct& base) const;

    // Session memory: record user feedback and build LLM context.
    void recordFeedback(FeedbackKind kind, const std::string& prompt, const PatchStruct& patch);
    [[nodiscard]] std::string buildSystemPrompt(const std::string& userPrompt) const;
    [[nodiscard]] PatchVector getParameterBias(const std::string& userPrompt) const;
    [[nodiscard]] const SessionMemory& sessionMemory() const noexcept { return memory_; }

private:
    PrePatchPipeline pipeline_;
    engine::VariationEngine variationEngine_;
    SessionMemory memory_;
};

} // namespace agentic_synth::agent
