#pragma once

#include <string>
#include <vector>

#include "engine/PatchStruct.h"
#include "mapper/ArchetypeLibrary.h"

namespace agentic_synth::mapper {

// Phase 34b — LLM-as-delta-nudger over RAG retrieval.
//
// The Phase 34a flow ships the highest-scoring archetype straight through on
// LLM failure. That's already strictly better than a bare heuristic patch,
// but it surrenders any nudge-shape personalization the LLM could provide
// for free. Phase 34b's MVP keeps the curated archetypes as the search
// frontier and only asks the LLM to:
//   (1) pick the best of the top-3 candidates for the user's prompt, and
//   (2) propose a small set of percentage nudges on whitelisted axes.
//
// Failure is silent and degrades to top-1 — the calling site treats
// selected_index < 0 as "use the existing fallback". CLAP embeddings and
// full param-vector regression are deferred to a multi-week follow-up.

// One percentage nudge against a single whitelisted parameter path. The
// path is a dotted/indexed string interpreted by applyNudges; delta_percent
// is the relative change (+/- bounded by applyNudges' per-axis cap before
// the patch validator clamps the absolute value).
struct Nudge {
    std::string path;
    float delta_percent{0.0f};
};

struct NudgeRequest {
    std::string prompt;
    std::vector<const Archetype*> top3;
};

struct NudgeResult {
    int selected_index{-1};       // 0..top3.size()-1 on success; -1 on failure
    PatchStruct patch{};          // selected archetype's patch + nudges applied
    std::vector<Nudge> nudges{};  // applied nudges (post-clamp, deduped)
    std::string rationale;        // optional sensory sentence from the model
};

// Apply each nudge to `base` in path order. Whitelisted paths only — unknown
// paths are silently ignored (logged). Per-axis caps mirror the Gemini
// system-prompt contract; the result is passed through validate_patch so the
// final values land in the engine's legal ranges no matter what the LLM
// proposed.
[[nodiscard]] PatchStruct applyNudges(const PatchStruct& base, const std::vector<Nudge>& nudges) noexcept;

// Configuration for the Gemini round-trip. Mirrors the subset of
// GeminiSamplerConfig the nudger needs — the nudger is a much cheaper call
// (small prompt, JSON-only output) so the defaults are tighter than the
// patch-generation sampler's.
struct DeltaNudgerConfig {
    std::string api_key;
    std::string model{"gemini-2.5-flash"};
    float temperature{0.2f};
    int timeout_ms{8000};
    int max_output_tokens{1024};
    std::string safety_threshold{"BLOCK_ONLY_HIGH"};
};

class DeltaNudger {
public:
    DeltaNudger() = default;
    explicit DeltaNudger(DeltaNudgerConfig cfg) noexcept : cfg_(std::move(cfg)) {}
    virtual ~DeltaNudger() = default;

    void setApiKey(std::string key) noexcept { cfg_.api_key = std::move(key); }

    [[nodiscard]] bool enabled() const noexcept { return !cfg_.api_key.empty(); }

    // Synchronous Gemini round-trip. Returns NudgeResult with selected_index
    // >= 0 on success, or { selected_index=-1, patch=top3[0]->patch, ... } on
    // any failure (no key, network, safety filter, malformed JSON, empty
    // top3). Caller treats failure as "use top-1".
    [[nodiscard]] NudgeResult nudge(const NudgeRequest& req) const;

protected:
    // Test seam — subclassable so unit tests can short-circuit the network
    // round-trip with a canned response body. Returns the raw Gemini response
    // body, or an empty string on transport failure.
    [[nodiscard]] virtual std::string http_post(const std::string& url, const std::string& json_body) const;

private:
    DeltaNudgerConfig cfg_;

    // Compose the user-facing payload (per-archetype JSON brief) shipped
    // alongside the static system prompt. Public-static for testability.
public:
    [[nodiscard]] static std::string buildUserMessage(const NudgeRequest& req);
    [[nodiscard]] static std::string buildSystemPrompt();

    // Parse a Gemini response body — pulls candidates[0].content.parts[0].text,
    // strips optional ``` fences, then extracts { selected_index, nudges,
    // rationale }. Returns selected_index=-1 on any parse failure.
    [[nodiscard]] static NudgeResult parseResponse(const std::string& response_body, int top3_size);
};

} // namespace agentic_synth::mapper
