#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace agentic_synth::agent {

// One completed LLM generation attempt.
struct GenerationRecord {
    int64_t timestamp_ms{0};
    double latency_ms{0.0}; // wall-clock: prompt submit → first patch push
    int token_count{0};
    double tokens_per_second{0.0};
    bool success{true};
    std::string error_type; // empty on success
};

// Issue #91: Opt-in, local-only telemetry.
// All data stays in log_path; nothing is ever transmitted externally.
// Wire through AgentBridge::handleTextMessage() (get_telemetry / set_telemetry_enabled).
class Telemetry {
public:
    explicit Telemetry(std::string log_path = {});

    void setEnabled(bool on) noexcept { enabled_ = on; }
    [[nodiscard]] bool isEnabled() const noexcept { return enabled_; }

    // Record a completed generation attempt.
    // elapsed_s = total generation wall-clock time (for tokens/s computation).
    void recordGeneration(double latency_ms, int tokens, double elapsed_s, bool success, std::string error_type = {});

    // Serialize all records + computed summary to JSON.
    [[nodiscard]] std::string toJson() const;

    // Append all records to log_path_ (no-op when disabled or path empty).
    void flush();

    [[nodiscard]] const std::vector<GenerationRecord>& records() const noexcept { return records_; }

private:
    bool enabled_{false};
    std::string log_path_;
    std::vector<GenerationRecord> records_;

    [[nodiscard]] static int64_t nowMs() noexcept;
};

} // namespace agentic_synth::agent
