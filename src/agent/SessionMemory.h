#pragma once

#include <array>
#include <string>
#include <vector>

#include "engine/PatchStruct.h"

namespace agentic_synth::agent {

enum class FeedbackKind : uint8_t {
    Like = 0,
    Dislike,
    Tweak,
};

struct FeedbackEvent {
    FeedbackKind kind;
    std::string prompt;
    PatchStruct patch;
};

// 8-dimensional normalized patch parameter vector for similarity computation.
// Dimensions: [filter_cutoff_norm, resonance, attack_norm, sustain,
//              lfo_depth, reverb_mix, master_gain, osc0_volume]
using PatchVector = std::array<float, 8>;

class SessionMemory {
public:
    void recordFeedback(FeedbackKind kind, const std::string& prompt, const PatchStruct& patch);

    // Build a text recap of recent feedback for injection into the system prompt.
    [[nodiscard]] std::string buildRecap(const std::string& currentPrompt, int maxEvents = 5) const;

    // Compute per-dimension bias in [-1, +1].
    // Positive = boost that parameter region, negative = suppress.
    [[nodiscard]] PatchVector computeParameterBias(const std::string& currentPrompt) const;

    void clear();

    [[nodiscard]] const std::vector<FeedbackEvent>& events() const noexcept { return events_; }

    // Normalize filter cutoff to [0,1] on a log scale (20..20000 Hz).
    static float normalizeCutoff(float hz) noexcept;
    // Denormalize back to Hz.
    static float denormalizeCutoff(float norm) noexcept;

    static PatchVector extractVector(const PatchStruct& patch) noexcept;
    static float cosineSimilarity(const PatchVector& a, const PatchVector& b) noexcept;

    // Lightweight keyword-based prompt embedding (no external model required).
    static PatchVector promptToVector(const std::string& prompt) noexcept;

private:
    std::vector<FeedbackEvent> events_;

    static constexpr float kSimilarityThreshold = 0.3f;
};

} // namespace agentic_synth::agent
