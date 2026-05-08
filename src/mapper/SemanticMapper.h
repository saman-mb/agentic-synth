#pragma once

#include <optional>
#include <string>
#include <vector>

#include "engine/PatchStruct.h"
#include "mapper/descriptor_dataset.h"

namespace agentic_synth::mapper {

// Configuration for SemanticMapper.
// Embedding queries go to the llama.cpp /embedding endpoint.
// When server_url is empty the mapper falls back to tokenised word-overlap
// similarity (no network required, suitable for offline / unit-test use).
struct SemanticMapperConfig {
    std::string server_url;       // e.g. "http://127.0.0.1:8080"
    int embedding_dims{384};      // all-MiniLM-L6-v2 dimensionality
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
    [[nodiscard]] std::optional<const DescriptorEntry*>
    best_match(const std::string& descriptor, SoundContext ctx) const;

private:
    SemanticMapperConfig cfg_;

    // Tokenise prompt into lowercase words
    [[nodiscard]] static std::vector<std::string> tokenise(const std::string& prompt);

    // Fetch embedding vector from llama.cpp /embedding endpoint
    [[nodiscard]] std::vector<float> fetch_embedding(const std::string& text) const;

    // Cosine similarity between two equal-length vectors (returns -1..1)
    [[nodiscard]] static float cosine(const std::vector<float>& a,
                                      const std::vector<float>& b) noexcept;

    // Word-overlap similarity score (fallback when no embedding server)
    [[nodiscard]] static float word_overlap_score(const std::string& query,
                                                   std::string_view   entry_keyword) noexcept;

    // HTTP POST to embedding endpoint; returns raw JSON response body
    [[nodiscard]] std::string http_post_embedding(const std::string& text) const;

    // Parse float array from llama.cpp /embedding JSON response
    [[nodiscard]] static std::vector<float> parse_embedding_json(const std::string& json);
};

} // namespace agentic_synth::mapper
