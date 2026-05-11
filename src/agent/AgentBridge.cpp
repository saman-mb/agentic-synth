#include "agent/AgentBridge.h"

#include <algorithm>
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <utility>

namespace agentic_synth::agent {

namespace {

// Build a juce::var representation of a PatchStruct partial. Emit the shape
// React expects (PatchPreviewData in ui/src/types/chat.ts):
// {cutoffHz, resonance, attackS, sustainLevel, lfoDepth, reverbMix}.
// Snake-case fields would silently render an empty patch preview in the UI.
juce::var patchToVar(const PatchStruct& p) {
    auto* obj = new juce::DynamicObject{};
    obj->setProperty("cutoffHz", p.filter.cutoff_hz);
    obj->setProperty("resonance", p.filter.resonance);
    obj->setProperty("attackS", p.amp_env.attack_s);
    obj->setProperty("sustainLevel", p.amp_env.sustain);
    obj->setProperty("lfoDepth", p.lfo[0].depth);
    obj->setProperty("reverbMix", p.reverb.mix);
    return juce::var{obj};
}

} // namespace

AgentBridge::AgentBridge() {
    // Wire stream parser: each completed field injects a partial patch
    // directly onto the audio SPSC queue for < 500 ms first-audible-change.
    // Parallel emission to typed subscribers (Phase 2) — used by the
    // WebView bridge in Phase 4.  Legacy WSB path remains until Phase 5.
    streamParser_.setCallback([this](const PatchStruct& p) {
        pipeline_.injectPatch(p);
        // Fan out to typed subscribers so the WebView bridge can render the
        // streaming patch preview as fields complete.
        auto* obj = new juce::DynamicObject{};
        obj->setProperty("variation", juce::String("A"));
        obj->setProperty("data", patchToVar(p));
        notifyPatch(juce::var{obj});
    });
}

// ── Phase 2: subscription + dispatch plumbing ────────────────────────────────

AgentBridge::SubscriberHandle AgentBridge::subscribe(SlotList& slots, Callback cb) {
    auto holder = std::make_shared<Callback>(std::move(cb));
    {
        std::lock_guard<std::mutex> lock(subscribersMutex_);
        // Compact tombstones opportunistically so the slot list does not
        // grow without bound under churn.
        slots.erase(
            std::remove_if(slots.begin(), slots.end(), [](const std::weak_ptr<Callback>& w) { return w.expired(); }),
            slots.end());
        slots.emplace_back(holder);
    }
    // Aliased shared_ptr<void> keeps the Callback alive; destruction of
    // the handle drops the strong ref → the weak_ptr in the slot list
    // expires → the next dispatch skips it.
    return SubscriberHandle{holder, holder.get()};
}

void AgentBridge::dispatch(SlotList& slots, const juce::var& payload) {
    // Audio thread tripwire: dispatch allocates (callAsync, std::vector,
    // juce::var copies, mutex acquire).  In debug we crash loudly; in
    // release we must still bail BEFORE any of those allocations to avoid
    // RT glitches / priority inversion.  Bumping an atomic counter is
    // wait-free and lets Telemetry surface the drop later.
    const auto audioId = audioThreadId_.load(std::memory_order_relaxed);
    if (audioId != nullptr && juce::Thread::getCurrentThreadId() == audioId) {
        // Release-safe: bail without allocating. Record a drop so it shows in telemetry.
        jassertfalse; // debug crash, release no-op
        droppedFromAudioThread_.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    // Snapshot live callbacks under the lock, then release before invoking
    // so subscribers may register/unregister inside their own callback.
    std::vector<CallbackPtr> live;
    {
        std::lock_guard<std::mutex> lock(subscribersMutex_);
        live.reserve(slots.size());
        for (auto& weak : slots) {
            if (auto strong = weak.lock())
                live.emplace_back(std::move(strong));
        }
    }

    if (live.empty())
        return;

    // Marshal every invocation to the message thread; emission sites may
    // be on the streaming/network thread.  Audio thread is explicitly NOT
    // a supported caller — never emit from the realtime callback.
    auto* mm = juce::MessageManager::getInstanceWithoutCreating();
    if (mm == nullptr) {
        // Headless context (some test harness paths): invoke synchronously.
        for (auto& cb : live) {
            try {
                (*cb)(payload);
            } catch (const std::exception& e) {
                DBG("AgentBridge subscriber threw: " << e.what());
            } catch (...) {
                DBG("AgentBridge subscriber threw non-std exception");
            }
        }
        return;
    }

    for (auto& cb : live) {
        auto cbCopy = cb;
        auto payloadCopy = payload;
        juce::MessageManager::callAsync([cbCopy = std::move(cbCopy), payloadCopy = std::move(payloadCopy)]() {
            try {
                (*cbCopy)(payloadCopy);
            } catch (const std::exception& e) {
                DBG("AgentBridge subscriber threw: " << e.what());
            } catch (...) {
                DBG("AgentBridge subscriber threw non-std exception");
            }
        });
    }
}

AgentBridge::SubscriberHandle AgentBridge::onToken(Callback cb) { return subscribe(tokenSlots_, std::move(cb)); }
AgentBridge::SubscriberHandle AgentBridge::onPatch(Callback cb) { return subscribe(patchSlots_, std::move(cb)); }
AgentBridge::SubscriberHandle AgentBridge::onDone(Callback cb) { return subscribe(doneSlots_, std::move(cb)); }
AgentBridge::SubscriberHandle AgentBridge::onError(Callback cb) { return subscribe(errorSlots_, std::move(cb)); }
AgentBridge::SubscriberHandle AgentBridge::onRationale(Callback cb) {
    return subscribe(rationaleSlots_, std::move(cb));
}
AgentBridge::SubscriberHandle AgentBridge::onSuggestVariations(Callback cb) {
    return subscribe(suggestVariationsSlots_, std::move(cb));
}
AgentBridge::SubscriberHandle AgentBridge::onPatchUpdate(Callback cb) {
    return subscribe(patchUpdateSlots_, std::move(cb));
}
AgentBridge::SubscriberHandle AgentBridge::onTranscript(Callback cb) {
    return subscribe(transcriptSlots_, std::move(cb));
}

void AgentBridge::notifyToken(const juce::var& payload) { dispatch(tokenSlots_, payload); }
void AgentBridge::notifyPatch(const juce::var& payload) { dispatch(patchSlots_, payload); }
void AgentBridge::notifyDone(const juce::var& payload) { dispatch(doneSlots_, payload); }
void AgentBridge::notifyError(const juce::var& payload) { dispatch(errorSlots_, payload); }
void AgentBridge::notifyRationale(const juce::var& payload) { dispatch(rationaleSlots_, payload); }
void AgentBridge::notifySuggestVariations(const juce::var& payload) { dispatch(suggestVariationsSlots_, payload); }
void AgentBridge::notifyPatchUpdate(const juce::var& payload) { dispatch(patchUpdateSlots_, payload); }
void AgentBridge::notifyTranscript(const juce::var& payload) { dispatch(transcriptSlots_, payload); }

std::string AgentBridge::status() const { return "agent-bridge-v2"; }

// Phase 12A: submitPrompt / refinePatch / generateLlmPatch / feedChunk /
// buildSystemPrompt / getParameterBias / generateRationale forward to
// prompt_ (PromptHandler).

PatchStruct AgentBridge::submitPrompt(const std::string& prompt) { return prompt_.submitPrompt(prompt); }

void AgentBridge::refinePatch(const PatchStruct& llmPatch) { prompt_.refinePatch(llmPatch); }

std::optional<PatchStruct> AgentBridge::pollPatch() noexcept { return pipeline_.poll(); }

std::array<PatchStruct, engine::VariationEngine::kVariationCount>
AgentBridge::generateVariations(const PatchStruct& base) const {
    return variationEngine_.generateVariations(base);
}

std::array<PatchStruct, engine::VariationEngine::kVariationCount>
AgentBridge::generateVariationsWithSeed(const PatchStruct& base, uint32_t perturbSeed) const {
    return variationEngine_.generateVariationsWithSeed(base, perturbSeed);
}

void AgentBridge::recordFeedback(FeedbackKind kind, const std::string& prompt, const PatchStruct& patch) {
    memory_.recordFeedback(kind, prompt, patch);
}

std::string AgentBridge::buildSystemPrompt(const std::string& userPrompt) const {
    return prompt_.buildSystemPrompt(userPrompt);
}

PatchVector AgentBridge::getParameterBias(const std::string& userPrompt) const {
    return prompt_.getParameterBias(userPrompt);
}

std::optional<PatchStruct> AgentBridge::generateLlmPatch(const std::string& prompt, uint32_t patch_id) {
    return prompt_.generateLlmPatch(prompt, patch_id);
}

void AgentBridge::feedChunk(std::string_view chunk) { prompt_.feedChunk(chunk); }

void AgentBridge::onMidiCC(int controller, int value) noexcept { knob_.onMidiCC(controller, value); }

// ── Issue #72: Bidirectional knob bridge (Phase 12A: forwards to KnobBridge) ─

void AgentBridge::handleKnobTweak(const std::string& param, float value) { knob_.handleKnobTweak(param, value); }

// ── Issue #90: Semantic dictionary (Phase 10C: forwards to DictionaryService) ─

std::string AgentBridge::getDictionaryJson() const { return dictionary_.getDictionaryJson(); }

void AgentBridge::saveDictionary(const std::string& json) { dictionary_.saveDictionary(json); }

void AgentBridge::loadDictionary(const std::string& path) { dictionary_.loadDictionary(path); }

// ── Issue #91: Telemetry (Phase 10C: forwards to TelemetryService) ───────────

std::string AgentBridge::getTelemetryJson() const { return telemetry_.getTelemetryJson(); }

void AgentBridge::setTelemetryEnabled(bool on) { telemetry_.setEnabled(on); }

// ── Issue #85: Session-aware narrative generation (Phase 12A: forwards) ──────

std::string AgentBridge::generateRationale(const std::string& prompt, const PatchStruct& patch) const {
    return prompt_.generateRationale(prompt, patch);
}

// ── Phase 6C: in-browser audition keyboard ──────────────────────────────────

void AgentBridge::setMidiNoteSink(MidiNoteSink sink) {
    std::lock_guard<std::mutex> lock(midiSinkMutex_);
    midiNoteSink_ = std::move(sink);
}

void AgentBridge::postMidiNote(int note, float velocity, int durationMs) {
    // Clamp inputs: bad UI input should never crash the engine.
    if (note < 0)
        note = 0;
    if (note > 127)
        note = 127;
    if (velocity < 0.0f)
        velocity = 0.0f;
    if (velocity > 1.0f)
        velocity = 1.0f;
    if (durationMs < 10)
        durationMs = 10;
    if (durationMs > 10000)
        durationMs = 10000;

    // Snapshot the sink under the lock so a concurrent setMidiNoteSink
    // cannot race the dispatch below.
    MidiNoteSink sinkCopy;
    {
        std::lock_guard<std::mutex> lock(midiSinkMutex_);
        sinkCopy = midiNoteSink_;
    }
    if (!sinkCopy) {
        DBG("AgentBridge::postMidiNote: no sink registered, note dropped (note=" << note << ")");
        return;
    }

    auto* mm = juce::MessageManager::getInstanceWithoutCreating();
    if (mm == nullptr) {
        // Headless: invoke synchronously and assume the caller knows what
        // they are doing (tests can stub the sink).
        sinkCopy(note, velocity, /*isNoteOn=*/true);
        sinkCopy(note, velocity, /*isNoteOn=*/false);
        return;
    }

    // Drive note-on now on the message thread; AudioProcessor's
    // processBlock will pick up the queued event from the sink on the
    // next audio block.
    juce::MessageManager::callAsync([sinkCopy, note, velocity]() { sinkCopy(note, velocity, /*isNoteOn=*/true); });

    // Schedule the matched note-off. Timer::callAfterDelay runs on the
    // message thread, mirroring the note-on dispatch above so ordering
    // is preserved end-to-end. Capturing by value keeps the sink alive
    // even if setMidiNoteSink swaps it before the timer fires.
    juce::Timer::callAfterDelay(durationMs,
                                [sinkCopy, note, velocity]() { sinkCopy(note, velocity, /*isNoteOn=*/false); });
}

} // namespace agentic_synth::agent
