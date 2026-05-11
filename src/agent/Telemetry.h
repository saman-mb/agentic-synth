#pragma once

#include <cstdint>
#include <mutex>
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

// Structured WebView / UI lifecycle event (P1 SRE review).
// Captured even in release builds so production WebView health is observable.
struct UiEvent {
    int64_t ts_ms{0};
    std::string kind;   // "page_about_to_load" | "page_finished_loading" | "page_load_error"
    std::string detail; // url or error string
};

// Issue #91: Opt-in, local-only telemetry.
// All data stays in log_path; nothing is ever transmitted externally.
// Wire through AgentBridge::getTelemetryJson() and setTelemetryEnabled()
// (exposed to the UI via WebUiComponent's get_telemetry / set_telemetry_enabled
// native functions).
class Telemetry {
public:
    explicit Telemetry(std::string log_path = {});

    void setEnabled(bool on) noexcept { enabled_ = on; }
    [[nodiscard]] bool isEnabled() const noexcept { return enabled_; }

    // Record a completed generation attempt.
    // elapsed_s = total generation wall-clock time (for tokens/s computation).
    void recordGeneration(double latency_ms, int tokens, double elapsed_s, bool success, std::string error_type = {});

    // Record a WebView lifecycle event. Called from the JUCE message thread but
    // guarded with a mutex so any future off-thread callers remain safe.
    // No-op when telemetry is disabled (matches recordGeneration semantics).
    void recordUiEvent(std::string kind, std::string detail);

    // Serialize all records + computed summary to JSON.
    [[nodiscard]] std::string toJson() const;

    // Append all records to log_path_ (no-op when disabled or path empty).
    void flush();

    [[nodiscard]] const std::vector<GenerationRecord>& records() const noexcept { return records_; }

    // Snapshot of the current UI-event ring buffer (oldest first).
    [[nodiscard]] std::vector<UiEvent> uiEvents() const;

    // Ring-buffer capacity for UI events. Drops oldest when exceeded.
    static constexpr size_t kUiEventCap = 256;

    // Platform-appropriate log directory (created on first call).
    // Used as the default log_path so the file never lands in the DAW's CWD.
    // Multiple plugin instances get unique filenames via process ID.
    [[nodiscard]] static std::string defaultLogPath();

private:
    bool enabled_{false};
    std::string log_path_;
    std::vector<GenerationRecord> records_;
    std::vector<UiEvent> ui_events_;
    mutable std::mutex ui_mutex_;

    [[nodiscard]] static int64_t nowMs() noexcept;
};

} // namespace agentic_synth::agent
