#pragma once

#include <array>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <juce_core/juce_core.h>

#include "agent/PrePatchPipeline.h"
#include "agent/SessionMemory.h"
#include "agent/StreamParser.h"
#include "agent/Telemetry.h"
#include "engine/PatchStruct.h"
#include "engine/VariationEngine.h"
#include "mapper/GrammarSampler.h"
#include "mapper/SemanticMapper.h"

namespace agentic_synth::agent {

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
    [[nodiscard]] std::optional<PatchStruct> generateLlmPatch(const std::string& prompt, uint32_t patch_id = 0);

    // Issue #67: streaming patch application.
    // Feed a chunk of streaming LLM JSON. Fires pipeline_.injectPatch() each
    // time a top-level field is complete, delivering first audible change
    // well within 500 ms of stream start.
    void feedChunk(std::string_view chunk);

    // Issue #76: called by MidiHandler for every CC message.
    // Tracks CC movements and biases future AI patch generation accordingly.
    void onMidiCC(int controller, int value) noexcept;

    // ── Issue #72: Bidirectional knob bridge ──────────────────────────────────

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
    [[nodiscard]] std::string generateRationale(const std::string& prompt, const PatchStruct& patch) const;

    // ── Phase 6C: in-browser audition keyboard ────────────────────────────────
    //
    // Queue a one-shot MIDI note for the synth engine. note ∈ [0,127],
    // velocity ∈ [0,1], durationMs is when the matched note-off should fire.
    // Posts a noteOn immediately on the JUCE message thread and schedules
    // the matching noteOff via Timer::callAfterDelay.
    //
    // The actual VoiceManager lives in the AudioProcessor, not on
    // AgentBridge. Wiring is done by the AudioProcessor at construction via
    // setMidiNoteSink — when no sink is registered (e.g. headless tests)
    // postMidiNote logs and returns. This keeps AgentBridge free of any
    // direct engine dependency.
    using MidiNoteSink = std::function<void(int note, float velocity, bool isNoteOn)>;
    void setMidiNoteSink(MidiNoteSink sink);
    void postMidiNote(int note, float velocity, int durationMs);

    // ── Typed callback subscription API ──────────────────────────────────────
    //
    // RAII subscription: returned handle owns the slot; when it goes out of
    // scope the callback is unregistered automatically.  Callbacks are
    // marshalled to the JUCE message thread via MessageManager::callAsync so
    // emission sites may run on any thread (audio thread excluded — never
    // emit from the audio callback).

    using Callback = std::function<void(const juce::var&)>;
    using SubscriberHandle = std::shared_ptr<void>;

    [[nodiscard]] SubscriberHandle onToken(Callback cb);
    [[nodiscard]] SubscriberHandle onPatch(Callback cb);
    [[nodiscard]] SubscriberHandle onDone(Callback cb);
    [[nodiscard]] SubscriberHandle onError(Callback cb);
    [[nodiscard]] SubscriberHandle onRationale(Callback cb);
    [[nodiscard]] SubscriberHandle onSuggestVariations(Callback cb);
    [[nodiscard]] SubscriberHandle onPatchUpdate(Callback cb);
    [[nodiscard]] SubscriberHandle onTranscript(Callback cb);

    // Test/integration emission helpers — visible so call sites in this
    // translation unit and the test fixture can drive the subscriber fan-out
    // directly.
    void notifyToken(const juce::var& payload);
    void notifyPatch(const juce::var& payload);
    void notifyDone(const juce::var& payload);
    void notifyError(const juce::var& payload);
    void notifyRationale(const juce::var& payload);
    void notifySuggestVariations(const juce::var& payload);
    void notifyPatchUpdate(const juce::var& payload);
    void notifyTranscript(const juce::var& payload);

private:
    using CallbackPtr = std::shared_ptr<Callback>;
    using SlotList = std::vector<std::weak_ptr<Callback>>;

    SubscriberHandle subscribe(SlotList& slots, Callback cb);
    void dispatch(SlotList& slots, const juce::var& payload);

    PrePatchPipeline pipeline_;
    engine::VariationEngine variationEngine_;
    SessionMemory memory_;
    mapper::GrammarSampler sampler_{mapper::GrammarSamplerConfig{}};
    mapper::SemanticMapper semanticMapper_;
    StreamParser streamParser_;
    Telemetry telemetry_{Telemetry::defaultLogPath()};

    // Subscriber slot lists, guarded by a single mutex.  Each slot holds a
    // weak_ptr to the Callback; the SubscriberHandle (an aliased shared_ptr)
    // owns the strong reference and unsubscribes via destruction.
    mutable std::mutex subscribersMutex_;
    SlotList tokenSlots_;
    SlotList patchSlots_;
    SlotList doneSlots_;
    SlotList errorSlots_;
    SlotList rationaleSlots_;
    SlotList suggestVariationsSlots_;
    SlotList patchUpdateSlots_;
    SlotList transcriptSlots_;

    // Written by the MIDI/audio thread; read by UI/control thread — must be atomic.
    std::atomic<float> midiCutoffNorm_{0.5f};
    std::atomic<float> midiResonanceNorm_{0.0f};

    // Phase 6C: optional in-browser-audition note sink. Owned externally
    // (typically by AgenticSynthPlugin). Mutex guards swap-during-callback;
    // invocations always happen on the JUCE message thread.
    mutable std::mutex midiSinkMutex_;
    MidiNoteSink midiNoteSink_;

    // Tripwire: stamped by pollPatch() so dispatch() can assert it is never
    // reached from the audio thread (callAsync + var copies allocate).
    std::atomic<juce::Thread::ThreadID> audioThreadId_{nullptr};

    // Count of dispatch() calls that were short-circuited because they
    // originated on the audio thread.  Bumped before any allocation so the
    // RT thread stays clean; Telemetry reads this lazily from the UI thread.
    std::atomic<uint64_t> droppedFromAudioThread_{0};

public:
    // Called by AudioProcessor::processBlock immediately to register the RT
    // thread for the lifetime of the audio engine.  Safe to call repeatedly.
    void markAudioThread() noexcept {
        audioThreadId_.store(juce::Thread::getCurrentThreadId(),
                             std::memory_order_relaxed);
    }

    // Number of dispatch() invocations dropped because they originated on
    // the audio thread.  Exposed for tests and future Telemetry hookup.
    [[nodiscard]] uint64_t audioThreadDropCount() const noexcept {
        return droppedFromAudioThread_.load(std::memory_order_relaxed);
    }
};

} // namespace agentic_synth::agent
