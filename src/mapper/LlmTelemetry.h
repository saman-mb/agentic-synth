#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>

namespace agentic_synth::mapper {

// Phase 33: structured per-call telemetry for the Gemini path.
//
// Append-only JSONL sink — one line per LLM call, lazily created in the
// platform-appropriate app data directory. Mapper-layer (mirrors
// agent/Telemetry's defaultLogPath resolution but lives below the agent
// library in the dep graph so GeminiSampler / PromptEnhancer can reach it
// without inverting the existing engine/mapper/agent stack).
//
// All writes are mutex-guarded, std::ofstream-in-append-mode, and SWALLOW
// every IO error. Logging must never throw or take a slow path that
// observable callers can notice — telemetry is opt-out infrastructure, not
// load-bearing logic.
struct LlmCall {
    // Wall-clock ISO-8601 timestamp at write time (UTC). Filled by log().
    std::string ts;
    // "GeminiSampler" | "PromptEnhancer" | "GeminiSTT".
    std::string caller;
    // cfg_.model — e.g. "gemini-2.5-flash" / "gemini-2.5-flash-lite".
    std::string model;
    // 1-N attempts taken (post-retry — captures the final count).
    int attempts{1};
    // Total wall-clock from first POST to final outcome.
    double latency_ms{0.0};
    // Final response body size in bytes (0 when curl returned nothing).
    std::size_t body_size_bytes{0};
    // Total request body size in bytes (final attempt — proxy for prompt size).
    std::size_t prompt_size_bytes{0};
    // From candidate finishReason — "STOP" / "SAFETY" / "MAX_TOKENS" / "" if empty.
    std::string finish_reason;
    // From promptFeedback.blockReason if present.
    std::string block_reason;
    // "success" | "empty" | "blocked" | "truncated" | "curl_error".
    std::string outcome;
};

class LlmTelemetry {
public:
    // Process-wide sink. Path resolved lazily on first log() — same algo as
    // agent::Telemetry::defaultLogPath() but lands at
    //   <appDataDir>/AgenticSynth/llm_telemetry.jsonl
    // so multiple plugin instances share one append log (each line has the
    // pid + caller, so attribution is preserved).
    static LlmTelemetry& instance();

    // Append one JSON object + newline to the log. Never throws. No-ops
    // when the log path can't be created.
    void log(const LlmCall& call) noexcept;

    // Resolved log file path (may be empty until first log()).
    [[nodiscard]] const std::string& path() const noexcept { return path_; }

    // Test seam: override the on-disk path. Pass empty to reset to lazy.
    void setPathForTest(std::string p) noexcept;

private:
    LlmTelemetry() = default;

    std::mutex mu_;
    std::string path_;
    bool resolved_{false};

    // Compute <appDataDir>/AgenticSynth/llm_telemetry.jsonl.
    static std::string defaultPath();
};

} // namespace agentic_synth::mapper
