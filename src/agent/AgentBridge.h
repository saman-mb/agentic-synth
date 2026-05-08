#pragma once

#include <array>
#include <optional>
#include <string>
#include <string_view>

#include "agent/PrePatchPipeline.h"
#include "agent/SessionMemory.h"
#include "agent/StreamParser.h"
#include "agent/Telemetry.h"
#include "engine/PatchStruct.h"
#include "engine/VariationEngine.h"
#include "mapper/GrammarSampler.h"
#include "mapper/SemanticMapper.h"

namespace agentic_synth::agent {

// Forward declaration — keeps JUCE out of this header.
class WebSocketBridge;

class AgentBridge {
public:
    AgentBridge();

    [[nodiscard]] std::string status() const;

    // Issue #68: pre-patch pipeline.
    // Parse heuristically and dispatch to audio thread immediately (< 200 ms).
    PatchStruct submitPrompt(const std::string& prompt);

    // Smoothly replace the heuristic patch with the LLM-refined result.
    void refinePatch(const PatchStruct& llmPatch);

    // Audio thread: pop the next pending patch.
    [[nodiscard]] std::optional<PatchStruct> pollPatch() noexcept;

    // Issue #69: variation engine.
    // Generate 5 audibly distinct variants from a base patch concurrently.
    [[nodiscard]] std::array<PatchStruct, engine::VariationEngine::kVariationCount>
    generateVariations(const PatchStruct& base) const;

    // Reproducible variation set via explicit perturbation seed.
    [[nodiscard]] std::array<PatchStruct, engine::VariationEngine::kVariationCount>
    generateVariationsWithSeed(const PatchStruct& base, uint32_t perturbSeed) const;

    // Session memory: record user feedback and build LLM context.
    void recordFeedback(FeedbackKind kind, const std::string& prompt, const PatchStruct& patch);
    [[nodiscard]] std::string buildSystemPrompt(const std::string& userPrompt) const;
    [[nodiscard]] PatchVector getParameterBias(const std::string& userPrompt) const;
    [[nodiscard]] const SessionMemory& sessionMemory() const noexcept { return memory_; }

    // Issue #64: grammar-constrained LLM patch generation.
    // Calls GrammarSampler, validates the result, then drives refinePatch().
    // Returns nullopt when the server is unreachable or returns invalid JSON.
    [[nodiscard]] std::optional<PatchStruct> generateLlmPatch(const std::string& prompt,
                                                               uint32_t patch_id = 0);

    // Issue #67: streaming patch application.
    // Feed a chunk of streaming LLM JSON. Fires pipeline_.injectPatch() each
    // time a top-level field is complete, delivering first audible change
    // well within 500 ms of stream start.
    void feedChunk(std::string_view chunk);

    // Issue #76: called by MidiHandler for every CC message.
    // Tracks CC movements and biases future AI patch generation accordingly.
    void onMidiCC(int controller, int value) noexcept;

    // ── Issue #72: Bidirectional knob bridge ──────────────────────────────────

    // Set the bridge so this class can send replies (dictionary, telemetry data).
    void setWebSocketBridge(WebSocketBridge* bridge) noexcept { wsb_ = bridge; }

    // Route all incoming WebSocket text frames here.
    // Dispatches: knob_tweak, get_dictionary, save_dictionary,
    //             get_telemetry, set_telemetry_enabled.
    void handleTextMessage(const std::string& json, int clientId);

    // Apply one real-time knob change to the audio pipeline (target ≤16 ms).
    // Records the change as FeedbackKind::Tweak in session memory.
    void handleKnobTweak(const std::string& param, float value);

    // ── Issue #90: Semantic dictionary ────────────────────────────────────────

    // All static + custom entries serialised as a dictionary_data JSON frame.
    [[nodiscard]] std::string getDictionaryJson() const;

    // Parse custom entries from a save_dictionary JSON frame and persist to disk.
    void saveDictionary(const std::string& json);

    // Load custom entries from disk (call once at startup).
    void loadDictionary(const std::string& path = "descriptor_dataset_custom.json");

    // ── Issue #91: Telemetry ──────────────────────────────────────────────────

    // Current telemetry serialised as a telemetry_data JSON frame.
    [[nodiscard]] std::string getTelemetryJson() const;

    void setTelemetryEnabled(bool on);

    [[nodiscard]] Telemetry& telemetry() noexcept { return telemetry_; }

    // ── Issue #85: Session-aware narrative generation ─────────────────────────

    // Generate natural-language rationale explaining parameter choices for
    // the given patch in the context of the current prompt and session memory.
    [[nodiscard]] std::string generateRationale(const std::string& prompt,
                                                const PatchStruct& patch) const;

private:
    PrePatchPipeline            pipeline_;
    engine::VariationEngine     variationEngine_;
    SessionMemory               memory_;
    mapper::GrammarSampler      sampler_;
    mapper::SemanticMapper      semanticMapper_;
    StreamParser                streamParser_;
    Telemetry                   telemetry_{"agentic_synth_telemetry.json"};
    WebSocketBridge*            wsb_{nullptr};

    // Normalised [0,1] last-seen values from MIDI CC; injected into system prompt.
    float midiCutoffNorm_{0.5f};
    float midiResonanceNorm_{0.0f};
};

} // namespace agentic_synth::agent
