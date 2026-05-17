#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include <juce_core/juce_core.h>

#include "agent/AgentBridge.h"
#include "engine/PatchStruct.h"

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
//
// Lifetime contract:
//   WebUiComponent holds a reference to AgentBridge (bridge_ below). The
//   owner (typically AgenticSynthPlugin's editor) MUST guarantee that the
//   AgentBridge outlives this WebUiComponent.
//
//   The internal ThreadPool drains workers in the dtor with
//   removeAllJobs(/*interruptRunning=*/true, /*timeoutMs=*/5000). Cooperative
//   jobs that capture `this` or `bridge_` MUST poll
//   juce::ThreadPoolJob::getCurrentThreadPoolJob()->shouldExit() before any
//   long-running step so they exit inside that 5-second window. If a job
//   blocks past the timeout it may still hold a reference to `bridge_` after
//   ~WebUiComponent returns — therefore the owner MUST NOT destroy the
//   AgentBridge until every WebUiComponent that references it has been
//   destructed AND its dtor has returned.
class WebUiComponent : public juce::Component {
public:
    explicit WebUiComponent(agent::AgentBridge& bridge);
    ~WebUiComponent() override;

    void resized() override;

    // Optional: route push_audio_pcm calls into a WhisperClient. If unset,
    // pushed PCM is logged and dropped. The component does not own the client.
    void setWhisperClient(agent::WhisperClient* client) noexcept { whisperClient_ = client; }

    // Phase 12: scope sample provider hookup. Caller (typically the
    // AudioProcessor editor) wires this to AgenticSynthPlugin::pullScopeSamples.
    // The provider is invoked synchronously from the `getScopeSamples` native
    // function on the JUCE message thread: it MUST be wait-free w.r.t. the
    // audio thread (the plugin's SPSC scope queue satisfies this). When unset
    // the native function resolves with an empty array, which lets the JS
    // Visualizer fall back to its simulated source path.
    using ScopeSampleProvider = std::function<int(float* dest, int max)>;
    void setScopeSampleProvider(ScopeSampleProvider provider) { scopeProvider_ = std::move(provider); }

    // Pure resource lookup against the bundled UI binary data. Exposed as a
    // free static so unit tests can exercise it without instantiating the
    // component (which would require a windowed parent + a live WebView).
    //
    // Returns std::nullopt for unknown paths. "/" is rewritten to
    // "/index.html". MIME type is selected by file extension.
    static std::optional<juce::WebBrowserComponent::Resource> serveResource(const juce::String& path);

    // Exposed for testing: returns the number of live SubscriberHandles
    // currently held by this component (one per AgentBridge.on* hookup).
    [[nodiscard]] std::size_t subscriberCountForTesting() const noexcept { return subs_.size(); }

    // True until a page-load network error has been observed via the
    // TelemetryAwareBrowser lifecycle hook. Exposed for tests and for parent
    // components that want to know whether the fallback UI is showing.
    [[nodiscard]] bool didLoadSucceed() const noexcept { return !loadFailed_.load(); }

    // Test hook: submit a worker job through the same ThreadPool the
    // `generate` native handler uses. Lets tests assert lifecycle (cancel on
    // dtor, concurrent jobs) without driving the WebView's JS bridge.
    void submitWorkerForTesting(std::function<void()> job);

    // Test hook: number of jobs currently queued or running in the worker
    // pool. Useful for asserting that the dtor drained them.
    [[nodiscard]] int pendingWorkerJobsForTesting() const noexcept;

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
        TelemetryAwareBrowser(const Options& options, agent::AgentBridge& bridge, WebUiComponent& owner)
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

    // Pool for UI worker tasks (LLM call, rationale, etc.). Declared BEFORE
    // browser_/subs_ so that when ~WebUiComponent runs, member destruction
    // order (reverse-declaration) tears down browser_/subs_ first, and the
    // pool last. However we explicitly drain the pool at the very top of the
    // dtor (with interruptRunningJobs=true) so workers never observe a
    // half-destructed component or bridge.
    //
    // Cancellation contract: jobs that capture `this`/`bridge_` MUST check
    // juce::ThreadPoolJob::getCurrentThreadPoolJob()->shouldExit() before
    // long-running steps; the dtor calls removeAllJobs(true, timeout) which
    // signals every active job to exit.
    juce::ThreadPool workerPool_;

    std::unique_ptr<TelemetryAwareBrowser> browser_;
    FallbackComponent fallback_;
    std::vector<agent::AgentBridge::SubscriberHandle> subs_;
    agent::WhisperClient* whisperClient_{nullptr};

    // Phase 12: scope sample provider — set by the editor (which owns the
    // AudioProcessor reference). Invoked on the message thread from the
    // `getScopeSamples` native function. Default-empty so the JS bridge call
    // returns an empty array when no provider is wired (browser dev / tests).
    ScopeSampleProvider scopeProvider_;

    std::atomic<bool> loadFailed_{false};
    juce::String lastLoadError_;

    // Phase 22 — refinement context. The `generate` worker stores the last
    // successful patch + the prompt that produced it so the NEXT generate
    // call can pass them into AgentBridge::generateLlmPatch and trigger
    // refinement mode for relative prompts ("darker", "more wobble") instead
    // of regenerating from scratch.
    //
    // Mutex contract: the worker pool may run two generate jobs back-to-back
    // (or, in theory, concurrently — workerPool_ has 2 threads). Read/write
    // happen on worker threads; we serialise via lastPatchMutex_. Holding it
    // across the LLM call would block a second submission; so we COPY out of
    // the snapshot under the lock, run generation lock-free, then re-acquire
    // the lock to write the new patch. Worst case, two simultaneous prompts
    // race and the later writer wins — acceptable: the UI itself is gated to
    // one inflight generate at a time, this is belt-and-braces.
    mutable std::mutex lastPatchMutex_;
    std::optional<PatchStruct> lastSuccessfulPatch_;
    std::string lastPrompt_;

    // Phase B simple-view #249 — rolling history + liked-pool for the
    // `morph_request` worker. We keep a small deque per axis so we never
    // unbounded-grow inside a long DAW session. Liked is wired via the
    // existing `feedback` native function (kind == "like"); history is
    // populated by morph_request itself so consecutive clicks accumulate
    // generations.
    mutable std::mutex morphMutex_;
    std::deque<PatchStruct> morphHistory_;
    std::deque<PatchStruct> morphLiked_;
    uint32_t morphSeedCounter_{0};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WebUiComponent)
};

} // namespace agentic_synth::ui
