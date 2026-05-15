#pragma once

#include <functional>
#include <optional>
#include <string>
#include <string_view>

#include "agent/PrePatchPipeline.h"
#include "agent/SessionMemory.h"
#include "agent/StreamParser.h"
#include "engine/PatchStruct.h"
#include "mapper/GeminiSampler.h"
#include "mapper/GrammarSampler.h"
#include "mapper/SemanticMapper.h"

namespace agentic_synth::agent {

class KnobBridge; // for read-only MIDI CC state accessors

// Phase 12A: extracted from AgentBridge to address god-class concerns
// (continuation of the Phase 10C split — see DictionaryService /
// TelemetryService).
//
// Single responsibility: the LLM-prompt / streaming patch path. Drives
// PrePatchPipeline.submit() for the < 200 ms heuristic dispatch, runs
// the SemanticMapper refinement, applies LLM-refined patches, feeds
// streaming JSON chunks into the StreamParser, and builds the
// session-aware system prompt + natural-language rationale.
//
// Composition (not inheritance): AgentBridge owns one PromptHandler
// that composes non-owning references to PrePatchPipeline,
// GrammarSampler, SemanticMapper, StreamParser, SessionMemory, and a
// const-ref to KnobBridge so it can read the latched MIDI CC state
// without taking a write-side dependency.
//
// The StreamParser callback wiring stays on AgentBridge (it has to
// also fan out to typed subscribers via notifyPatch); PromptHandler
// owns only the methods that read/write the StreamParser state.
class PromptHandler {
public:
    PromptHandler(PrePatchPipeline& pipeline, mapper::GrammarSampler& sampler, mapper::GeminiSampler& gemini,
                  mapper::SemanticMapper& semanticMapper, StreamParser& streamParser, SessionMemory& memory,
                  const KnobBridge& knob) noexcept
        : pipeline_(pipeline), sampler_(sampler), gemini_(gemini), semanticMapper_(semanticMapper),
          streamParser_(streamParser), memory_(memory), knob_(knob) {}

    // Issue #65/#68: parse heuristically and dispatch to audio thread
    // immediately (< 200 ms); semantic mapper refines in place.
    PatchStruct submitPrompt(const std::string& prompt);

    // Smoothly replace the heuristic patch with the LLM-refined result.
    void refinePatch(const PatchStruct& llmPatch);

    // Issue #64: grammar-constrained LLM patch generation. Returns nullopt
    // when the server is unreachable or returns invalid JSON.
    //
    // Phase 22 — refinement context. When the user types a relative prompt
    // ("darker", "more wobble", "a bit thicker") we MUST NOT regenerate from
    // scratch — that's what was stripping out the wobble the user wanted to
    // keep. Pass the last successful patch + the prompt that produced it via
    // previousPatch/previousPrompt; PromptHandler detects relative language
    // (isRelativePrompt) and, when both are present, wraps the LLM payload in
    // a refinement frame so §5.3 of system-prompt.md fires and the LLM
    // nudges instead of restarting. Both args default to nullopt for legacy
    // call sites and for cold-start prompts where no prior patch exists.
    [[nodiscard]] std::optional<PatchStruct>
    generateLlmPatch(const std::string& prompt, uint32_t patch_id = 0,
                     std::optional<PatchStruct> previousPatch = std::nullopt,
                     std::optional<std::string> previousPrompt = std::nullopt);

    // Phase 22: returns true when `prompt` contains comparative / refinement
    // language ("darker", "more wobble", "weirder", …). Used by callers
    // (WebUiComponent worker, AgentBridge) to skip the prompt-enhancer step
    // — enhancing "darker" into a 9-section brief erases the directional
    // intent that §5.3 needs to see verbatim. Word-boundary substring match
    // against a static keyword list; no NLP, no allocation hot path.
    [[nodiscard]] static bool isRelativePrompt(const std::string& prompt) noexcept;

    // Issue #67: streaming patch application — feed a JSON chunk.
    void feedChunk(std::string_view chunk);

    // Build the session-aware system prompt for the LLM. Appends MIDI CC
    // context (filter open/closed, resonance) and recent session recap.
    [[nodiscard]] std::string buildSystemPrompt(const std::string& userPrompt) const;

    // Per-dimension parameter bias derived from session memory.
    [[nodiscard]] PatchVector getParameterBias(const std::string& userPrompt) const;

    // Issue #85: natural-language rationale for the chosen patch in the
    // context of the current prompt + session memory.
    [[nodiscard]] std::string generateRationale(const std::string& prompt, const PatchStruct& patch) const;

private:
    PrePatchPipeline& pipeline_;
    mapper::GrammarSampler& sampler_;
    mapper::GeminiSampler& gemini_;
    mapper::SemanticMapper& semanticMapper_;
    StreamParser& streamParser_;
    SessionMemory& memory_;
    const KnobBridge& knob_;
};

} // namespace agentic_synth::agent
