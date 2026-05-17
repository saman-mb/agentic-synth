#pragma once

#include <cstdint>
#include <mutex>
#include <string>

namespace agentic_synth::agent {

// Phase H / #261 — append-only JSONL morph telemetry.
//
// Sink mirrors mapper::LlmTelemetry's pattern (Phase 33): a process-wide
// singleton, lazy path resolution to <appDataDir>/AgenticSynth/morph_telemetry.jsonl,
// mutex-guarded ofstream-in-append-mode writes. log() never throws and swallows
// every IO error so telemetry can't observably slow callers down.
//
// One JSONL line per event. Six event kinds — see MorphEventKind below — chosen
// to answer the product questions in #261 (which morph strategies get picked,
// how often users hit "More variations", which axis they nudge, dwell time per
// variation, A/B toggle rate, preset commit rate, bounce-to-wav rate).
//
// Anonymization (per Phase 33 PromptSanitizer / LlmTelemetry policy):
//   * No raw prompts. Hash → first 8 hex chars of SHA-256, deterministic per
//     prompt so the rough cardinality / repeat-rate is recoverable without
//     exposing the text.
//   * No preset names — name_length only.
//   * No transcripts, no raw audio, no PII.

enum class MorphEventKind {
    MorphRequested,   // user clicked "More variations" or sent a fresh prompt
    VariationPicked,  // user promoted a thumbnail to dominant
    MacroTweaked,     // user moved one of the 4 hero macros
    ABToggled,        // user toggled A/B compare
    PresetCommitted,  // Phase D "Keep this sound"
    BounceToWav,      // Phase D bounce-to-wav
};

// Single-record envelope. Most fields are kind-specific — see the per-record
// helpers on MorphTelemetry below. Wire format is a flat JSON object; consumers
// (analytics scripts, dashboards) match on `kind` then read the populated
// fields, ignoring the unset numeric defaults.
struct MorphEvent {
    MorphEventKind kind{MorphEventKind::MorphRequested};
    std::string ts;            // ISO-8601 UTC, filled by log() if empty

    // MorphRequested.
    std::string prompt_hash;
    int history_size{0};
    int liked_size{0};

    // VariationPicked.
    int strategy_id{-1};       // 0..4 — index into MorphLoop's strategy array
    std::string label;         // e.g. "warmer", "brighter", "A", "B"
    int time_since_arrival_ms{0};

    // MacroTweaked.
    int macro_index{-1};       // 0..3 (BRIGHTNESS / WOBBLE / EDGE / AIR)
    float value{0.0f};
    int dwell_ms{0};

    // ABToggled.
    int from_slot{-1};
    int to_slot{-1};

    // PresetCommitted.
    int name_length{0};        // anonymised — the name itself never logged

    // BounceToWav.
    float duration_s{0.0f};
};

class MorphTelemetry {
public:
    // Process-wide sink. Path resolved lazily on first log().
    [[nodiscard]] static MorphTelemetry& instance();

    void setEnabled(bool on) noexcept { enabled_ = on; }
    [[nodiscard]] bool isEnabled() const noexcept { return enabled_; }

    // Append one JSONL line. No-throw, no-op when disabled or path empty.
    void log(const MorphEvent& ev) noexcept;

    // Resolved log file path (may be empty until first log()).
    [[nodiscard]] std::string path() const;

    // Test seam: override the on-disk path. Pass empty to reset to lazy
    // default resolution.
    void setPathForTest(std::string p) noexcept;

    // SHA-256 of `prompt`, first 8 hex chars. Cheap, deterministic per prompt,
    // no PII leak. Exposed so callers (AgentBridge methods) can pre-hash the
    // prompt without going through a MorphEvent constructor.
    [[nodiscard]] static std::string hashPrompt(const std::string& prompt);

private:
    MorphTelemetry() = default;

    mutable std::mutex mu_;
    std::string path_;
    bool resolved_{false};
    // Defaults to true in dev (mirrors mapper::LlmTelemetry, which has no
    // gate). The user-facing opt-in toggle is follow-up — for the MVP the C++
    // flag defaults on and tests flip it off explicitly.
    bool enabled_{true};

    // Compute <appDataDir>/AgenticSynth/morph_telemetry.jsonl.
    [[nodiscard]] static std::string defaultPath();
};

} // namespace agentic_synth::agent
