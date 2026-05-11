#include "agent/AgentBridge.h"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <sstream>
#include <utility>

#include "agent/ParamMap.h"

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

namespace {

// Phase 9C: paramToDelta (UI param path → PatchDelta) now lives in
// ParamMap.cpp as a single source-of-truth table. AgentBridge calls
// agent::paramToDelta directly — see handleKnobTweak below.

} // namespace

std::string AgentBridge::status() const { return "agent-bridge-v2"; }

PatchStruct AgentBridge::submitPrompt(const std::string& prompt) {
    // Reset stream parser so field-complete callbacks from a prior LLM call
    // don't bleed into this submission's heuristic patch.
    streamParser_.reset();
    // Issue #65/#68: heuristic dispatched < 200 ms; semantic layer refines in place.
    PatchStruct patch = pipeline_.submit(prompt);
    if (semanticMapper_.apply(prompt, patch) > 0)
        pipeline_.injectPatch(patch);
    return patch;
}

void AgentBridge::refinePatch(const PatchStruct& llmPatch) { pipeline_.refinePatch(llmPatch); }

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
    const std::string& base =
        sampler_.systemPrompt().empty()
            ? std::string("You are a synthesizer patch designer. Generate synth parameters as structured JSON.\n")
            : sampler_.systemPrompt();

    std::string prompt = base;

    // Append MIDI CC context so the AI respects the user's current performance state.
    if (midiCutoffNorm_ < 0.25f)
        prompt += "MIDI context: filter is currently closed (dark sound).\n";
    else if (midiCutoffNorm_ > 0.75f)
        prompt += "MIDI context: filter is currently open (bright sound).\n";
    if (midiResonanceNorm_ > 0.5f)
        prompt += "MIDI context: high resonance is active.\n";

    std::string recap = memory_.buildRecap(userPrompt);
    if (recap.empty())
        return prompt;
    return prompt + "\n## Session Feedback\n" + recap + "\nUse the above feedback to guide parameter choices.\n";
}

PatchVector AgentBridge::getParameterBias(const std::string& userPrompt) const {
    return memory_.computeParameterBias(userPrompt);
}

std::optional<PatchStruct> AgentBridge::generateLlmPatch(const std::string& prompt, uint32_t patch_id) {
    auto result = sampler_.generate(prompt, patch_id);
    if (result)
        refinePatch(*result);
    return result;
}

void AgentBridge::feedChunk(std::string_view chunk) { streamParser_.feedChunk(chunk); }

void AgentBridge::onMidiCC(int controller, int value) noexcept {
    // Track CC74 (brightness/filter cutoff) and CC71 (resonance) so the
    // system prompt can reflect the user's current timbral preference.
    switch (controller) {
    case 71:
        midiResonanceNorm_ = static_cast<float>(value) / 127.0f;
        break;
    case 74:
        midiCutoffNorm_ = static_cast<float>(value) / 127.0f;
        break;
    default:
        break;
    }
}

// ── Issue #72: Bidirectional knob bridge ─────────────────────────────────────

void AgentBridge::handleKnobTweak(const std::string& param, float value) {
    // Copy current patch, apply the single-parameter delta, inject immediately.
    PatchStruct patch = pipeline_.currentPatch();
    mapper::apply_delta(patch, paramToDelta(param, value));
    pipeline_.injectPatch(patch);
    // Record so the session memory can bias future generations towards user tweaks.
    memory_.recordFeedback(FeedbackKind::Tweak, param, patch);
}

// ── Issue #90: Semantic dictionary (Phase 10C: forwards to DictionaryService) ─

std::string AgentBridge::getDictionaryJson() const { return dictionary_.getDictionaryJson(); }

void AgentBridge::saveDictionary(const std::string& json) { dictionary_.saveDictionary(json); }

void AgentBridge::loadDictionary(const std::string& path) { dictionary_.loadDictionary(path); }

// ── Issue #91: Telemetry (Phase 10C: forwards to TelemetryService) ───────────

std::string AgentBridge::getTelemetryJson() const { return telemetry_.getTelemetryJson(); }

void AgentBridge::setTelemetryEnabled(bool on) { telemetry_.setEnabled(on); }

// ── Issue #85: Session-aware narrative generation ────────────────────────────

std::string AgentBridge::generateRationale(const std::string& prompt, const PatchStruct& patch) const {
    std::ostringstream oss;

    // Describe oscillator character.
    static const char* kOscNames[] = {"sine", "triangle", "sawtooth", "square", "pulse", "wavetable", "FM", "noise"};
    const int oscIdx = static_cast<int>(patch.osc[0].type);
    const char* oscName = (oscIdx >= 0 && oscIdx < 8) ? kOscNames[oscIdx] : "sawtooth";

    oss << "I chose a " << oscName << " oscillator";

    // Filter character.
    if (patch.filter.cutoff_hz < 500.0f)
        oss << " with a closed filter for a dark, sub-heavy character";
    else if (patch.filter.cutoff_hz < 4000.0f)
        oss << " with a mid-range filter for warmth and presence";
    else
        oss << " with an open filter for brightness and clarity";

    if (patch.filter.resonance > 0.6f)
        oss << ", pushing the resonance for an acidic edge";
    else if (patch.filter.resonance > 0.3f)
        oss << " with moderate resonance for character";

    // Amplitude envelope.
    if (patch.amp_env.attack_s > 0.5f)
        oss << ". The slow attack lets the sound bloom gradually";
    else if (patch.amp_env.attack_s < 0.01f)
        oss << ". The instant attack gives it punch and immediacy";

    if (patch.amp_env.release_s > 1.5f)
        oss << " with a long release tail";

    // Modulation / movement.
    if (patch.lfo[0].depth > 0.3f) {
        const char* lfoTarget = (patch.lfo[0].target == LfoTarget::Pitch)          ? "pitch modulation"
                                : (patch.lfo[0].target == LfoTarget::FilterCutoff) ? "filter movement"
                                : (patch.lfo[0].target == LfoTarget::Amplitude)    ? "tremolo"
                                                                                   : "modulation";
        oss << ", adding " << lfoTarget << " for animation";
    }

    // Space.
    if (patch.reverb.mix > 0.4f)
        oss << ". Heavy reverb places it in a wide, ambient space";
    else if (patch.reverb.mix > 0.15f)
        oss << ". Light reverb adds depth without washing it out";

    if (patch.delay.mix > 0.2f)
        oss << " with delay for rhythmic echo";

    // Session context influence.
    const std::string recap = memory_.buildRecap(prompt, 3);
    if (!recap.empty()) {
        oss << ". Your session feedback steered me toward this timbral direction"
            << " — I've adjusted based on what you've liked and passed on previously";
    }

    // MIDI context.
    if (midiCutoffNorm_ < 0.25f)
        oss << ". I respected your MIDI filter position (currently closed/dark)";
    else if (midiCutoffNorm_ > 0.75f)
        oss << ". I matched your MIDI filter position (currently open/bright)";

    oss << ".";
    return oss.str();
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
