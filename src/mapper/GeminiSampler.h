#pragma once

#include <optional>
#include <string>

#include "engine/PatchStruct.h"

namespace agentic_synth::mapper {

struct GeminiSamplerConfig {
    // Google Generative Language API key (from GEMINI_KEY env / .env file).
    // When empty the sampler is disabled and generate() returns nullopt.
    std::string api_key;
    // Model id. gemini-2.5-flash is the cheap/fast default; callers can
    // override for higher-quality runs.
    std::string model{"gemini-2.5-flash"};
    // Synth-domain system prompt; concatenated with the user prompt as the
    // single "text" part. Matches the GrammarSampler approach so prompts
    // built by PromptHandler::buildSystemPrompt work unchanged.
    std::string system_prompt;
    // Sampling temperature passed in generationConfig.
    float temperature{0.35f};
    // Hard ceiling on curl wall-time. Matches GrammarSampler's 15s default.
    int timeout_ms{15000};
    // Cap on output tokens. Gemini 2.5 Flash defaults to 8192 but reasoning
    // tokens count against the budget — with the ~80KB synth system prompt
    // the model can burn the whole budget "thinking" and emit empty parts.
    // 4096 leaves enough headroom for the JSON patch.
    int max_output_tokens{4096};
    // Safety filter threshold applied to all 4 HARM_CATEGORY_* buckets.
    // BLOCK_ONLY_HIGH pre-empts false-positive blocks on horror / Kubrick /
    // dread / Vangelis-style prompt keywords.
    std::string safety_threshold{"BLOCK_ONLY_HIGH"};
};

// Cloud fallback sampler. POSTs to Google's Gemini `generateContent`
// endpoint over HTTPS via a `curl` subprocess (no libssl/libcurl link
// dependency) and parses the returned JSON through the existing
// GrammarSampler::parse_patch_json validator.
//
// Threading: synchronous + blocking, intended to run on the same agent
// worker thread that drives GrammarSampler — never call from the audio
// thread.
class GeminiSampler {
public:
    explicit GeminiSampler(GeminiSamplerConfig cfg);

    // Returns nullopt when the API key is missing, the HTTPS request fails,
    // the response is malformed, or the returned patch fails range
    // validation. Logs a short reason to stderr in each failure mode so the
    // caller can see why a fallback path was rejected.
    [[nodiscard]] std::optional<PatchStruct> generate(const std::string& user_prompt, uint32_t patch_id = 0) const;

    [[nodiscard]] bool enabled() const noexcept { return !cfg_.api_key.empty(); }

    // Post-construction key wiring. AgentBridge looks up GEMINI_KEY at
    // startup via mapper::loadEnvKey and pokes it in here — keeps the
    // member's default-construction (no key) usable while still letting
    // the live agent reach the API.
    void setApiKey(std::string key) noexcept { cfg_.api_key = std::move(key); }
    void setSystemPrompt(std::string sp) noexcept { cfg_.system_prompt = std::move(sp); }

private:
    GeminiSamplerConfig cfg_;

    // Build the v1beta generateContent request body (JSON).
    [[nodiscard]] std::string build_request(const std::string& user_prompt, uint32_t patch_id) const;

    // Shell out to `curl` to issue an HTTPS POST. Returns response body or
    // empty string on error.
    [[nodiscard]] std::string http_post(const std::string& url, const std::string& json_body) const;

    // Variant exposing the curl exit code so the retry loop can distinguish
    // network-level failures from API-side error envelopes returned in the
    // body. `exit_code` is 0 on success, non-zero when curl itself failed
    // (DNS, timeout, SSL, transport error). The body is still surfaced when
    // present (curl --fail-with-body keeps the response on 4xx/5xx).
    [[nodiscard]] std::string http_post_ex(const std::string& url, const std::string& json_body, int& exit_code) const;

    // Extract the first candidate's text payload from the Gemini response.
    [[nodiscard]] static std::string extract_text(const std::string& response_json);
};

} // namespace agentic_synth::mapper
