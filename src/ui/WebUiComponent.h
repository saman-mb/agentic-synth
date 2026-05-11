#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

#include <atomic>
#include <optional>
#include <vector>

#include "agent/AgentBridge.h"

namespace agentic_synth::agent {
class WhisperClient;
}

namespace agentic_synth::ui {

// Phase 4: WebUiComponent is the real bridge between the JUCE host (audio
// engine + AgentBridge) and the bundled React UI running inside JUCE 8's
// WebBrowserComponent.
//
// Responsibilities:
//   • Register 8 native functions on the C++ side (UI → C++ requests).
//   • Subscribe to 8 AgentBridge typed callbacks and forward each emission
//     to the WebView via emitEventIfBrowserIsVisible (C++ → UI events).
//   • Serve the bundled React assets via the static resource provider.
//   • Hook WebView lifecycle (page load / load error) for telemetry.
//
// Multi-instance safety: each instance gets its own WebView2 user-data
// folder under temp/, disambiguated via juce::Uuid (Windows only).
class WebUiComponent : public juce::Component {
public:
    explicit WebUiComponent(agent::AgentBridge& bridge);
    ~WebUiComponent() override;

    void resized() override;

    // Optional: route push_audio_pcm calls into a WhisperClient. If unset,
    // pushed PCM is logged and dropped. The component does not own the client.
    void setWhisperClient(agent::WhisperClient* client) noexcept { whisperClient_ = client; }

    // Pure resource lookup against the bundled UI binary data. Exposed as a
    // free static so unit tests can exercise it without instantiating the
    // component (which would require a windowed parent + a live WebView).
    //
    // Returns std::nullopt for unknown paths. "/" is rewritten to
    // "/index.html". MIME type is selected by file extension.
    static std::optional<juce::WebBrowserComponent::Resource>
    serveResource(const juce::String& path);

    // Exposed for testing: returns the number of live SubscriberHandles
    // currently held by this component (one per AgentBridge.on* hookup).
    [[nodiscard]] std::size_t subscriberCountForTesting() const noexcept { return subs_.size(); }

    // True until a page-load network error has been observed via the
    // TelemetryAwareBrowser lifecycle hook. Exposed for tests and for parent
    // components that want to know whether the fallback UI is showing.
    [[nodiscard]] bool didLoadSucceed() const noexcept { return !loadFailed_.load(); }

    // Pure helper for composing the user-facing fallback message. Factored as
    // a static so unit tests can exercise it without instantiating the
    // component (which would require a real WebView runtime).
    [[nodiscard]] static juce::String buildFallbackMessage(const juce::String& errorInfo);

private:
    // Subclass WebBrowserComponent so we can override the lifecycle virtuals
    // (pageAboutToLoad / pageFinishedLoading / pageLoadHadNetworkError) and
    // route them into AgentBridge::telemetry() for visibility on failure.
    class TelemetryAwareBrowser : public juce::WebBrowserComponent {
    public:
        TelemetryAwareBrowser(const Options& options,
                              agent::AgentBridge& bridge,
                              WebUiComponent& owner)
            : juce::WebBrowserComponent(options), bridge_(bridge), owner_(owner) {}

        bool pageAboutToLoad(const juce::String& url) override;
        void pageFinishedLoading(const juce::String& url) override;
        bool pageLoadHadNetworkError(const juce::String& errorInfo) override;

    private:
        agent::AgentBridge& bridge_;
        WebUiComponent& owner_;
    };

    // Diagnostic panel shown when the underlying WebView fails to load.
    // Centred dark layout matching the app palette, with a "Copy diagnostic"
    // button that pushes the raw error info to the system clipboard so users
    // can paste it into bug reports.
    class FallbackComponent : public juce::Component {
    public:
        FallbackComponent();

        void setError(const juce::String& errorInfo);

        void paint(juce::Graphics& g) override;
        void resized() override;

    private:
        juce::Label message_;
        juce::TextButton copyButton_{"Copy diagnostic"};
        juce::String errorInfo_;
    };

    // Called by TelemetryAwareBrowser when a load error fires. Marshals onto
    // the message thread so the fallback swap is safe regardless of the
    // thread the lifecycle hook arrived on.
    void handleLoadFailure(const juce::String& errorInfo);

    agent::AgentBridge& bridge_;
    std::unique_ptr<TelemetryAwareBrowser> browser_;
    FallbackComponent fallback_;
    std::vector<agent::AgentBridge::SubscriberHandle> subs_;
    agent::WhisperClient* whisperClient_{nullptr};

    std::atomic<bool> loadFailed_{false};
    juce::String lastLoadError_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WebUiComponent)
};

} // namespace agentic_synth::ui
