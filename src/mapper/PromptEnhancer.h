#pragma once

#include <cstddef>
#include <list>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>

namespace agentic_synth::mapper {

struct PromptEnhancerConfig {
    // Google Generative Language API key. Empty disables the enhancer
    // (enhance() returns "" and callers fall back to the raw user prompt).
    std::string api_key;
    // gemini-2.5-flash-lite is the cheap/fast translator default; the
    // enhancer is small-prompt + short-output so the lite tier is plenty.
    std::string model{"gemini-2.5-flash-lite"};
    // Concatenated with the user prompt as the single "text" part. The
    // bundled TIMBRE translator prompt lives in src/mapper/enhancer-prompt.md
    // and is loaded by loadEnhancerPromptFile() at AgentBridge ctor time.
    std::string system_prompt;
    // Wider creative latitude than the generator — translator wants
    // colour, not numerical precision. Bumped from the generator's 0.35.
    float temperature{0.85f};
    // Hard ceiling on curl wall-time. 10s is half the generator budget; if
    // the enhancer is slow we'd rather skip it than make the user wait.
    int timeout_ms{10000};
};

// Two-step LLM flow (ENHANCER → GENERATOR):
//   1. PromptEnhancer rewrites a terse producer description ("dark dubstep
//      wobbly bass") into a 9-section plain-text sound-design brief.
//   2. GeminiSampler / GrammarSampler then receives the brief instead of
//      the raw prompt and emits the PatchStruct JSON.
//
// Threading: synchronous + blocking, intended to run on the same worker
// thread that drives GrammarSampler / GeminiSampler. Never call from the
// audio thread.
//
// Caching: a small LRU (32 entries) keyed by trim(lowercase(prompt))
// short-circuits repeated identical prompts so we don't burn quota on a
// user that hits "generate" twice in a row. Mutex-guarded for concurrent
// reads from the worker pool.
class PromptEnhancer {
public:
    explicit PromptEnhancer(PromptEnhancerConfig cfg);

    // Returns the enhanced brief (200–400 words, 9 sections) or "" on any
    // failure: no API key, HTTP failure, empty response, parse miss. The
    // caller (WebUiComponent worker job) treats "" as "fall back to the
    // raw user prompt".
    [[nodiscard]] std::string enhance(const std::string& userPrompt) const;

    [[nodiscard]] bool enabled() const noexcept { return !cfg_.api_key.empty(); }

    void setApiKey(std::string k) noexcept { cfg_.api_key = std::move(k); }
    void setSystemPrompt(std::string s) noexcept { cfg_.system_prompt = std::move(s); }

    // Pull the bundled TIMBRE enhancer briefing from disk. Mirrors
    // GrammarSampler::loadSystemPromptFile — tries an explicit override
    // first, then the compile-time-baked AGENTIC_SYNTH_ENHANCER_PROMPT_PATH,
    // then a dev-fallback cwd-relative path. Returns "" on all failures so
    // callers can fall back to a stub.
    [[nodiscard]] static std::string loadEnhancerPromptFile(const std::string& override_path = {});

private:
    PromptEnhancerConfig cfg_;

    // Issue the HTTPS POST. Returns response body or "" on error.
    [[nodiscard]] std::string http_post(const std::string& url, const std::string& json_body) const;

    // Variant exposing the curl exit code so the Phase 33 retry loop can
    // distinguish network failures from API error envelopes. Same body
    // returned as http_post(); exit_code == 0 on success, non-zero when
    // curl itself failed.
    [[nodiscard]] std::string http_post_ex(const std::string& url, const std::string& json_body,
                                           int& exit_code) const;

    // Extract candidates[0].content.parts[0].text from the Gemini response.
    [[nodiscard]] static std::string extract_text(const std::string& response_json);

    // Normalise a prompt into its cache key (trim + lowercase + collapse
    // internal whitespace). Different surface forms of the same prompt
    // should share a cache slot.
    [[nodiscard]] static std::string canonicalise(const std::string& s);

    // LRU cache: list holds (key, brief) pairs in access order (front =
    // most-recently-used); map points at list iterators for O(1) lookup
    // and O(1) promotion-to-front. Cap = 32 entries; on insert over cap
    // we drop the back.
    static constexpr std::size_t kCacheCap = 32;
    using CacheEntry = std::pair<std::string, std::string>;
    using CacheList = std::list<CacheEntry>;
    mutable std::mutex cache_mu_;
    mutable CacheList cache_lru_;
    mutable std::unordered_map<std::string, CacheList::iterator> cache_idx_;

    // Cache helpers — caller must hold cache_mu_.
    void cache_put_locked(const std::string& key, std::string value) const;
    std::string cache_get_locked(const std::string& key) const; // "" on miss
};

} // namespace agentic_synth::mapper
