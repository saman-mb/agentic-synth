#include "agent/AgentBridge.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <initializer_list>
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <string_view>
#include <utility>

#include "agent/PresetStore.h"
#include "engine/OfflineRenderer.h"
#include "mapper/EnvLoader.h"

namespace agentic_synth::agent {

namespace {

int enumIndex(float v, int lo, int hi) noexcept {
    if (!std::isfinite(v))
        return lo;
    return std::clamp(static_cast<int>(std::lround(v)), lo, hi);
}

double propNumber(const juce::DynamicObject* obj, const char* name, double fallback) {
    if (obj == nullptr || !obj->hasProperty(name))
        return fallback;
    return static_cast<double>(obj->getProperty(name));
}

juce::DynamicObject* propObject(const juce::DynamicObject* obj, const char* name) {
    if (obj == nullptr || !obj->hasProperty(name))
        return nullptr;
    return obj->getProperty(name).getDynamicObject();
}

juce::var routeVar(const char* target, double amount) {
    auto* route = new juce::DynamicObject{};
    route->setProperty("target", juce::String(target));
    route->setProperty("amount", amount);
    return juce::var{route};
}

juce::var macroVar(const char* name, std::initializer_list<juce::var> routesIn) {
    auto* macro = new juce::DynamicObject{};
    macro->setProperty("name", juce::String(name));
    juce::Array<juce::var> routes;
    for (const auto& route : routesIn)
        routes.add(route);
    macro->setProperty("routes", juce::var{routes});
    return juce::var{macro};
}

} // namespace

juce::var AgentBridge::patchToVar(const PatchStruct& p) {
    auto* obj = new juce::DynamicObject{};

    juce::Array<juce::var> osc;
    for (const auto& o : p.osc) {
        auto* oo = new juce::DynamicObject{};
        oo->setProperty("type", static_cast<int>(o.type));
        oo->setProperty("semitone_offset", o.semitone_offset);
        oo->setProperty("detune_cents", o.detune_cents);
        oo->setProperty("wavetable_pos", o.wavetable_pos);
        oo->setProperty("fm_ratio", o.fm_ratio);
        oo->setProperty("fm_depth", o.fm_depth);
        oo->setProperty("volume", o.volume);
        oo->setProperty("pan", o.pan);
        oo->setProperty("pulse_width", o.pulse_width);
        oo->setProperty("enabled", static_cast<int>(o.enabled != 0));
        osc.add(juce::var{oo});
    }
    obj->setProperty("osc", juce::var{osc});

    auto* filter = new juce::DynamicObject{};
    filter->setProperty("type", static_cast<int>(p.filter.type));
    filter->setProperty("cutoff_hz", p.filter.cutoff_hz);
    filter->setProperty("resonance", p.filter.resonance);
    filter->setProperty("env_mod", p.filter.env_mod);
    filter->setProperty("key_track", p.filter.key_track);
    filter->setProperty("drive", p.filter.drive);
    obj->setProperty("filter", juce::var{filter});

    auto envToVar = [](const EnvParams& e) {
        auto* env = new juce::DynamicObject{};
        env->setProperty("attack_s", e.attack_s);
        env->setProperty("decay_s", e.decay_s);
        env->setProperty("sustain", e.sustain);
        env->setProperty("release_s", e.release_s);
        return juce::var{env};
    };
    obj->setProperty("filter_env", envToVar(p.filter_env));
    obj->setProperty("amp_env", envToVar(p.amp_env));

    juce::Array<juce::var> lfos;
    for (const auto& l : p.lfo) {
        auto* lo = new juce::DynamicObject{};
        lo->setProperty("waveform", static_cast<int>(l.waveform));
        lo->setProperty("target", static_cast<int>(l.target));
        lo->setProperty("rate_hz", l.rate_hz);
        lo->setProperty("depth", l.depth);
        lo->setProperty("phase_offset", l.phase_offset);
        lo->setProperty("bpm_sync", static_cast<int>(l.bpm_sync != 0));
        lfos.add(juce::var{lo});
    }
    obj->setProperty("lfo", juce::var{lfos});

    auto* reverb = new juce::DynamicObject{};
    reverb->setProperty("size", p.reverb.size);
    reverb->setProperty("damping", p.reverb.damping);
    reverb->setProperty("width", p.reverb.width);
    reverb->setProperty("mix", p.reverb.mix);
    obj->setProperty("reverb", juce::var{reverb});

    auto* delay = new juce::DynamicObject{};
    delay->setProperty("time_s", p.delay.time_s);
    delay->setProperty("feedback", p.delay.feedback);
    delay->setProperty("mix", p.delay.mix);
    delay->setProperty("stereo", p.delay.stereo);
    delay->setProperty("bpm_sync", static_cast<int>(p.delay.bpm_sync != 0));
    obj->setProperty("delay", juce::var{delay});

    // Phase E (#265): chorus + tubesat + reverb-send HPF.
    auto* chorus = new juce::DynamicObject{};
    chorus->setProperty("rate_hz", p.chorus.rate_hz);
    chorus->setProperty("depth", p.chorus.depth);
    chorus->setProperty("mix", p.chorus.mix);
    obj->setProperty("chorus", juce::var{chorus});

    auto* tubesat = new juce::DynamicObject{};
    tubesat->setProperty("drive", p.tubesat.drive);
    tubesat->setProperty("mix", p.tubesat.mix);
    obj->setProperty("tubesat", juce::var{tubesat});

    obj->setProperty("reverb_send_hpf_hz", p.reverb_send_hpf_hz);

    obj->setProperty("master_gain", p.master_gain);
    obj->setProperty("portamento_s", p.portamento_s);
    obj->setProperty("voice_count", static_cast<int>(p.voice_count));
    return juce::var{obj};
}

PatchStruct AgentBridge::patchFromVar(const juce::var& payload) {
    PatchStruct p = make_default_patch();
    auto* obj = payload.getDynamicObject();
    if (obj == nullptr)
        return p;

    const juce::var oscVar = obj->getProperty("osc");
    if (auto* oscs = oscVar.getArray()) {
        for (int i = 0; i < std::min(oscs->size(), kMaxOscillators); ++i) {
            auto* o = oscs->getReference(i).getDynamicObject();
            if (o == nullptr)
                continue;
            p.osc[i].type = static_cast<OscType>(enumIndex(static_cast<float>(propNumber(o, "type", static_cast<int>(p.osc[i].type))), 0, 7));
            p.osc[i].semitone_offset = static_cast<float>(propNumber(o, "semitone_offset", p.osc[i].semitone_offset));
            p.osc[i].detune_cents = static_cast<float>(propNumber(o, "detune_cents", p.osc[i].detune_cents));
            p.osc[i].wavetable_pos = static_cast<float>(propNumber(o, "wavetable_pos", p.osc[i].wavetable_pos));
            p.osc[i].fm_ratio = static_cast<float>(propNumber(o, "fm_ratio", p.osc[i].fm_ratio));
            p.osc[i].fm_depth = static_cast<float>(propNumber(o, "fm_depth", p.osc[i].fm_depth));
            p.osc[i].volume = static_cast<float>(propNumber(o, "volume", p.osc[i].volume));
            p.osc[i].pan = static_cast<float>(propNumber(o, "pan", p.osc[i].pan));
            p.osc[i].pulse_width = static_cast<float>(propNumber(o, "pulse_width", p.osc[i].pulse_width));
            p.osc[i].enabled = propNumber(o, "enabled", p.osc[i].enabled) >= 0.5 ? 1u : 0u;
        }
    }

    if (auto* f = propObject(obj, "filter")) {
        p.filter.type = static_cast<FilterType>(enumIndex(static_cast<float>(propNumber(f, "type", static_cast<int>(p.filter.type))), 0, 4));
        p.filter.cutoff_hz = static_cast<float>(propNumber(f, "cutoff_hz", p.filter.cutoff_hz));
        p.filter.resonance = static_cast<float>(propNumber(f, "resonance", p.filter.resonance));
        p.filter.env_mod = static_cast<float>(propNumber(f, "env_mod", p.filter.env_mod));
        p.filter.key_track = static_cast<float>(propNumber(f, "key_track", p.filter.key_track));
        p.filter.drive = static_cast<float>(propNumber(f, "drive", p.filter.drive));
    }

    auto readEnv = [](juce::DynamicObject* env, EnvParams& out) {
        if (env == nullptr)
            return;
        out.attack_s = static_cast<float>(propNumber(env, "attack_s", out.attack_s));
        out.decay_s = static_cast<float>(propNumber(env, "decay_s", out.decay_s));
        out.sustain = static_cast<float>(propNumber(env, "sustain", out.sustain));
        out.release_s = static_cast<float>(propNumber(env, "release_s", out.release_s));
    };
    readEnv(propObject(obj, "filter_env"), p.filter_env);
    readEnv(propObject(obj, "amp_env"), p.amp_env);

    const juce::var lfoVar = obj->getProperty("lfo");
    if (auto* lfos = lfoVar.getArray()) {
        for (int i = 0; i < std::min(lfos->size(), kMaxLfos); ++i) {
            auto* l = lfos->getReference(i).getDynamicObject();
            if (l == nullptr)
                continue;
            p.lfo[i].waveform = static_cast<LfoWaveform>(enumIndex(static_cast<float>(propNumber(l, "waveform", static_cast<int>(p.lfo[i].waveform))), 0, 4));
            p.lfo[i].target = static_cast<LfoTarget>(enumIndex(static_cast<float>(propNumber(l, "target", static_cast<int>(p.lfo[i].target))), 0, 6));
            p.lfo[i].rate_hz = static_cast<float>(propNumber(l, "rate_hz", p.lfo[i].rate_hz));
            p.lfo[i].depth = static_cast<float>(propNumber(l, "depth", p.lfo[i].depth));
            p.lfo[i].phase_offset = static_cast<float>(propNumber(l, "phase_offset", p.lfo[i].phase_offset));
            p.lfo[i].bpm_sync = propNumber(l, "bpm_sync", p.lfo[i].bpm_sync) >= 0.5 ? 1u : 0u;
        }
    }

    if (auto* rv = propObject(obj, "reverb")) {
        p.reverb.size = static_cast<float>(propNumber(rv, "size", p.reverb.size));
        p.reverb.damping = static_cast<float>(propNumber(rv, "damping", p.reverb.damping));
        p.reverb.width = static_cast<float>(propNumber(rv, "width", p.reverb.width));
        p.reverb.mix = static_cast<float>(propNumber(rv, "mix", p.reverb.mix));
    }

    if (auto* d = propObject(obj, "delay")) {
        p.delay.time_s = static_cast<float>(propNumber(d, "time_s", p.delay.time_s));
        p.delay.feedback = static_cast<float>(propNumber(d, "feedback", p.delay.feedback));
        p.delay.mix = static_cast<float>(propNumber(d, "mix", p.delay.mix));
        p.delay.stereo = static_cast<float>(propNumber(d, "stereo", p.delay.stereo));
        p.delay.bpm_sync = propNumber(d, "bpm_sync", p.delay.bpm_sync) >= 0.5 ? 1u : 0u;
    }

    // Phase E (#265): chorus + tubesat + reverb-send HPF. Missing fields
    // default to make_default_patch() values (chorus.mix=0, tubesat.drive=0,
    // reverb_send_hpf_hz=0) — pre-Phase-E patches round-trip silently.
    if (auto* c = propObject(obj, "chorus")) {
        p.chorus.rate_hz = static_cast<float>(propNumber(c, "rate_hz", p.chorus.rate_hz));
        p.chorus.depth = static_cast<float>(propNumber(c, "depth", p.chorus.depth));
        p.chorus.mix = static_cast<float>(propNumber(c, "mix", p.chorus.mix));
    }
    if (auto* t = propObject(obj, "tubesat")) {
        p.tubesat.drive = static_cast<float>(propNumber(t, "drive", p.tubesat.drive));
        p.tubesat.mix = static_cast<float>(propNumber(t, "mix", p.tubesat.mix));
    }
    p.reverb_send_hpf_hz = static_cast<float>(propNumber(obj, "reverb_send_hpf_hz", p.reverb_send_hpf_hz));

    p.master_gain = static_cast<float>(propNumber(obj, "master_gain", p.master_gain));
    p.portamento_s = static_cast<float>(propNumber(obj, "portamento_s", p.portamento_s));
    p.voice_count = static_cast<uint8_t>(enumIndex(static_cast<float>(propNumber(obj, "voice_count", p.voice_count)), 1, 16));
    return p;
}

juce::var AgentBridge::modulationPlanForPatch(const PatchStruct& p) {
    juce::Array<juce::var> macros;
    const bool dark = p.filter.cutoff_hz < 1000.0f;
    const bool hasMotion = p.lfo[0].depth > 0.05f || p.lfo[1].depth > 0.05f;
    const bool hasFm = p.osc[0].type == OscType::FM || p.osc[1].type == OscType::FM || p.osc[2].type == OscType::FM;
    const bool hasWavetable = p.osc[0].type == OscType::Wavetable || p.osc[1].type == OscType::Wavetable ||
                              p.osc[2].type == OscType::Wavetable;

    macros.add(macroVar(dark ? "GRIP" : "BRIGHTNESS",
                        {routeVar("filter.cutoff_hz", dark ? 0.65 : 0.8), routeVar("filter.resonance", 0.2)}));
    macros.add(macroVar(hasMotion ? "WOBBLE" : "MOTION",
                        {routeVar("lfo.0.depth", 0.65), routeVar("lfo.0.rate_hz", 0.25)}));
    if (hasFm) {
        macros.add(macroVar("EDGE", {routeVar("osc.0.fm_depth", 0.55), routeVar("filter.drive", 0.35)}));
    } else if (hasWavetable) {
        macros.add(macroVar("MORPH", {routeVar("osc.0.wavetable_pos", 0.65), routeVar("lfo.0.depth", 0.25)}));
    } else {
        macros.add(macroVar("WIDTH", {routeVar("osc.1.detune_cents", 0.25), routeVar("osc.2.detune_cents", -0.25)}));
    }
    macros.add(macroVar("SPACE", {routeVar("reverb.mix", 0.45), routeVar("delay.mix", 0.3), routeVar("delay.stereo", 0.35)}));

    auto* mod = new juce::DynamicObject{};
    mod->setProperty("macros", juce::var{macros});
    return juce::var{mod};
}

AgentBridge::AgentBridge() {
    // Load the bundled TIMBRE sound-designer briefing (system-prompt.md)
    // and inject it into both samplers BEFORE wiring the Gemini key — the
    // GeminiSampler copies the prompt at setSystemPrompt() time, so we want
    // the rich briefing in place before that copy happens. Without this,
    // both samplers fall back to a stub generic prompt and the LLM emits
    // safe, characterless patches.
    auto systemPrompt = mapper::GrammarSampler::loadSystemPromptFile();
    if (!systemPrompt.empty()) {
        sampler_.setSystemPrompt(systemPrompt);
        std::cerr << "[AgentBridge] system-prompt.md loaded (" << systemPrompt.size() << " bytes)\n";
    } else {
        std::cerr << "[AgentBridge] system-prompt.md not found; samplers will use stub fallback\n";
    }

    // Look up GEMINI_KEY (env var, falling back to a `.env` walked up from
    // cwd). When found, enable the GeminiSampler so PromptHandler can use
    // it as a cloud fallback whenever the local llama.cpp /completion
    // server is unreachable. When the key is absent the sampler stays
    // disabled and the existing nullopt-on-failure behaviour is preserved.
    const auto geminiKey = mapper::loadEnvKey("GEMINI_KEY");
    if (!geminiKey.empty()) {
        gemini_.setApiKey(geminiKey);
        gemini_.setSystemPrompt(sampler_.systemPrompt());
        std::cerr << "[AgentBridge] GEMINI_KEY loaded; Gemini fallback enabled\n";
    } else {
        std::cerr << "[AgentBridge] GEMINI_KEY not set; Gemini fallback disabled\n";
    }

    // Phase 34b (#264) — wire the delta-nudger to the same key. When the
    // key is empty deltaNudger_.enabled() stays false and PromptHandler
    // skips straight to the Phase 34a top-1 archetype path.
    if (!geminiKey.empty()) {
        deltaNudger_.setApiKey(geminiKey);
        std::cerr << "[AgentBridge] DeltaNudger enabled (LLM nudges over RAG retrieval)\n";
    } else {
        std::cerr << "[AgentBridge] DeltaNudger disabled — no GEMINI_KEY\n";
    }
    prompt_.setDeltaNudger(&deltaNudger_);

    // ── Two-step LLM flow: ENHANCER (translator) step ────────────────────────
    //
    // Load the bundled TIMBRE translator briefing (enhancer-prompt.md, ~280
    // lines, 9 fixed sections, 6 worked examples) and inject it + the same
    // GEMINI_KEY into the PromptEnhancer. The enhancer rewrites a terse
    // producer prompt ("dark dubstep wobbly bass") into a 200–400-word
    // sensory brief that the patch generator (Gemini / Grammar samplers)
    // receives instead of the raw user prompt. Without a key the enhancer
    // stays disabled and the worker job falls back to the raw prompt — same
    // pattern as the gemini_ fallback.
    auto enhancerPrompt = mapper::PromptEnhancer::loadEnhancerPromptFile();
    if (!enhancerPrompt.empty()) {
        enhancer_.setSystemPrompt(std::move(enhancerPrompt));
    } else {
        std::cerr << "[AgentBridge] enhancer-prompt.md not found; enhancer will use stub fallback\n";
    }
    if (!geminiKey.empty()) {
        enhancer_.setApiKey(geminiKey);
        std::cerr << "[AgentBridge] PromptEnhancer enabled\n";
    } else {
        std::cerr << "[AgentBridge] PromptEnhancer disabled\n";
    }

    // Phase 29 — same key drives the STT path.
    if (!geminiKey.empty()) {
        stt_.setApiKey(geminiKey);
        std::cerr << "[AgentBridge] GeminiSTT enabled (push-to-talk transcription)\n";
    } else {
        std::cerr << "[AgentBridge] GeminiSTT disabled — no GEMINI_KEY\n";
    }

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

    // Phase C (#269) — wire the PromptHandler's failure sink to our
    // typed `failure` notifier so degraded-generation events flow out
    // through the same subscriber fan-out the rest of the agent uses.
    // The lambda captures `this`; AgentBridge outlives PromptHandler so
    // the back-reference is safe for the bridge's lifetime.
    prompt_.setFailureSink([this](const std::string& kind, const std::string& detail) {
        auto* obj = new juce::DynamicObject{};
        obj->setProperty("kind", juce::String(kind));
        if (!detail.empty())
            obj->setProperty("detail", juce::String(detail));
        notifyFailure(juce::var{obj});
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
AgentBridge::SubscriberHandle AgentBridge::onEnhancement(Callback cb) {
    return subscribe(enhancementSlots_, std::move(cb));
}
AgentBridge::SubscriberHandle AgentBridge::onVariationsReady(Callback cb) {
    return subscribe(variationsReadySlots_, std::move(cb));
}
AgentBridge::SubscriberHandle AgentBridge::onFailure(Callback cb) {
    return subscribe(failureSlots_, std::move(cb));
}
AgentBridge::SubscriberHandle AgentBridge::onPresetCommitted(Callback cb) {
    return subscribe(presetCommittedSlots_, std::move(cb));
}
AgentBridge::SubscriberHandle AgentBridge::onBounceComplete(Callback cb) {
    return subscribe(bounceCompleteSlots_, std::move(cb));
}

void AgentBridge::notifyToken(const juce::var& payload) { dispatch(tokenSlots_, payload); }
void AgentBridge::notifyPatch(const juce::var& payload) { dispatch(patchSlots_, payload); }
void AgentBridge::notifyDone(const juce::var& payload) { dispatch(doneSlots_, payload); }
void AgentBridge::notifyError(const juce::var& payload) { dispatch(errorSlots_, payload); }
void AgentBridge::notifyRationale(const juce::var& payload) { dispatch(rationaleSlots_, payload); }
void AgentBridge::notifySuggestVariations(const juce::var& payload) { dispatch(suggestVariationsSlots_, payload); }
void AgentBridge::notifyPatchUpdate(const juce::var& payload) { dispatch(patchUpdateSlots_, payload); }
void AgentBridge::notifyTranscript(const juce::var& payload) { dispatch(transcriptSlots_, payload); }
void AgentBridge::notifyEnhancement(const juce::var& payload) { dispatch(enhancementSlots_, payload); }
void AgentBridge::notifyVariationsReady(const juce::var& payload) { dispatch(variationsReadySlots_, payload); }
void AgentBridge::notifyFailure(const juce::var& payload) { dispatch(failureSlots_, payload); }
void AgentBridge::notifyPresetCommitted(const juce::var& payload) { dispatch(presetCommittedSlots_, payload); }
void AgentBridge::notifyBounceComplete(const juce::var& payload) { dispatch(bounceCompleteSlots_, payload); }

// ── Phase D / #260 — preset commit + persistence ─────────────────────────────

void AgentBridge::commitPreset(const std::string& name, const std::string& prompt, const PatchStruct& patch) {
    if (name.empty())
        return;
    PresetStore::instance().save(name, prompt, patch);
    const auto stored = PresetStore::instance().getByName(name);
    auto* obj = new juce::DynamicObject{};
    obj->setProperty("name", juce::String(name));
    obj->setProperty("prompt", juce::String(prompt));
    if (stored) {
        obj->setProperty("created_ms", static_cast<double>(stored->created_ms));
    }
    obj->setProperty("patch", patchToVar(patch));
    notifyPresetCommitted(juce::var{obj});
}

std::string AgentBridge::getPresetsJson() const {
    const auto all = PresetStore::instance().all();
    juce::Array<juce::var> arr;
    arr.ensureStorageAllocated(static_cast<int>(all.size()));
    for (const auto& sp : all) {
        auto* obj = new juce::DynamicObject{};
        obj->setProperty("name", juce::String(sp.name));
        obj->setProperty("prompt", juce::String(sp.prompt));
        obj->setProperty("created_ms", static_cast<double>(sp.created_ms));
        obj->setProperty("patch", patchToVar(sp.patch));
        arr.add(juce::var{obj});
    }
    auto* root = new juce::DynamicObject{};
    root->setProperty("presets", juce::var{arr});
    return juce::JSON::toString(juce::var{root}).toStdString();
}

void AgentBridge::deletePreset(const std::string& name) {
    PresetStore::instance().deleteByName(name);
}

// ── Phase D / #268 (partial) — audio bounce ──────────────────────────────────

void AgentBridge::bouncePatchToFile(const PatchStruct& patch, const juce::File& dest) {
    engine::BounceConfig cfg;  // defaults: 4s @ 48k/24-bit stereo, C3, hold 3s
    const bool ok = engine::renderPatchToWav(patch, dest, cfg);
    auto* obj = new juce::DynamicObject{};
    obj->setProperty("ok", ok);
    if (ok) {
        obj->setProperty("path", dest.getFullPathName());
    } else {
        obj->setProperty("error", juce::String("offline render or write failed"));
    }
    notifyBounceComplete(juce::var{obj});
}

std::string AgentBridge::enhancePrompt(const std::string& userPrompt) { return enhancer_.enhance(userPrompt); }

std::string AgentBridge::transcribeAudio(const std::int16_t* samples, int numSamples, int sampleRate) const {
    return stt_.transcribe(samples, numSamples, sampleRate);
}

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

std::optional<PatchStruct> AgentBridge::generateLlmPatch(const std::string& prompt, uint32_t patch_id,
                                                         std::optional<PatchStruct> previousPatch,
                                                         std::optional<std::string> previousPrompt) {
    return prompt_.generateLlmPatch(prompt, patch_id, std::move(previousPatch), std::move(previousPrompt));
}

void AgentBridge::applyGuardrailIfNotRefinement(PatchStruct& patch, const std::string& prompt,
                                                bool hasPreviousPatch) noexcept {
    prompt_.applyGuardrailIfNotRefinement(patch, prompt, hasPreviousPatch);
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

void AgentBridge::postMidiNoteOn(int note, float velocity) {
    if (note < 0) note = 0;
    if (note > 127) note = 127;
    if (velocity < 0.0f) velocity = 0.0f;
    if (velocity > 1.0f) velocity = 1.0f;
    MidiNoteSink sinkCopy;
    {
        std::lock_guard<std::mutex> lock(midiSinkMutex_);
        sinkCopy = midiNoteSink_;
    }
    if (!sinkCopy) return;
    auto* mm = juce::MessageManager::getInstanceWithoutCreating();
    if (mm == nullptr) { sinkCopy(note, velocity, true); return; }
    juce::MessageManager::callAsync([sinkCopy, note, velocity]() { sinkCopy(note, velocity, true); });
}

void AgentBridge::postMidiNoteOff(int note) {
    if (note < 0) note = 0;
    if (note > 127) note = 127;
    MidiNoteSink sinkCopy;
    {
        std::lock_guard<std::mutex> lock(midiSinkMutex_);
        sinkCopy = midiNoteSink_;
    }
    if (!sinkCopy) return;
    auto* mm = juce::MessageManager::getInstanceWithoutCreating();
    if (mm == nullptr) { sinkCopy(note, 0.0f, false); return; }
    juce::MessageManager::callAsync([sinkCopy, note]() { sinkCopy(note, 0.0f, false); });
}

} // namespace agentic_synth::agent
