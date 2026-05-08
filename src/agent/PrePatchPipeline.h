#pragma once

#include <chrono>
#include <optional>
#include <string>

#include "engine/PatchStruct.h"
#include "engine/PatchValidator.h"
#include "engine/SPSCQueue.h"
#include "mapper/HeuristicParser.h"

namespace agentic_synth::agent {

// Linearly interpolate all float fields between two PatchStructs.
// Non-float fields snap to `b` when t >= 0.5.
[[nodiscard]] PatchStruct lerpPatch(const PatchStruct& a, const PatchStruct& b, float t) noexcept;

// Pre-patch pipeline for Issue #68.
// Runs HeuristicParser immediately on submit for < 200 ms time-to-first-audio,
// then smoothly replaces parameters with the LLM-refined patch over ~50 ms.
class PrePatchPipeline {
public:
    // Number of interpolation steps pushed to the queue on refinePatch().
    // At a typical 512-sample block / 44100 Hz, 5 steps ≈ 58 ms of crossfade.
    static constexpr int kTransitionSteps = 5;

    // Parse heuristically and push to the audio-thread queue immediately.
    // Returns the dispatched patch. Measures dispatch latency internally.
    PatchStruct submit(const std::string& prompt);

    // Call when the LLM produces a refined patch. Pushes kTransitionSteps
    // linearly-interpolated patches from the last heuristic result to llmPatch,
    // providing ~50 ms of smooth parameter crossfade on the audio thread.
    void refinePatch(const PatchStruct& llmPatch);

    // Audio thread: pop the oldest pending patch from the queue.
    [[nodiscard]] std::optional<PatchStruct> poll() noexcept;

    // Milliseconds between the last submit() call and the queue push.
    // Must be < 200 ms to meet the time-to-first-audio requirement.
    [[nodiscard]] double lastDispatchLatencyMs() const noexcept;

    // Push a patch directly (no interpolation). Used for real-time user knob tweaks.
    void injectPatch(const PatchStruct& patch);

    [[nodiscard]] const PatchStruct& currentPatch() const noexcept { return lastHeuristicPatch_; }

private:
    agentsynth::HeuristicParser parser_;
    PatchQueue queue_;
    PatchStruct lastHeuristicPatch_{make_default_patch()};
    std::chrono::steady_clock::time_point submitTime_;
    double dispatchLatencyMs_{0.0};
};

} // namespace agentic_synth::agent
