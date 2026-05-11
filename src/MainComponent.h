#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

#include "agent/AgentBridge.h"
#include "ui/WebUiComponent.h"

//==============================================================================
// Phase 4: the standalone host window is the WebView, end to end. The
// welcome label is gone; the React UI fills the bounds and talks to the
// passed-in AgentBridge through the native bridge.
class MainComponent final : public juce::Component {
public:
    explicit MainComponent(agentic_synth::agent::AgentBridge& bridge);
    ~MainComponent() override = default;

    void resized() override;

private:
    agentic_synth::ui::WebUiComponent web_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
