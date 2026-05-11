#include "ui/WebUiComponent.h"

#include "UiBinaryData.h"
#include "agent/WhisperClient.h"

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
        } else if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
                   || (c >= '0' && c <= '9') || c == '_') {
            out += c;
        }
        // else: drop (matches retainCharacters in JUCE).
    }
    return out;
}

juce::String mimeForPath(const juce::String& path) {
    if (path.endsWithIgnoreCase(".html")) return "text/html";
    if (path.endsWithIgnoreCase(".js"))   return "text/javascript";
    if (path.endsWithIgnoreCase(".mjs"))  return "text/javascript";
    if (path.endsWithIgnoreCase(".css"))  return "text/css";
    if (path.endsWithIgnoreCase(".svg"))  return "image/svg+xml";
    if (path.endsWithIgnoreCase(".woff2"))return "font/woff2";
    if (path.endsWithIgnoreCase(".woff")) return "font/woff";
    if (path.endsWithIgnoreCase(".json")) return "application/json";
    if (path.endsWithIgnoreCase(".png"))  return "image/png";
    if (path.endsWithIgnoreCase(".jpg") || path.endsWithIgnoreCase(".jpeg")) return "image/jpeg";
    if (path.endsWithIgnoreCase(".ico"))  return "image/x-icon";
    return "application/octet-stream";
}

// Convenience: extract a juce::var argument by index with a default fallback.
juce::var argOr(const juce::Array<juce::var>& args, int index, const juce::var& fallback) {
    return (index >= 0 && index < args.size()) ? args[index] : fallback;
}

} // namespace

std::optional<juce::WebBrowserComponent::Resource>
WebUiComponent::serveResource(const juce::String& path) {
    // Normalise: strip any query/fragment, default "/" → "/index.html".
    juce::String trimmed = path.upToFirstOccurrenceOf("?", false, false)
                               .upToFirstOccurrenceOf("#", false, false);
    if (trimmed.isEmpty() || trimmed == "/") {
        trimmed = "/index.html";
    }

    if (trimmed.contains("..") || trimmed.contains("\\")) {
        return std::nullopt;
    }

    juce::String relative = trimmed.startsWith("/") ? trimmed.substring(1) : trimmed;
    juce::String basename = relative.fromLastOccurrenceOf("/", false, false);
    juce::String mangled  = mangleResourceName(basename);

    int numBytes = 0;
    const char* data = UiBinaryData::getNamedResource(mangled.toRawUTF8(), numBytes);
    if (data == nullptr || numBytes <= 0) {
        return std::nullopt;
    }

    juce::WebBrowserComponent::Resource resource;
    resource.data.assign(reinterpret_cast<const std::byte*>(data),
                         reinterpret_cast<const std::byte*>(data) + numBytes);
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
#define AGENTIC_SYNTH_PROJECT_NAME "Agentic Synth"
#endif
#ifndef AGENTIC_SYNTH_VERSION_STRING
#define AGENTIC_SYNTH_VERSION_STRING "0.0.0"
#endif
    constexpr const char* kProjectName = AGENTIC_SYNTH_PROJECT_NAME;
    constexpr const char* kVersion     = AGENTIC_SYNTH_VERSION_STRING;

    const juce::String bullet = juce::String::fromUTF8("\xe2\x80\xa2");

    juce::String msg;
    msg << "Failed to load " << kProjectName << " UI.\n"
        << "This usually means the system WebView is missing or broken.\n\n"
        << bullet << " macOS: requires WKWebView (system)\n"
        << bullet << " Windows: requires WebView2 Runtime "
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

    copyButton_.onClick = [this]() {
        juce::SystemClipboard::copyTextToClipboard(errorInfo_);
    };
    copyButton_.setColour(juce::TextButton::buttonColourId,
                          juce::Colour::fromRGB(0x33, 0x33, 0x3a));
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
    constexpr int kButtonWidth  = 200;
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
        if (browser_) browser_->setVisible(false);
        addAndMakeVisible(fallback_);
        fallback_.setBounds(getLocalBounds());
        fallback_.toFront(false);
    });
}

// ── Constructor: wire everything ─────────────────────────────────────────────

WebUiComponent::WebUiComponent(agent::AgentBridge& bridge) : bridge_(bridge) {
    using Options = juce::WebBrowserComponent::Options;
    using NativeFnCompletion = juce::WebBrowserComponent::NativeFunctionCompletion;

    Options options = Options{}
        .withNativeIntegrationEnabled(true)
        .withKeepPageLoadedWhenBrowserIsHidden()
        .withResourceProvider([](const juce::String& p) { return WebUiComponent::serveResource(p); })
        // Per-instance WebView2 user-data folder avoids multi-DAW-instance
        // collisions on Windows. Apple/Linux backends ignore this option.
        .withWinWebView2Options(Options::WinWebView2{}
            .withUserDataFolder(juce::File::getSpecialLocation(juce::File::tempDirectory)
                                .getChildFile("AgenticSynth-WV2-" + juce::Uuid().toString())));

    // ── 8 native functions (UI → C++) ────────────────────────────────────────

    options = options.withNativeFunction(
        juce::Identifier{"knob_tweak"},
        [this](const juce::Array<juce::var>& args, NativeFnCompletion completion) {
            const auto param = argOr(args, 0, juce::var{""}).toString().toStdString();
            const auto value = static_cast<float>(static_cast<double>(argOr(args, 1, juce::var{0.0})));
            bridge_.handleKnobTweak(param, value);
            completion(juce::var{});
        });

    options = options.withNativeFunction(
        juce::Identifier{"generate"},
        [this](const juce::Array<juce::var>& args, NativeFnCompletion completion) {
            const auto prompt = argOr(args, 0, juce::var{""}).toString().toStdString();
            // Resolve the JS promise immediately so the UI stays responsive.
            // The actual heuristic + rationale work runs on a worker; results
            // arrive at the UI via the onPatch / onRationale / onDone events.
            completion(juce::var{});
            juce::Thread::launch([this, prompt]() {
                const PatchStruct patch = bridge_.submitPrompt(prompt);
                const std::string rationale = bridge_.generateRationale(prompt, patch);
                auto* pobj = new juce::DynamicObject{};
                pobj->setProperty("variation", juce::String("A"));
                pobj->setProperty("data", [&patch]() {
                    auto* d = new juce::DynamicObject{};
                    d->setProperty("cutoffHz", patch.filter.cutoff_hz);
                    d->setProperty("resonance", patch.filter.resonance);
                    d->setProperty("attackS", patch.amp_env.attack_s);
                    d->setProperty("sustainLevel", patch.amp_env.sustain);
                    d->setProperty("lfoDepth", patch.lfo[0].depth);
                    d->setProperty("reverbMix", patch.reverb.mix);
                    return juce::var{d};
                }());
                bridge_.notifyPatch(juce::var{pobj});

                auto* robj = new juce::DynamicObject{};
                robj->setProperty("text", juce::String(rationale));
                bridge_.notifyRationale(juce::var{robj});
                bridge_.notifyDone(juce::var{});
            });
        });

    options = options.withNativeFunction(
        juce::Identifier{"feedback"},
        [this](const juce::Array<juce::var>& args, NativeFnCompletion completion) {
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

            // The patch payload arrives shaped like PatchPreviewData. Map
            // the preview fields back onto PatchStruct so SessionMemory's
            // learning signal includes the actual liked/disliked parameter
            // values, not a default-constructed patch.
            PatchStruct patch{};
            const auto& patchVar = argOr(args, 2, juce::var{});
            if (auto* obj = patchVar.getDynamicObject()) {
                patch.filter.cutoff_hz = static_cast<float>(static_cast<double>(obj->getProperty("cutoffHz")));
                patch.filter.resonance = static_cast<float>(static_cast<double>(obj->getProperty("resonance")));
                patch.amp_env.attack_s = static_cast<float>(static_cast<double>(obj->getProperty("attackS")));
                patch.amp_env.sustain  = static_cast<float>(static_cast<double>(obj->getProperty("sustainLevel")));
                patch.lfo[0].depth     = static_cast<float>(static_cast<double>(obj->getProperty("lfoDepth")));
                patch.reverb.mix       = static_cast<float>(static_cast<double>(obj->getProperty("reverbMix")));
            }
            bridge_.recordFeedback(kind, /*prompt*/ "", patch);
            completion(juce::var{});
        });

    options = options.withNativeFunction(
        juce::Identifier{"get_dictionary"},
        [this](const juce::Array<juce::var>& /*args*/, NativeFnCompletion completion) {
            // Resolve the JS Promise with the parsed JSON payload.
            const std::string json = bridge_.getDictionaryJson();
            completion(juce::JSON::parse(juce::String(json)));
        });

    options = options.withNativeFunction(
        juce::Identifier{"save_dictionary"},
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

    options = options.withNativeFunction(
        juce::Identifier{"get_telemetry"},
        [this](const juce::Array<juce::var>& /*args*/, NativeFnCompletion completion) {
            const std::string json = bridge_.getTelemetryJson();
            completion(juce::JSON::parse(juce::String(json)));
        });

    options = options.withNativeFunction(
        juce::Identifier{"set_telemetry_enabled"},
        [this](const juce::Array<juce::var>& args, NativeFnCompletion completion) {
            const bool enabled = static_cast<bool>(argOr(args, 0, juce::var{false}));
            bridge_.setTelemetryEnabled(enabled);
            completion(juce::var{});
        });

    options = options.withNativeFunction(
        juce::Identifier{"push_audio_pcm"},
        [this](const juce::Array<juce::var>& args, NativeFnCompletion completion) {
            // First arg is expected to be a Float32 array marshalled as a
            // juce::Array<juce::var> of numbers. Convert and forward.
            if (whisperClient_ == nullptr) {
                DBG("push_audio_pcm received but no WhisperClient wired");
                completion(juce::var{});
                return;
            }
            const auto& pcm = argOr(args, 0, juce::var{});
            if (const auto* arr = pcm.getArray()) {
                std::vector<std::int16_t> samples;
                samples.reserve(static_cast<size_t>(arr->size()));
                for (const auto& v : *arr) {
                    const float f = static_cast<float>(static_cast<double>(v));
                    const float clamped = juce::jlimit(-1.0f, 1.0f, f);
                    samples.push_back(static_cast<std::int16_t>(clamped * 32767.0f));
                }
                whisperClient_->feedAudio(samples.data(), static_cast<int>(samples.size()));
            }
            completion(juce::var{});
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
        if (browser_) browser_->emitEventIfBrowserIsVisible(juce::Identifier{"token"}, v);
    }));
    subs_.push_back(bridge_.onPatch([this](const juce::var& v) {
        if (browser_) browser_->emitEventIfBrowserIsVisible(juce::Identifier{"patch"}, v);
    }));
    subs_.push_back(bridge_.onDone([this](const juce::var& v) {
        if (browser_) browser_->emitEventIfBrowserIsVisible(juce::Identifier{"done"}, v);
    }));
    subs_.push_back(bridge_.onError([this](const juce::var& v) {
        if (browser_) browser_->emitEventIfBrowserIsVisible(juce::Identifier{"error"}, v);
    }));
    subs_.push_back(bridge_.onRationale([this](const juce::var& v) {
        if (browser_) browser_->emitEventIfBrowserIsVisible(juce::Identifier{"rationale"}, v);
    }));
    subs_.push_back(bridge_.onSuggestVariations([this](const juce::var& v) {
        if (browser_) browser_->emitEventIfBrowserIsVisible(juce::Identifier{"suggest_variations"}, v);
    }));
    subs_.push_back(bridge_.onPatchUpdate([this](const juce::var& v) {
        if (browser_) browser_->emitEventIfBrowserIsVisible(juce::Identifier{"patch_update"}, v);
    }));
    subs_.push_back(bridge_.onTranscript([this](const juce::var& v) {
        if (browser_) browser_->emitEventIfBrowserIsVisible(juce::Identifier{"transcript"}, v);
    }));

#if AGENTIC_SYNTH_UI_DEV
    browser_->goToURL("http://localhost:5173");
#else
    browser_->goToURL(juce::WebBrowserComponent::getResourceProviderRoot());
#endif
}

WebUiComponent::~WebUiComponent() {
    // SubscriberHandles release first so no late callback can fire against a
    // half-destructed browser_. (Members are destroyed in reverse-declaration
    // order; subs_ comes after browser_ in the header, so destroy it manually.)
    subs_.clear();
}

void WebUiComponent::resized() {
    if (browser_)
        browser_->setBounds(getLocalBounds());
    if (fallback_.isVisible())
        fallback_.setBounds(getLocalBounds());
}

} // namespace agentic_synth::ui
