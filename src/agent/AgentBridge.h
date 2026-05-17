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

#include "agent/DictionaryService.h"
#include "agent/KnobBridge.h"
#include "agent/PrePatchPipeline.h"
#include "agent/PromptHandler.h"
#include "agent/SessionMemory.h"
#include "agent/StreamParser.h"
#include "agent/Telemetry.h"
#include "agent/TelemetryService.h"
#include "engine/PatchStruct.h"
#include "engine/VariationEngine.h"
#include "agent/GeminiSTT.h"
#include "mapper/GeminiSampler.h"
#include "mapper/GrammarSampler.h"
#include "mapper/PromptEnhancer.h"
#include "mapper/SemanticMapper.h"

namespace agentic_synth::agent {

// Phase 10C / 12A god-object split — status:
//   * DictionaryService  — extracted (Phase 10C).
//   * TelemetryService   — extracted (Phase 10C).
//   * PromptHandler      — extracted (Phase 12A). Owns submitPrompt /
//                          refinePatch / generateLlmPatch / feedChunk /
//                          buildSystemPrompt / generateRationale /
//                          getParameterBias. AgentBridge methods are thin
//                          forwards; the streaming-parser callback wiring
//                          stays here because it has to also fan out to
//                          typed subscribers via notifyPatch.
//   * KnobBridge         — extracted (Phase 12A). Owns handleKnobTweak /
//                          onMidiCC + the midi cutoff/resonance atomics.
//                          The atomics moved out of AgentBridge; reads
//                          flow through KnobBridge::midi*Norm() accessors.
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
    //
    // Phase 22 — refinement context. previousPatch + previousPrompt carry
    // the last successful generation forward so that relative prompts
    // ("darker", "more wobble") become NUDGES instead of fresh generations.
    // The detection (PromptHandler::isRelativePrompt) and request wrapping
    // live in the handler; this is a thin forward.
    [[nodiscard]] std::optional<PatchStruct>
    generateLlmPatch(const std::string& prompt, uint32_t patch_id = 0,
                     std::optional<PatchStruct> previousPatch = std::nullopt,
                     std::optional<std::string> previousPrompt = std::nullopt);

    // Phase 31 — heuristic-fallback guardrail forward. Worker invokes this on
    // the LLM-failure branch so the Phase 23/27/30 PatchAugmenter still fires
    // on the bare heuristic patch from submitPrompt(). Refinement-skip rule
    // matches generateLlmPatch: when the prompt is relative AND a prior patch
    // exists, the augmenter is skipped. See PromptHandler.h for full notes.
    void applyGuardrailIfNotRefinement(PatchStruct& patch, const std::string& prompt,
                                       bool hasPreviousPatch) noexcept;

    // Two-step LLM flow: ENHANCER step. Rewrites a terse user prompt into a
    // 9-section plain-text sound-design brief that the generator (above)
    // then receives instead of the raw prompt. Returns "" when the enhancer
    // is disabled (no GEMINI_KEY) or the HTTPS call fails — callers fall
    // back to the raw user prompt in that case.
    [[nodiscard]] std::string enhancePrompt(const std::string& userPrompt);

    // Phase 29 — speech-to-text via Gemini. Called from the WebUiComponent
    // push_audio_pcm worker after PCM decode. Returns the transcript or
    // empty string on failure / disabled state. Safe to call from a worker
    // thread; performs a synchronous HTTPS round-trip internally.
    [[nodiscard]] std::string transcribeAudio(const std::int16_t* samples, int numSamples,
                                              int sampleRate = 16000) const;

    [[nodiscard]] bool sttEnabled() const noexcept { return stt_.enabled(); }

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

    [[nodiscard]] Telemetry& telemetry() noexcept { return telemetry_.telemetry(); }

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
    // Open-ended hold: noteOn fires now, no scheduled noteOff. Caller must
    // emit postMidiNoteOff(note) when the gesture ends. Used by the audition
    // keyboard so pointer/key hold maps to sustained note.
    void postMidiNoteOn(int note, float velocity);
    void postMidiNoteOff(int note);

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
    // Two-step LLM flow: emits the enhanced brief produced by the
    // PromptEnhancer step so the chat UI can show it in the live ticker
    // (HEARING IT OUT → SHAPING) and keep it on the assistant message's
    // rationale block after generation completes.
    [[nodiscard]] SubscriberHandle onEnhancement(Callback cb);
    // Phase B simple-view (#249) — fan-out for the 5-variation morph result.
    // Payload shape: { variations: [{label, patch, modulation}, ...] }.
    // Distinct from `suggest_variations` (proactive 3-suggestion stream) — this
    // fires once per explicit user "more variations" request.
    [[nodiscard]] SubscriberHandle onVariationsReady(Callback cb);
    // Phase C failure-state UX (#269) — first-class user-facing surface for
    // LLM-offline / prompt-unclear / safety-block. The kind drives the
    // banner copy in the UI; `detail` (optional) is opaque engineering text
    // the user can choose to expand. mic_denied stays handled inline by
    // PushToTalk; emitting it here is reserved for future routing.
    [[nodiscard]] SubscriberHandle onFailure(Callback cb);

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
    void notifyEnhancement(const juce::var& payload);
    void notifyVariationsReady(const juce::var& payload);
    void notifyFailure(const juce::var& payload);

    // Full patch wire helpers used by native UI events and tests. The shape
    // mirrors React PatchParams: nested modules with numeric enum/bool fields.
    [[nodiscard]] static juce::var patchToVar(const PatchStruct& patch);
    [[nodiscard]] static PatchStruct patchFromVar(const juce::var& payload);
    [[nodiscard]] static juce::var modulationPlanForPatch(const PatchStruct& patch);

private:
    using CallbackPtr = std::shared_ptr<Callback>;
    using SlotList = std::vector<std::weak_ptr<Callback>>;

    SubscriberHandle subscribe(SlotList& slots, Callback cb);
    void dispatch(SlotList& slots, const juce::var& payload);

    PrePatchPipeline pipeline_;
    engine::VariationEngine variationEngine_;
    SessionMemory memory_;
    mapper::GrammarSampler sampler_{mapper::GrammarSamplerConfig{}};
    // Constructed in AgentBridge() after looking up GEMINI_KEY via
    // mapper::loadEnvKey — disabled (enabled()==false) when no key is found
    // so the fallback degrades silently in unit-test/CI environments that
    // don't ship credentials.
    mapper::GeminiSampler gemini_{mapper::GeminiSamplerConfig{}};
    // Step 1 of the 2-step LLM flow (translator → generator). Constructed
    // empty; AgentBridge() injects GEMINI_KEY + the enhancer-prompt.md
    // briefing at startup, mirroring how gemini_ is configured.
    mapper::PromptEnhancer enhancer_{mapper::PromptEnhancerConfig{}};
    // Phase 29 — Gemini STT for push-to-talk. Wired from GEMINI_KEY at
    // construction the same way enhancer_ / gemini_ are. Stays disabled
    // (transcribe returns empty string) when no key is set.
    agent::GeminiSTT stt_;
    mapper::SemanticMapper semanticMapper_;
    StreamParser streamParser_;

    // Phase 10C / 12A extracted services (see header comment above). Each
    // owns its own state; AgentBridge forwards the public API to them.
    //
    // Declaration order is load-bearing: services hold non-owning references
    // to the members above and to each other, so each one MUST come after
    // every dependency it captures.
    //   * dictionary_   -> semanticMapper_
    //   * knob_         -> pipeline_, memory_
    //   * prompt_       -> pipeline_, sampler_, semanticMapper_,
    //                      streamParser_, memory_, knob_
    TelemetryService telemetry_{Telemetry::defaultLogPath()};
    DictionaryService dictionary_{semanticMapper_};
    KnobBridge knob_{pipeline_, memory_};
    PromptHandler prompt_{pipeline_, sampler_, gemini_, semanticMapper_, streamParser_, memory_, knob_};

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
    SlotList enhancementSlots_;
    SlotList variationsReadySlots_;
    SlotList failureSlots_;

    // Phase 12A: midi cutoff/resonance atomics moved to KnobBridge (knob_);
    // read via knob_.midiCutoffNorm() / knob_.midiResonanceNorm().

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
        audioThreadId_.store(juce::Thread::getCurrentThreadId(), std::memory_order_relaxed);
    }

    // Number of dispatch() invocations dropped because they originated on
    // the audio thread.  Exposed for tests and future Telemetry hookup.
    [[nodiscard]] uint64_t audioThreadDropCount() const noexcept {
        return droppedFromAudioThread_.load(std::memory_order_relaxed);
    }
};

} // namespace agentic_synth::agent
