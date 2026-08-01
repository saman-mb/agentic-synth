#include "MainComponent.h"

//==============================================================================
MainComponent::MainComponent(agentic_synth::agent::AgentBridge& bridge) : web_(bridge) {
    addAndMakeVisible(web_);
    // 800x500 minimum keeps the WebView load-failure fallback message readable
    // on small standalone windows (SRE P0/P1 fallback diagnostic visibility).
    // Size relative to the display — a fixed 1200x800 opens far too small on
    // a 4K panel. Shared with the plugin editor.
    const auto size = agentic_synth::ui::WebUiComponent::defaultWindowSize();
    setSize(size.getWidth(), size.getHeight());
}

void MainComponent::resized() { web_.setBounds(getLocalBounds()); }
