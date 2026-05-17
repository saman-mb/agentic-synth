#pragma once

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "agent/PrePatchPipeline.h"
#include "agent/SessionMemory.h"
#include "agent/StreamParser.h"
#include "engine/PatchStruct.h"
#include "mapper/DeltaNudger.h"
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

    // Phase 34b — wire the LLM-delta-nudger in front of the Phase 34a top-1
    // archetype fallback. Default-constructed nudger is disabled; AgentBridge
    // pokes the GEMINI_KEY in at startup the same way it does for gemini_ /
    // enhancer_ / stt_. Test seam: inject a mock subclass.
    void setDeltaNudger(mapper::DeltaNudger* nudger) noexcept { deltaNudger_ = nudger; }
    [[nodiscard]] mapper::DeltaNudger* deltaNudger() const noexcept { return deltaNudger_; }

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

    // Phase 31 — heuristic-fallback guardrail. When the LLM call fails the
    // worker is left holding the bare heuristic patch from submitPrompt(),
    // bypassing the Phase 23/27/30 PatchAugmenter (3-osc layering, FM
    // coercion, cinematic-pad recipe, noise-only fix). Call this AFTER an
    // LLM-failure branch to fire the augmenter on the heuristic patch using
    // the same refinement-skip rule used inside generateLlmPatch(): when
    // isRelativePrompt(prompt) is true AND a previous patch exists the
    // augmenter is skipped (user is nudging an existing topology). The
    // augmenter is idempotent on already-augmented patches but should still
    // only be invoked on the LLM-failure path so we don't double-run on
    // success.
    void applyGuardrailIfNotRefinement(PatchStruct& patch, const std::string& prompt,
                                       bool hasPreviousPatch) noexcept;

    // Issue #67: streaming patch application — feed a JSON chunk.
    void feedChunk(std::string_view chunk);

    // Build the session-aware system prompt for the LLM. Appends MIDI CC
    // context (filter open/closed, resonance) and recent session recap.
    [[nodiscard]] std::string buildSystemPrompt(const std::string& userPrompt) const;

    // Phase 33 — strategic split of the ~80KB system prompt.
    // splitSystemPrompt() partitions the source markdown into:
    //   * `rails`     — everything OUTSIDE §3 (~10KB). Output contract,
    //                   schema, anti-patterns, refinement contract,
    //                   modulation plan, examples — the load-bearing rules
    //                   the model must always see.
    //   * `archetypes`— map of normalized keyword → recipe block text from
    //                   §3.0..3.N. Loaded lazily and appended to the rails
    //                   when the user prompt mentions a matching keyword.
    //
    // PURE function: callers pass the original system prompt text in.
    // Behaviour-neutral for this commit — the production wiring still uses
    // the full prompt (see useRailsOnlyPrompt below); the helper is here so
    // a follow-up phase can flip the flag once the keyword-match harness is
    // in place.
    struct SplitPrompt {
        std::string rails;
        // Keyword (lower-cased, single token) → recipe block (the markdown
        // text between the recipe's leading "N. **Name**" line and the
        // start of the next list item). One keyword may map to one recipe.
        std::vector<std::pair<std::string, std::string>> archetypes;
    };
    [[nodiscard]] static SplitPrompt splitSystemPrompt(const std::string& full);

    // Flag (default OFF) that selects between the legacy full-prompt path
    // and the rails-only + lazy-archetype path. Wired but not yet flipped —
    // flipping the default ON is a follow-up phase after the keyword-match
    // harness lands. Test seam: setUseRailsOnlyPrompt(true) in unit tests
    // can validate the split path end-to-end.
    void setUseRailsOnlyPrompt(bool on) noexcept { use_rails_only_ = on; }
    [[nodiscard]] bool useRailsOnlyPrompt() const noexcept { return use_rails_only_; }

    // Per-dimension parameter bias derived from session memory.
    [[nodiscard]] PatchVector getParameterBias(const std::string& userPrompt) const;

    // Phase C failure-state UX (#269) — inject a "report failure to UI"
    // sink. Called from generateLlmPatch when:
    //   * both LLM paths fail AND RAG fallback ships a real archetype
    //     → kind="llm_offline"
    //   * archetype retrieval falls back to `default_init` (no tag match)
    //     → kind="prompt_unclear"
    // Callable from any thread; AgentBridge::notifyFailure marshals onto
    // the JUCE message thread via callAsync. Default sink is empty so
    // unit tests that don't wire the sink stay silent.
    using FailureSink = std::function<void(const std::string& kind, const std::string& detail)>;
    void setFailureSink(FailureSink sink) noexcept { failureSink_ = std::move(sink); }

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
    // Phase 33 — rails-only system prompt flag. OFF by default; flip ON in
    // a follow-up phase after telemetry confirms the §3 split doesn't
    // erode patch quality.
    bool use_rails_only_{false};
    // Phase C failure-state UX (#269) — optional UI failure surface.
    FailureSink failureSink_{};
    // Phase 34b — LLM-delta-nudger over RAG retrieval. Non-owning; supplied
    // by AgentBridge (or unit tests). Nullptr / disabled() == fall straight
    // through to the Phase 34a top-1 archetype path.
    mapper::DeltaNudger* deltaNudger_{nullptr};
};

} // namespace agentic_synth::agent
