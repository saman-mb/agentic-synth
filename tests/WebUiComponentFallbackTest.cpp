// SRE P0/P1: WebView load-failure fallback.
//
// We cannot exercise the live fallback swap without a broken WebView host
// (WKWebView / WebView2 / libwebkit2gtk), so this test pins the pure pieces
// of the contract that DO survive without a runtime:
//
//   1. buildFallbackMessage(...) — static, pure, deterministic. Verify the
//      diagnostic text mentions every platform requirement so a user filing
//      a bug report on any OS has actionable context.
//   2. didLoadSucceed() accessor — present on the component. The test
//      constructs the component (which requires a GUI environment, same as
//      WebUiComponentEmissionTest) and confirms the accessor returns true
//      before any error fires.
//   3. Telemetry::recordUiEvent — callable through the same API the
//      WebUiComponent uses, asserted independently of the WebView so the
//      wiring contract holds even where no real browser is available.

#include "ui/WebUiComponent.h"
#include "agent/AgentBridge.h"
#include "agent/Telemetry.h"

#include <catch2/catch_test_macros.hpp>

#include <juce_events/juce_events.h>
#include <juce_gui_extra/juce_gui_extra.h>

#include <memory>

using agentic_synth::agent::AgentBridge;
using agentic_synth::agent::Telemetry;
using agentic_synth::ui::WebUiComponent;

namespace {
struct GuiFixture {
    juce::ScopedJuceInitialiser_GUI gui;
};
} // namespace

TEST_CASE("buildFallbackMessage covers every platform's WebView dependency",
          "[WebUiComponent][Fallback]") {
    const auto msg = WebUiComponent::buildFallbackMessage("net::ERR_FAILED");

    // Brand + opening line.
    REQUIRE(msg.contains("Failed to load"));
    REQUIRE(msg.contains("Agentic Synth"));

    // Each platform's runtime requirement must be named explicitly so a
    // bug-report recipient knows immediately which OS install step to
    // recommend.
    REQUIRE(msg.contains("macOS"));
    REQUIRE(msg.contains("WKWebView"));
    REQUIRE(msg.contains("Windows"));
    REQUIRE(msg.contains("WebView2"));
    REQUIRE(msg.contains("Linux"));
    REQUIRE(msg.contains("libwebkit2gtk-4.1"));

    // The Microsoft installer URL is the highest-leverage piece of info for
    // Windows users hitting this path.
    REQUIRE(msg.contains("https://developer.microsoft.com/microsoft-edge/webview2/"));

    // The original error string is preserved verbatim for diagnostics.
    REQUIRE(msg.contains("net::ERR_FAILED"));

    // Version info is included for bug reports.
    REQUIRE(msg.contains("Version:"));
}

TEST_CASE("buildFallbackMessage handles empty error info gracefully",
          "[WebUiComponent][Fallback]") {
    const auto msg = WebUiComponent::buildFallbackMessage({});
    // Empty error must not produce a "Details: " line with nothing after it —
    // that would look broken to the user.
    REQUIRE(msg.contains("Details:"));
    REQUIRE(msg.contains("(no error info)"));
}

TEST_CASE("WebUiComponent reports successful load until an error fires",
          "[WebUiComponent][Fallback]") {
    GuiFixture fix;
    AgentBridge bridge;

    auto component = std::make_unique<WebUiComponent>(bridge);

    // Without a real WebView runtime we can't trigger a genuine load error,
    // but the initial state must be "load OK" so callers (e.g. integration
    // tests on a working host) can rely on the accessor.
    CHECK(component->didLoadSucceed());

    juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
    component.reset();
}

TEST_CASE("Telemetry::recordUiEvent is callable along the WebUiComponent wiring path",
          "[WebUiComponent][Fallback][Telemetry]") {
    // Mirror the exact call surface WebUiComponent uses: bridge.telemetry().
    // If this contract regresses (e.g. accessor renamed) the WebView
    // lifecycle hooks would silently drop events, defeating the SRE fix.
    AgentBridge bridge;
    bridge.telemetry().setEnabled(true);

    bridge.telemetry().recordUiEvent("page_about_to_load", "agentic://test");
    bridge.telemetry().recordUiEvent("page_load_error", "net::ERR_CONNECTION_REFUSED");

    const auto events = bridge.telemetry().uiEvents();
    REQUIRE(events.size() == 2);
    CHECK(events[0].kind == "page_about_to_load");
    CHECK(events[1].kind == "page_load_error");
    CHECK(events[1].detail == "net::ERR_CONNECTION_REFUSED");
}
