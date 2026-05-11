#pragma once

#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "engine/PatchStruct.h"
#include "mapper/descriptor_dataset.h"

namespace agentic_synth::mapper {

// Issue #90: a user-defined (keyword, context, delta) triple with an owned keyword string.
// Stored in SemanticMapper::customEntries_; takes priority over the static dataset.
struct CustomEntry {
    std::string keyword;
    SoundContext context{SoundContext::Generic};
    PatchDelta delta;
};

// Configuration for SemanticMapper.
// Embedding queries go to the llama.cpp /embedding endpoint.
// When server_url is empty the mapper falls back to tokenised word-overlap
// similarity (no network required, suitable for offline / unit-test use).
struct SemanticMapperConfig {
    std::string server_url;  // e.g. "http://127.0.0.1:8080"
    int embedding_dims{384}; // all-MiniLM-L6-v2 dimensionality
    float similarity_threshold{0.25f};
    int timeout_ms{5000};
};

// Resolves free-form descriptors to the nearest entry in the curated dataset
// and applies the corresponding PatchDelta to a base PatchStruct.
//
// Context-aware: the same word ("dark", "bright") maps to different deltas
// depending on the SoundContext inferred from the prompt (bass / pad / lead …).
class SemanticMapper {
public:
    explicit SemanticMapper(SemanticMapperConfig cfg = {});

    // Apply mapper to base_patch, mutating it in place.
    // Returns the number of descriptors resolved (0 = no match found).
    int apply(const std::string& prompt, PatchStruct& base_patch) const;

    // Infer SoundContext from prompt keywords ("bass", "pad", "lead", …)
    [[nodiscard]] static SoundContext infer_context(const std::string& prompt);

    // Retrieve the best-matching DescriptorEntry for one descriptor word.
    // Uses embedding similarity when a server is configured, otherwise uses
    // word-overlap scoring.
    [[nodiscard]] std::optional<const DescriptorEntry*> best_match(const std::string& descriptor,
                                                                   SoundContext ctx) const;

    // ── Issue #90: runtime dictionary editing ─────────────────────────────────

    // Load custom entries from a JSON file (call once at startup).
    void loadCustomEntries(const std::string& json_path);

    // Parse the "entries" array from a save_dictionary JSON frame,
    // update customEntries_, and persist them to json_path.
    void parseAndSaveCustomEntries(const std::string& json, const std::string& json_path);

    // Programmatically add / replace a custom entry.
    void addCustomEntry(CustomEntry e);

    // Dump all static + custom entries to a JSON array string.
    [[nodiscard]] std::string dumpAllToJson() const;

    [[nodiscard]] const std::vector<CustomEntry>& customEntries() const noexcept { return customEntries_; }

    // Phase 9B: prefetch embeddings for every static descriptor keyword so
    // that best_match() no longer triggers an HTTP round-trip per keyword
    // per query word. No-op when server_url is empty (offline / unit tests).
    void prewarmEmbeddings();

private:
    SemanticMapperConfig cfg_;
    std::vector<CustomEntry> customEntries_;

    // Phase 9B: lifetime-of-mapper embedding cache. Indexed by raw query
    // text (keyword or user descriptor). Empty vector is cached as well so
    // we don't re-hit the server for transient failures within one session.
    mutable std::unordered_map<std::string, std::vector<float>> embeddingCache_;
    mutable std::mutex embeddingCacheMutex_;

    // Tokenise prompt into lowercase words
    [[nodiscard]] static std::vector<std::string> tokenise(const std::string& prompt);

    // Fetch embedding vector from llama.cpp /embedding endpoint
    [[nodiscard]] std::vector<float> fetch_embedding(const std::string& text) const;

    // Cosine similarity between two equal-length vectors (returns -1..1)
    [[nodiscard]] static float cosine(const std::vector<float>& a, const std::vector<float>& b) noexcept;

    // Word-overlap similarity score (fallback when no embedding server)
    [[nodiscard]] static float word_overlap_score(const std::string& query, std::string_view entry_keyword) noexcept;

    // HTTP POST to embedding endpoint; returns raw JSON response body
    [[nodiscard]] std::string http_post_embedding(const std::string& text) const;

    // Parse float array from llama.cpp /embedding JSON response
    [[nodiscard]] static std::vector<float> parse_embedding_json(const std::string& json);
};

} // namespace agentic_synth::mapper
