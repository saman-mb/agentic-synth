#include "ui/WebUiComponent.h"

#include "UiBinaryData.h"
#include "agent/PromptHandler.h"
#include "agent/WhisperClient.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <utility>
#include <vector>

#ifndef AGENTIC_SYNTH_UI_DEV
#define AGENTIC_SYNTH_UI_DEV 0
#endif

namespace agentic_synth::ui {

namespace {

// JUCE's juce_add_binary_data mangles each source filename via
// build_tools::makeBinaryDataIdentifierName: replace ' ' and '.' with '_',
// then drop every character that isn't [A-Za-z0-9_]. So hyphens are
// stripped (not converted), e.g. "index-CqAoZC13.js" → "indexCqAoZC13_js".
// getNamedResource expects that exact mangled identifier.
juce::String mangleResourceName(const juce::String& filename) {
    juce::String out;
    out.preallocateBytes(static_cast<size_t>(filename.length()));
    for (auto c : filename) {
        if (c == ' ' || c == '.') {
            out += '_';
        } else if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_') {
            out += c;
        }
        // else: drop (matches retainCharacters in JUCE).
    }
    return out;
}

juce::String mimeForPath(const juce::String& path) {
    if (path.endsWithIgnoreCase(".html"))
        return "text/html";
    if (path.endsWithIgnoreCase(".js"))
        return "text/javascript";
    if (path.endsWithIgnoreCase(".mjs"))
        return "text/javascript";
    if (path.endsWithIgnoreCase(".css"))
        return "text/css";
    if (path.endsWithIgnoreCase(".svg"))
        return "image/svg+xml";
    if (path.endsWithIgnoreCase(".woff2"))
        return "font/woff2";
    if (path.endsWithIgnoreCase(".woff"))
        return "font/woff";
    if (path.endsWithIgnoreCase(".json"))
        return "application/json";
    if (path.endsWithIgnoreCase(".png"))
        return "image/png";
    if (path.endsWithIgnoreCase(".jpg") || path.endsWithIgnoreCase(".jpeg"))
        return "image/jpeg";
    if (path.endsWithIgnoreCase(".ico"))
        return "image/x-icon";
    return "application/octet-stream";
}

// Convenience: extract a juce::var argument by index with a default fallback.
juce::var argOr(const juce::Array<juce::var>& args, int index, const juce::var& fallback) {
    return (index >= 0 && index < args.size()) ? args[index] : fallback;
}

// Root directory for all AgenticSynth persistent state. Sits under the
// platform-appropriate per-user app-data dir (NOT temp), so OS housekeeping
// (Windows %TEMP%, macOS /var/folders, systemd-tmpfiles) does not nuke
// WebView2 localStorage / cookies / IndexedDB between DAW sessions.
juce::File agenticSynthAppDataDir() {
    auto dir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("AgenticSynth");
    if (!dir.isDirectory()) {
        dir.createDirectory();
    }
    return dir;
}

// Resolve (or lazily create) the persistent install-stable identifier used
// as the root segment of every WebView2 user-data folder.
//
// Stored in <appData>/AgenticSynth/instance-id.json as { "instanceId": "<uuid>" }.
// On first run we generate a Uuid and persist it; on every subsequent run we
// read the same string back, so WebView2 state survives:
//   • plugin re-instantiation inside the same DAW session
//   • DAW project close/reopen
//   • DAW restart / machine reboot
// (Anything that wipes <appData> itself is treated as "fresh install".)
//
// We deliberately do NOT mix in processId / device-id / editor-index here:
// those change across runs and would defeat the persistence goal.
juce::String persistentInstanceId() {
    static const juce::String cached = []() -> juce::String {
        const auto file = agenticSynthAppDataDir().getChildFile("instance-id.json");

        if (file.existsAsFile()) {
            const auto parsed = juce::JSON::parse(file);
            if (auto* obj = parsed.getDynamicObject()) {
                const auto id = obj->getProperty("instanceId").toString();
                if (id.isNotEmpty()) {
                    return id;
                }
            }
            // Corrupt / unexpected shape: fall through and rewrite below.
        }

        const auto fresh = juce::Uuid().toString();
        auto* obj = new juce::DynamicObject{};
        obj->setProperty("instanceId", fresh);
        file.replaceWithText(juce::JSON::toString(juce::var{obj}));
        return fresh;
    }();
    return cached;
}

// Per-process slot counter for multi-instance disambiguation. WebView2
// permits multiple concurrent controllers in one process ONLY if each uses
// a distinct user-data folder. We hand out slot-0, slot-1, … to live
// WebUiComponent ctors. The counter resets each process start, so the first
// editor opened in a given DAW session always lands on slot-0 and therefore
// reuses the same on-disk localStorage/IndexedDB across reopens. Concurrent
// siblings (e.g. 4-instance DAW projects) get slot-1..slot-N, also
// persisted (folders are reused on subsequent runs in arrival order).
//
// Reuse-on-destruct is intentionally not implemented: editor lifetimes
// inside hosts are too noisy, and reusing a slot whose WebView2 is still
// shutting down can race the file lock. Folders are cheap; leave them.
int nextWebView2SlotIndex() {
    static std::atomic<int> counter{0};
    return counter.fetch_add(1, std::memory_order_relaxed);
}

// Compose the final user-data folder for THIS WebUiComponent instance.
// Layout: <appData>/AgenticSynth/WebView2/<stableId>/slot-<N>
juce::File webView2UserDataFolderForThisInstance() {
    auto folder = agenticSynthAppDataDir()
                      .getChildFile("WebView2")
                      .getChildFile(persistentInstanceId())
                      .getChildFile("slot-" + juce::String(nextWebView2SlotIndex()));
    folder.createDirectory();
    return folder;
}

} // namespace

std::optional<juce::WebBrowserComponent::Resource> WebUiComponent::serveResource(const juce::String& path) {
    // Normalise: strip any query/fragment, default "/" → "/index.html".
    juce::String trimmed = path.upToFirstOccurrenceOf("?", false, false).upToFirstOccurrenceOf("#", false, false);
    if (trimmed.isEmpty() || trimmed == "/") {
        trimmed = "/index.html";
    }

    if (trimmed.contains("..") || trimmed.contains("\\")) {
        return std::nullopt;
    }

    juce::String relative = trimmed.startsWith("/") ? trimmed.substring(1) : trimmed;
    juce::String basename = relative.fromLastOccurrenceOf("/", false, false);
    juce::String mangled = mangleResourceName(basename);

    int numBytes = 0;
    const char* data = UiBinaryData::getNamedResource(mangled.toRawUTF8(), numBytes);
    if (data == nullptr || numBytes <= 0) {
        return std::nullopt;
    }

    juce::WebBrowserComponent::Resource resource;
    resource.data.assign(reinterpret_cast<const std::byte*>(data), reinterpret_cast<const std::byte*>(data) + numBytes);
    resource.mimeType = mimeForPath(trimmed);
    return resource;
}

// ── TelemetryAwareBrowser ────────────────────────────────────────────────────
//
// Lifecycle hooks feed into Telemetry::recordUiEvent (P1 SRE fix: gives us a
// production-observable trace of WebView health). On load failure we ALSO
// swap in a diagnostic fallback panel via WebUiComponent::handleLoadFailure
// so the user never sees a silent blank window.

bool WebUiComponent::TelemetryAwareBrowser::pageAboutToLoad(const juce::String& url) {
    bridge_.telemetry().recordUiEvent("page_about_to_load", url.toStdString());
    DBG("[WebUI] page_about_to_load: " << url);
    return true;
}

void WebUiComponent::TelemetryAwareBrowser::pageFinishedLoading(const juce::String& url) {
    bridge_.telemetry().recordUiEvent("page_finished_loading", url.toStdString());
    DBG("[WebUI] page_finished_loading: " << url);
}

bool WebUiComponent::TelemetryAwareBrowser::pageLoadHadNetworkError(const juce::String& errorInfo) {
    bridge_.telemetry().recordUiEvent("page_load_error", errorInfo.toStdString());
    DBG("[WebUI] page_load_error: " << errorInfo);
    owner_.handleLoadFailure(errorInfo);
    // Allow the browser's default error page so users see something behind
    // the overlay (defence in depth if our overlay itself fails to paint).
    return true;
}

// ── FallbackComponent ────────────────────────────────────────────────────────

juce::String WebUiComponent::buildFallbackMessage(const juce::String& errorInfo) {
    // Build info passed in from CMake via target_compile_definitions so the
    // string stays in sync with project(... VERSION X.Y.Z) at root level.
    // Test target also defines these (see tests/CMakeLists.txt).
#ifndef AGENTIC_SYNTH_PROJECT_NAME
#define AGENTIC_SYNTH_PROJECT_NAME "TIMBRE"
#endif
#ifndef AGENTIC_SYNTH_VERSION_STRING
#define AGENTIC_SYNTH_VERSION_STRING "0.0.0"
#endif
    constexpr const char* kProjectName = AGENTIC_SYNTH_PROJECT_NAME;
    constexpr const char* kVersion = AGENTIC_SYNTH_VERSION_STRING;

    const juce::String bullet = juce::String::fromUTF8("\xe2\x80\xa2");

    juce::String msg;
    msg << "Failed to load " << kProjectName << " UI.\n"
        << "This usually means the system WebView is missing or broken.\n\n"
        << bullet << " macOS: requires WKWebView (system)\n"
        << bullet
        << " Windows: requires WebView2 Runtime "
           "(https://developer.microsoft.com/microsoft-edge/webview2/)\n"
        << bullet << " Linux: requires libwebkit2gtk-4.1-0\n\n"
        << "Version: " << kVersion << "\n"
        << "Details: " << (errorInfo.isEmpty() ? juce::String("(no error info)") : errorInfo);
    return msg;
}

WebUiComponent::FallbackComponent::FallbackComponent() {
    message_.setJustificationType(juce::Justification::centred);
    message_.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.92f));
    message_.setMinimumHorizontalScale(1.0f);
    message_.setFont(juce::Font(juce::FontOptions(14.0f)));
    message_.setText(WebUiComponent::buildFallbackMessage({}), juce::dontSendNotification);
    addAndMakeVisible(message_);

    copyButton_.onClick = [this]() { juce::SystemClipboard::copyTextToClipboard(errorInfo_); };
    copyButton_.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(0x33, 0x33, 0x3a));
    copyButton_.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    addAndMakeVisible(copyButton_);
}

void WebUiComponent::FallbackComponent::setError(const juce::String& errorInfo) {
    errorInfo_ = errorInfo;
    message_.setText(WebUiComponent::buildFallbackMessage(errorInfo), juce::dontSendNotification);
}

void WebUiComponent::FallbackComponent::paint(juce::Graphics& g) {
    // Dark background matches the existing app palette so the fallback does
    // not look like an unrelated error dialog.
    g.fillAll(juce::Colour::fromRGB(0x12, 0x12, 0x16));
}

void WebUiComponent::FallbackComponent::resized() {
    auto bounds = getLocalBounds().reduced(40);
    constexpr int kButtonHeight = 36;
    constexpr int kButtonWidth = 200;
    auto buttonRow = bounds.removeFromBottom(kButtonHeight + 12).withTrimmedBottom(0);
    auto buttonBounds = buttonRow.withSizeKeepingCentre(kButtonWidth, kButtonHeight);
    copyButton_.setBounds(buttonBounds);
    message_.setBounds(bounds);
}

void WebUiComponent::handleLoadFailure(const juce::String& errorInfo) {
    // Avoid duplicating the swap if multiple errors fire for the same load.
    bool expected = false;
    if (!loadFailed_.compare_exchange_strong(expected, true)) {
        // Update the displayed error text but don't re-add the component.
        juce::MessageManager::callAsync([this, errorInfo]() {
            lastLoadError_ = errorInfo;
            fallback_.setError(errorInfo);
        });
        return;
    }

    juce::MessageManager::callAsync([this, errorInfo]() {
        lastLoadError_ = errorInfo;
        fallback_.setError(errorInfo);
        if (browser_)
            browser_->setVisible(false);
        addAndMakeVisible(fallback_);
        fallback_.setBounds(getLocalBounds());
        fallback_.toFront(false);
    });
}

// ── Constructor: wire everything ─────────────────────────────────────────────

WebUiComponent::WebUiComponent(agent::AgentBridge& bridge)
    : bridge_(bridge),
      // Worker pool for `generate` (and any future native handler that needs
      // a background thread but must NOT outlive the component). 2 threads is
      // enough for UI-driven work (LLM heuristic + rationale, file IO,
      // future async handlers) — these tasks are coarse-grained and not
      // latency-sensitive. The pool is owned by the component so dtor can
      // drain pending/active jobs via removeAllJobs(interrupt=true) before
      // bridge_ goes away, eliminating the use-after-free that
      // `juce::Thread::launch` had (architect P1 #15).
      workerPool_(juce::ThreadPool::Options{}
                      .withNumberOfThreads(2)
                      .withThreadName("WebUiWorker")) {
    using Options = juce::WebBrowserComponent::Options;
    using NativeFnCompletion = juce::WebBrowserComponent::NativeFunctionCompletion;

    Options options =
        Options{}
            .withNativeIntegrationEnabled(true)
            .withKeepPageLoadedWhenBrowserIsHidden()
            .withResourceProvider([](const juce::String& p) { return WebUiComponent::serveResource(p); })
            // Persistent, per-slot WebView2 user-data folder under
            // userApplicationDataDirectory (NOT temp). This survives DAW project
            // reopen, OS temp cleaners, and machine reboots, so localStorage,
            // cookies and IndexedDB written by the React UI persist. The
            // <stableId> segment is a UUID written once on first run; the
            // slot-<N> child disambiguates concurrent controllers when a DAW
            // hosts multiple plugin instances in the same process (WebView2
            // requires distinct user-data folders for concurrent controllers).
            // Apple/Linux backends ignore this option but the directory is
            // still safe to materialise on those platforms.
            .withWinWebView2Options(Options::WinWebView2{}.withUserDataFolder(webView2UserDataFolderForThisInstance()));

    // ── 8 native functions (UI → C++) ────────────────────────────────────────

    options = options.withNativeFunction(
        juce::Identifier{"knob_tweak"}, [this](const juce::Array<juce::var>& args, NativeFnCompletion completion) {
            const auto param = argOr(args, 0, juce::var{""}).toString().toStdString();
            const auto value = static_cast<float>(static_cast<double>(argOr(args, 1, juce::var{0.0})));
            bridge_.handleKnobTweak(param, value);
            completion(juce::var{});
        });

    options = options.withNativeFunction(juce::Identifier{"generate"},
                                         [this](const juce::Array<juce::var>& args, NativeFnCompletion completion) {
                                             const auto prompt = argOr(args, 0, juce::var{""}).toString().toStdString();
                                             // Resolve the JS promise immediately so the UI stays responsive.
                                             // The actual heuristic + rationale work runs on a worker; results
                                             // arrive at the UI via the onPatch / onRationale / onDone events.
                                             completion(juce::var{});

                                             // Architect P1 #15: switched from juce::Thread::launch to the
                                             // component-owned juce::ThreadPool so ~WebUiComponent can
                                             // drain in-flight work (removeAllJobs(interrupt=true)) before
                                             // bridge_ disappears, fixing a use-after-free when the editor
                                             // closes mid-submitPrompt.
                                             //
                                             // The job checks shouldExit() at each natural step boundary so
                                             // a dtor-issued signalJobShouldExit() can short-circuit the
                                             // rationale step and the subsequent notify*() calls.
                                             workerPool_.addJob([this, prompt]() {
                                                 auto* const self = juce::ThreadPoolJob::getCurrentThreadPoolJob();
                                                 const auto cancelled = [self]() noexcept {
                                                     return self != nullptr && self->shouldExit();
                                                 };

                                                 if (cancelled())
                                                     return;
                                                 // Heuristic patch first — instant local fallback.
                                                 PatchStruct patch = bridge_.submitPrompt(prompt);

                                                 if (cancelled())
                                                     return;
                                                 // ── Phase 22: snapshot prior patch + decide refinement ──
                                                 // Pull the last successful patch + prompt under the lock,
                                                 // then release it. We must NOT hold the mutex across the
                                                 // LLM call (it can take seconds). isRelativePrompt is a
                                                 // free static; no allocation on the audio thread, no
                                                 // dependency on bridge_ state.
                                                 std::optional<PatchStruct> priorPatch;
                                                 std::string priorPrompt;
                                                 {
                                                     std::lock_guard<std::mutex> lock(lastPatchMutex_);
                                                     priorPatch = lastSuccessfulPatch_;
                                                     priorPrompt = lastPrompt_;
                                                 }
                                                 const bool isRefinement =
                                                     agent::PromptHandler::isRelativePrompt(prompt) && priorPatch.has_value();

                                                 if (cancelled())
                                                     return;
                                                 // ── Step 1 of the 2-step LLM flow: ENHANCER ─────────────
                                                 // Phase 22: skip the enhancer entirely for refinement
                                                 // prompts. The enhancer rewrites a terse user prompt into
                                                 // a 9-section sensory brief — useful for cold-start
                                                 // ("warm pad") but actively harmful for ("darker"), where
                                                 // the directional intent must reach §5.3 verbatim. The
                                                 // refinement wrapper inside PromptHandler carries the
                                                 // previous patch instead, so the brief isn't needed.
                                                 std::string brief;
                                                 if (!isRefinement) {
                                                     std::cerr << "[WebUI] enhancing prompt='" << prompt << "'\n";
                                                     brief = bridge_.enhancePrompt(prompt);
                                                     if (cancelled())
                                                         return;
                                                     if (!brief.empty()) {
                                                         auto* eobj = new juce::DynamicObject{};
                                                         eobj->setProperty("brief", juce::String(brief));
                                                         bridge_.notifyEnhancement(juce::var{eobj});
                                                     }
                                                 } else {
                                                     std::cerr << "[WebUI] refinement prompt detected ('" << prompt
                                                               << "') — skipping enhancer, passing raw to LLM\n";
                                                 }
                                                 const std::string& promptForGen = brief.empty() ? prompt : brief;

                                                 if (cancelled())
                                                     return;
                                                 // ── Step 2 of the 2-step LLM flow: GENERATOR ────────────
                                                 // Local llama.cpp → Gemini fallback. Feed the brief if we
                                                 // have one; otherwise feed the raw prompt verbatim. For
                                                 // refinement prompts, also forward the previous patch +
                                                 // prompt so PromptHandler wraps the request in §5.3
                                                 // refinement frame.
                                                 std::cerr << "[WebUI] generate prompt (post-enhance, " << promptForGen.size()
                                                           << " bytes) — invoking LLM"
                                                           << (isRefinement ? " (refinement)" : "") << "\n";
                                                 if (auto llm = bridge_.generateLlmPatch(promptForGen, /*patch_id=*/0,
                                                                                          priorPatch, priorPrompt)) {
                                                     patch = *llm;
                                                     // Stash the new patch as the next refinement seed.
                                                     std::lock_guard<std::mutex> lock(lastPatchMutex_);
                                                     lastSuccessfulPatch_ = patch;
                                                     lastPrompt_ = prompt;
                                                 } else {
                                                     // Phase 31 — LLM failed. The heuristic patch from
                                                     // submitPrompt() (line above) has NOT been through the
                                                     // PatchAugmenter, so all the Phase 23/27/30 cinematic
                                                     // guardrails (3-osc layering, FM coercion, cinematic
                                                     // pad recipe, noise-only fix) are bypassed exactly when
                                                     // they're needed most. Fire the augmenter now, applying
                                                     // the same refinement-skip rule generateLlmPatch uses
                                                     // internally — if the user typed a relative prompt and
                                                     // we have a prior patch, leave the heuristic patch
                                                     // alone so the bare topology survives.
                                                     std::cerr << "[WebUI] LLM failed — applying heuristic-patch guardrail"
                                                               << (isRefinement ? " (skipped: refinement)" : "") << "\n";
                                                     bridge_.applyGuardrailIfNotRefinement(patch, prompt,
                                                                                            priorPatch.has_value());
                                                 }

                                                 if (cancelled())
                                                     return;
                                                 const std::string rationale = bridge_.generateRationale(prompt, patch);

                                                 if (cancelled())
                                                     return;
                                                 auto* pobj = new juce::DynamicObject{};
                                                 pobj->setProperty("variation", juce::String("A"));
                                                 pobj->setProperty("data", agent::AgentBridge::patchToVar(patch));
                                                 pobj->setProperty("modulation", agent::AgentBridge::modulationPlanForPatch(patch));
                                                 // Phase 26: surface PatchAugmenter mutations so the UI can render
                                                 // a transparency banner ("Patch adjusted: …"). Pipe-separated
                                                 // buffer → array of strings; empty buffer → empty array which
                                                 // the UI treats as "no banner".
                                                 if (patch.augmenter_actions[0] != '\0') {
                                                     juce::Array<juce::var> actions;
                                                     juce::String raw{patch.augmenter_actions};
                                                     for (auto& tok : juce::StringArray::fromTokens(raw, "|", "")) {
                                                         if (!tok.trim().isEmpty())
                                                             actions.add(juce::var{tok.trim()});
                                                     }
                                                     pobj->setProperty("augmenter_actions", juce::var{actions});
                                                 }
                                                 bridge_.notifyPatch(juce::var{pobj});

                                                 auto* update = new juce::DynamicObject{};
                                                 update->setProperty("patch", agent::AgentBridge::patchToVar(patch));
                                                 update->setProperty("modulation", agent::AgentBridge::modulationPlanForPatch(patch));
                                                 bridge_.notifyPatchUpdate(juce::var{update});

                                                 if (cancelled())
                                                     return;
                                                 // Emit rationale as a token frame too so it appears as the
                                                 // primary chat bubble text. Without this, the bubble stays
                                                 // visually empty and the rationale only shows when the user
                                                 // clicks the collapsed "Why this patch?" details element.
                                                 // JS handler reads msg.content (not msg.text).
                                                 {
                                                     auto* tobj = new juce::DynamicObject{};
                                                     tobj->setProperty("content", juce::String(rationale));
                                                     bridge_.notifyToken(juce::var{tobj});
                                                 }

                                                 if (cancelled())
                                                     return;
                                                 auto* robj = new juce::DynamicObject{};
                                                 robj->setProperty("text", juce::String(rationale));
                                                 bridge_.notifyRationale(juce::var{robj});

                                                 if (cancelled())
                                                     return;
                                                 bridge_.notifyDone(juce::var{});
                                             });
                                         });

    options = options.withNativeFunction(
        juce::Identifier{"feedback"}, [this](const juce::Array<juce::var>& args, NativeFnCompletion completion) {
            // args: [messageId, kind, patch?]. We only need kind + patch
            // shape for recordFeedback; messageId is opaque/UI-only.
            const auto kindStr = argOr(args, 1, juce::var{""}).toString();
            agent::FeedbackKind kind = agent::FeedbackKind::Dislike;
            if (kindStr == "like")
                kind = agent::FeedbackKind::Like;
            else if (kindStr == "dislike")
                kind = agent::FeedbackKind::Dislike;
            else if (kindStr == "tweak")
                kind = agent::FeedbackKind::Tweak;

            // Prefer the full nested PatchParams payload. Older UI builds sent
            // a six-field preview, so keep that fallback for feedback safety.
            PatchStruct patch = make_default_patch();
            const auto& patchVar = argOr(args, 2, juce::var{});
            if (auto* obj = patchVar.getDynamicObject()) {
                if (obj->hasProperty("osc")) {
                    patch = agent::AgentBridge::patchFromVar(patchVar);
                } else {
                    patch.filter.cutoff_hz = static_cast<float>(static_cast<double>(obj->getProperty("cutoffHz")));
                    patch.filter.resonance = static_cast<float>(static_cast<double>(obj->getProperty("resonance")));
                    patch.amp_env.attack_s = static_cast<float>(static_cast<double>(obj->getProperty("attackS")));
                    patch.amp_env.sustain = static_cast<float>(static_cast<double>(obj->getProperty("sustainLevel")));
                    patch.lfo[0].depth = static_cast<float>(static_cast<double>(obj->getProperty("lfoDepth")));
                    patch.reverb.mix = static_cast<float>(static_cast<double>(obj->getProperty("reverbMix")));
                }
            }
            bridge_.recordFeedback(kind, /*prompt*/ "", patch);
            completion(juce::var{});
        });

    options = options.withNativeFunction(juce::Identifier{"get_dictionary"},
                                         [this](const juce::Array<juce::var>& /*args*/, NativeFnCompletion completion) {
                                             // Resolve the JS Promise with the parsed JSON payload.
                                             const std::string json = bridge_.getDictionaryJson();
                                             completion(juce::JSON::parse(juce::String(json)));
                                         });

    options = options.withNativeFunction(juce::Identifier{"save_dictionary"},
                                         [this](const juce::Array<juce::var>& args, NativeFnCompletion completion) {
                                             // Frontend sends an entries array; we wrap it back into the
                                             // save_dictionary frame shape that SemanticMapper expects.
                                             const auto entries = argOr(args, 0, juce::var{});
                                             auto* wrapper = new juce::DynamicObject{};
                                             wrapper->setProperty("type", juce::String("save_dictionary"));
                                             wrapper->setProperty("entries", entries);
                                             const auto json = juce::JSON::toString(juce::var{wrapper});
                                             bridge_.saveDictionary(json.toStdString());
                                             completion(juce::var{});
                                         });

    options = options.withNativeFunction(juce::Identifier{"get_telemetry"},
                                         [this](const juce::Array<juce::var>& /*args*/, NativeFnCompletion completion) {
                                             const std::string json = bridge_.getTelemetryJson();
                                             completion(juce::JSON::parse(juce::String(json)));
                                         });

    options = options.withNativeFunction(juce::Identifier{"set_telemetry_enabled"},
                                         [this](const juce::Array<juce::var>& args, NativeFnCompletion completion) {
                                             const bool enabled = static_cast<bool>(argOr(args, 0, juce::var{false}));
                                             bridge_.setTelemetryEnabled(enabled);
                                             completion(juce::var{});
                                         });

    options = options.withNativeFunction(
        juce::Identifier{"note_on"}, [this](const juce::Array<juce::var>& args, NativeFnCompletion completion) {
            // Expressive open-ended hold path. JS calls note_on on pointerdown
            // / keydown and note_off on release. Engine sustains until note_off.
            const int note = static_cast<int>(argOr(args, 0, juce::var{60}));
            const float velocity = static_cast<float>(static_cast<double>(argOr(args, 1, juce::var{0.8})));
            bridge_.postMidiNoteOn(note, velocity);
            completion(juce::var{});
        });

    options = options.withNativeFunction(
        juce::Identifier{"note_off"}, [this](const juce::Array<juce::var>& args, NativeFnCompletion completion) {
            const int note = static_cast<int>(argOr(args, 0, juce::var{60}));
            bridge_.postMidiNoteOff(note);
            completion(juce::var{});
        });

    options = options.withNativeFunction(
        juce::Identifier{"play_midi_note"}, [this](const juce::Array<juce::var>& args, NativeFnCompletion completion) {
            // Phase 6C: in-browser audition keyboard. Args:
            //   [0] note (int, 0..127)
            //   [1] velocity (double, 0..1)
            //   [2] duration_ms (int)
            // AgentBridge::postMidiNote clamps and dispatches both note-on
            // and the matched note-off via the message thread. Resolve the
            // JS promise immediately so the keyboard stays responsive even
            // if the engine is mid-block.
            const int note = static_cast<int>(argOr(args, 0, juce::var{60}));
            const float velocity = static_cast<float>(static_cast<double>(argOr(args, 1, juce::var{0.8})));
            const int durationMs = static_cast<int>(argOr(args, 2, juce::var{350}));
            bridge_.postMidiNote(note, velocity, durationMs);
            completion(juce::var{});
        });

    options = options.withNativeFunction(
        juce::Identifier{"open_external_url"},
        [](const juce::Array<juce::var>& args, NativeFnCompletion completion) {
            // Phase 15 fix: window.open('x-apple.systempreferences:…', '_self')
            // navigates the WKWebView itself to a scheme it can't handle →
            // "unsupported URL" fallback page. Route external URLs through
            // juce::URL which uses NSWorkspace on macOS / ShellExecute on
            // Windows / xdg-open on Linux — handles app: + custom schemes.
            const auto urlStr = args.size() > 0 ? args.getReference(0).toString() : juce::String{};
            if (urlStr.isNotEmpty()) {
                juce::URL(urlStr).launchInDefaultBrowser();
            }
            completion(juce::var{});
        });

    options = options.withNativeFunction(
        juce::Identifier{"push_audio_pcm"}, [this](const juce::Array<juce::var>& args, NativeFnCompletion completion) {
            // Phase 29 — replaces the stub WhisperClient path with Gemini
            // audio understanding. The JS side sends the full utterance as
            // base64 Int16 PCM at 16 kHz mono on release; we decode on a
            // worker, transcribe via Gemini, then emit the transcript via
            // the existing notifyTranscript event so the chat UI fills its
            // textarea with the parsed words.
            const auto b64 = argOr(args, 0, juce::var{""}).toString();
            // Resolve the JS promise immediately so the message thread is
            // free to drain other events. Decode + transcribe runs on a
            // JUCE worker.
            completion(juce::var{});

            if (!bridge_.sttEnabled()) {
                DBG("push_audio_pcm received but GeminiSTT disabled (no GEMINI_KEY)");
                auto* err = new juce::DynamicObject{};
                err->setProperty("text", juce::String("[mic ready but speech-to-text disabled — GEMINI_KEY not configured]"));
                bridge_.notifyTranscript(juce::var{err});
                return;
            }

            auto& bridge = bridge_;
            juce::Thread::launch([&bridge, b64]() {
                juce::MemoryOutputStream raw;
                if (!juce::Base64::convertFromBase64(raw, b64)) {
                    DBG("push_audio_pcm: base64 decode failed");
                    return;
                }
                const auto byteCount = raw.getDataSize();
                if (byteCount == 0 || (byteCount % sizeof(std::int16_t)) != 0) {
                    DBG("push_audio_pcm: bad payload size " << static_cast<int>(byteCount));
                    return;
                }
                const auto* samples = reinterpret_cast<const std::int16_t*>(raw.getData());
                const int numSamples = static_cast<int>(byteCount / sizeof(std::int16_t));
                const std::string transcript = bridge.transcribeAudio(samples, numSamples, 16000);
                if (transcript.empty()) {
                    DBG("push_audio_pcm: empty transcript from GeminiSTT");
                    return;
                }
                auto* obj = new juce::DynamicObject{};
                obj->setProperty("text", juce::String(transcript));
                bridge.notifyTranscript(juce::var{obj});
            });
        });

    options = options.withNativeFunction(
        juce::Identifier{"getScopeSamples"},
        [this](const juce::Array<juce::var>& args, NativeFnCompletion completion) {
            // Phase 12: visualizer audio tap. JS asks for up to N samples
            // (default 1024); we pull lock-free from the plugin's SPSC scope
            // queue on the message thread, pack into a juce::var Array of
            // numbers and resolve the JS promise. If no provider is wired
            // (browser dev, headless tests) we resolve with an empty array so
            // the React side cleanly falls back to its simulated synth path.
            int maxSamples = static_cast<int>(argOr(args, 0, juce::var{1024}));
            if (maxSamples <= 0) {
                completion(juce::var{juce::Array<juce::var>{}});
                return;
            }
            // Cap at scope queue capacity so a runaway JS request can never
            // ask us to drain more than the audio thread could ever have
            // produced. 4096 mirrors AgenticSynthPlugin::kScopeQueueCapacity.
            constexpr int kHardCap = 4096;
            if (maxSamples > kHardCap)
                maxSamples = kHardCap;

            if (!scopeProvider_) {
                completion(juce::var{juce::Array<juce::var>{}});
                return;
            }

            // Scratch buffer on the stack — small (≤4096 floats = 16 KB),
            // well within the 8 MB message-thread stack budget. No heap
            // allocation in the audio-bridge path.
            std::array<float, kHardCap> scratch{};
            const int n = scopeProvider_(scratch.data(), maxSamples);

            juce::Array<juce::var> out;
            out.ensureStorageAllocated(n);
            for (int i = 0; i < n; ++i)
                out.add(juce::var{static_cast<double>(scratch[static_cast<size_t>(i)])});
            completion(juce::var{std::move(out)});
        });

    // ── Construct the WebView with the fully-built options ───────────────────

    browser_ = std::make_unique<TelemetryAwareBrowser>(options, bridge_, *this);
    addAndMakeVisible(*browser_);

    // ── 8 AgentBridge subscriber hookups (C++ → UI) ──────────────────────────
    //
    // AgentBridge::dispatch() already marshals every callback onto the
    // message thread via MessageManager::callAsync (see AgentBridge.cpp).
    // Therefore we can call emitEventIfBrowserIsVisible directly from each
    // subscriber without further dispatch.

    subs_.push_back(bridge_.onToken([this](const juce::var& v) {
        if (browser_)
            browser_->emitEventIfBrowserIsVisible(juce::Identifier{"token"}, v);
    }));
    subs_.push_back(bridge_.onPatch([this](const juce::var& v) {
        if (browser_)
            browser_->emitEventIfBrowserIsVisible(juce::Identifier{"patch"}, v);
    }));
    subs_.push_back(bridge_.onDone([this](const juce::var& v) {
        if (browser_)
            browser_->emitEventIfBrowserIsVisible(juce::Identifier{"done"}, v);
    }));
    subs_.push_back(bridge_.onError([this](const juce::var& v) {
        if (browser_)
            browser_->emitEventIfBrowserIsVisible(juce::Identifier{"error"}, v);
    }));
    subs_.push_back(bridge_.onRationale([this](const juce::var& v) {
        if (browser_)
            browser_->emitEventIfBrowserIsVisible(juce::Identifier{"rationale"}, v);
    }));
    subs_.push_back(bridge_.onSuggestVariations([this](const juce::var& v) {
        if (browser_)
            browser_->emitEventIfBrowserIsVisible(juce::Identifier{"suggest_variations"}, v);
    }));
    subs_.push_back(bridge_.onPatchUpdate([this](const juce::var& v) {
        if (browser_)
            browser_->emitEventIfBrowserIsVisible(juce::Identifier{"patch_update"}, v);
    }));
    subs_.push_back(bridge_.onTranscript([this](const juce::var& v) {
        if (browser_)
            browser_->emitEventIfBrowserIsVisible(juce::Identifier{"transcript"}, v);
    }));
    // Two-step LLM flow: the ENHANCER stage emits its 9-section brief
    // exactly once per generate call. React ticker swaps from HEARING IT
    // OUT to SHAPING when this fires (see ChatInterface ReasoningTicker).
    subs_.push_back(bridge_.onEnhancement([this](const juce::var& v) {
        if (browser_)
            browser_->emitEventIfBrowserIsVisible(juce::Identifier{"enhancement"}, v);
    }));

#if AGENTIC_SYNTH_UI_DEV
    browser_->goToURL("http://localhost:5173");
#else
    browser_->goToURL(juce::WebBrowserComponent::getResourceProviderRoot());
#endif
}

WebUiComponent::~WebUiComponent() {
    // Architect P1 #15: drain the worker pool BEFORE anything else. Pending /
    // running `generate` jobs capture `this` and touch `bridge_`; if we let
    // the editor go away first they UAF. removeAllJobs(interruptRunningJobs,
    // timeoutMs) signals each active job's shouldExit() and blocks until they
    // either return or the timeout expires. 5 s is comfortably more than any
    // heuristic + rationale call costs in practice; if a worker truly hangs
    // past that, leaking is worse than letting the destructor return, so we
    // do not assert on the bool result.
    workerPool_.removeAllJobs(/*interruptRunningJobs=*/true, /*timeOutMilliseconds=*/5000);

    // SubscriberHandles release next so no late callback can fire against a
    // half-destructed browser_. (Members are destroyed in reverse-declaration
    // order; subs_ comes after browser_ in the header, so destroy it manually.)
    subs_.clear();
}

void WebUiComponent::submitWorkerForTesting(std::function<void()> job) {
    workerPool_.addJob(std::move(job));
}

int WebUiComponent::pendingWorkerJobsForTesting() const noexcept {
    return workerPool_.getNumJobs();
}

void WebUiComponent::resized() {
    if (browser_)
        browser_->setBounds(getLocalBounds());
    if (fallback_.isVisible())
        fallback_.setBounds(getLocalBounds());
}

} // namespace agentic_synth::ui
