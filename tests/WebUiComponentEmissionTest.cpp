// Phase 4: validate that constructing a WebUiComponent wires all 8
// AgentBridge subscribers, and that emissions reaching the component would
// be forwarded to the WebView's emitEventIfBrowserIsVisible.
//
// We cannot actually receive events on the JS side in this test (no real
// browser runtime), so we verify only the subscription wiring: every
// notify*() emission on the bridge must reach a live SubscriberHandle that
// the component holds.

#include "ui/WebUiComponent.h"
#include "agent/AgentBridge.h"

#include <catch2/catch_test_macros.hpp>

#include <juce_events/juce_events.h>
#include <juce_gui_extra/juce_gui_extra.h>

#include <memory>

using agentic_synth::agent::AgentBridge;
using agentic_synth::ui::WebUiComponent;

namespace {
// WebUiComponent allocates a juce::WebBrowserComponent internally, which on
// some platforms requires the GUI subsystem to be initialised. The test
// harness pulls in juce_gui_extra so this is sufficient.
struct GuiFixture {
    juce::ScopedJuceInitialiser_GUI gui;
};
} // namespace

TEST_CASE("WebUiComponent registers all 8 AgentBridge subscribers on construction",
          "[WebUiComponent][Emission]") {
    GuiFixture fix;
    AgentBridge bridge;

    // Build the component on the heap so we can destroy it explicitly and
    // observe that subscribers go away.
    auto component = std::make_unique<WebUiComponent>(bridge);

    // 8 hookups: token, patch, done, error, rationale, suggest_variations,
    // patch_update, transcript.
    CHECK(component->subscriberCountForTesting() == 8u);

    // Sanity-check that emissions don't crash with a live component bound.
    // We cannot observe the WebView receiving them — JUCE swallows them when
    // the browser is not visible — but the dispatch path must be exercise-able.
    bridge.notifyToken(juce::var{new juce::DynamicObject{}});
    bridge.notifyPatch(juce::var{new juce::DynamicObject{}});
    bridge.notifyDone(juce::var{new juce::DynamicObject{}});
    bridge.notifyError(juce::var{new juce::DynamicObject{}});

    // Drain message thread so any queued callAsync runs while the component
    // is still alive.
    juce::MessageManager::getInstance()->runDispatchLoopUntil(50);

    // Destroy. Future emissions must not crash and must not invoke into
    // freed memory — the component's destructor clears its SubscriberHandles
    // before the WebBrowserComponent goes away.
    component.reset();

    bridge.notifyToken(juce::var{new juce::DynamicObject{}});
    juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
    SUCCEED("Post-destruction emissions did not crash");
}
