#pragma once

#include <atomic>
#include <string>

#include "agent/PrePatchPipeline.h"
#include "agent/SessionMemory.h"

namespace agentic_synth::agent {

// Phase 12A: extracted from AgentBridge to address god-class concerns
// (continuation of the Phase 10C split — see DictionaryService /
// TelemetryService).
//
// Single responsibility: the real-time knob-tweak path. Owns the two
// atomics that track the user's current MIDI CC state for filter cutoff
// (CC74) and resonance (CC71) and applies single-parameter PatchDelta
// updates to the audio pipeline inside the < 16 ms target.
//
// Composition (not inheritance): AgentBridge owns one KnobBridge that
// composes non-owning references to PrePatchPipeline and SessionMemory.
// The MIDI CC atomics live here because they are the natural data the
// knob/CC path owns; PromptHandler reads them through const-ref
// accessors so the prompt-builder + rationale code can reflect the
// user's current timbral state without taking a write-side dependency.
//
// Audio-thread contract: handleKnobTweak / onMidiCC are called from the
// UI / MIDI threads. injectPatch is the existing SPSC-queue producer
// path (PrePatchPipeline serialises via producerMutex_) so the atomics
// + injectPatch contract is unchanged from the AgentBridge inline version.
class KnobBridge {
public:
    KnobBridge(PrePatchPipeline& pipeline, SessionMemory& memory) noexcept
        : pipeline_(pipeline), memory_(memory) {}

    // Apply one real-time knob change to the audio pipeline (target ≤ 16 ms).
    // Records the change as FeedbackKind::Tweak in session memory.
    void handleKnobTweak(const std::string& param, float value);

    // Called by MidiHandler for every CC message. Tracks CC74 (cutoff /
    // brightness) and CC71 (resonance); other controllers are ignored.
    void onMidiCC(int controller, int value) noexcept;

    // Read-only views of the latched MIDI CC state. Used by PromptHandler
    // to bias the system prompt + rationale toward the user's current
    // performance state.
    [[nodiscard]] float midiCutoffNorm() const noexcept {
        return midiCutoffNorm_.load(std::memory_order_relaxed);
    }
    [[nodiscard]] float midiResonanceNorm() const noexcept {
        return midiResonanceNorm_.load(std::memory_order_relaxed);
    }

private:
    PrePatchPipeline& pipeline_;
    SessionMemory& memory_;

    // Written by the MIDI/audio thread; read by UI/control thread — atomic.
    std::atomic<float> midiCutoffNorm_{0.5f};
    std::atomic<float> midiResonanceNorm_{0.0f};
};

} // namespace agentic_synth::agent
