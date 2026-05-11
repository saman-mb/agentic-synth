#pragma once

#include <optional>
#include <string>

#include "engine/PatchStruct.h"

namespace agentic_synth::mapper {

struct GrammarSamplerConfig {
    // llama.cpp server base URL (no trailing slash)
    std::string server_url{"http://127.0.0.1:8080"};
    // Path to patch_struct.gbnf; loaded once at construction
    std::string grammar_path;
    // Synth domain system prompt text (loaded from system-prompt.md or passed directly)
    std::string system_prompt;
    int max_tokens{1024};
    float temperature{0.35f};
    int timeout_ms{15000};
};

// Calls a llama.cpp /completion server with GBNF grammar-constrained sampling
// to generate structurally valid PatchStruct JSON.
class GrammarSampler {
public:
    explicit GrammarSampler(GrammarSamplerConfig cfg);

    // Generate a PatchStruct from a free-form natural-language prompt.
    // Returns nullopt on HTTP failure, JSON parse error, or range violation.
    [[nodiscard]] std::optional<PatchStruct> generate(const std::string& user_prompt, uint32_t patch_id = 0) const;

    // Parse a GBNF-constrained JSON string into a PatchStruct.
    // Pure function — no network calls. Used for unit tests and offline validation.
    [[nodiscard]] static std::optional<PatchStruct> parse_patch_json(const std::string& json_text);

    // The synth-domain system prompt text loaded at construction.
    [[nodiscard]] const std::string& systemPrompt() const noexcept { return cfg_.system_prompt; }

private:
    GrammarSamplerConfig cfg_;
    std::string grammar_text_; // content of the .gbnf file

    // Build llama.cpp /completion request body (JSON)
    [[nodiscard]] std::string build_request(const std::string& user_prompt, uint32_t patch_id) const;

    // Blocking HTTP POST; returns response body or empty string on error
    [[nodiscard]] std::string http_post(const std::string& json_body) const;

    // Extract "content" field from a llama.cpp /completion JSON response
    [[nodiscard]] static std::string extract_content(const std::string& response_json);
};

} // namespace agentic_synth::mapper
