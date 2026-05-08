#pragma once

#include <array>
#include <optional>
#include <string>
#include <string_view>

#include "agent/PrePatchPipeline.h"
#include "agent/SessionMemory.h"
#include "agent/StreamParser.h"
#include "engine/PatchStruct.h"
#include "engine/VariationEngine.h"
#include "mapper/GrammarSampler.h"
#include "mapper/SemanticMapper.h"

namespace agentic_synth::agent {

class AgentBridge {
public:
    AgentBridge();

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

    // Issue #64: grammar-constrained LLM patch generation.
    // Calls GrammarSampler, validates the result, then drives refinePatch().
    // Returns nullopt when the server is unreachable or returns invalid JSON.
    [[nodiscard]] std::optional<PatchStruct> generateLlmPatch(const std::string& prompt,
                                                               uint32_t patch_id = 0);

    // Issue #67: streaming patch application.
    // Feed a chunk of streaming LLM JSON. Fires pipeline_.injectPatch() each
    // time a top-level field is complete, delivering first audible change
    // well within 500 ms of stream start.
    void feedChunk(std::string_view chunk);

private:
    PrePatchPipeline pipeline_;
    engine::VariationEngine variationEngine_;
    SessionMemory memory_;
    mapper::GrammarSampler sampler_;
    mapper::SemanticMapper semanticMapper_;
    StreamParser streamParser_;
};

} // namespace agentic_synth::agent
