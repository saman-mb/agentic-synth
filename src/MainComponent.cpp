#include "MainComponent.h"

//==============================================================================
MainComponent::MainComponent(agentic_synth::agent::AgentBridge& bridge) : web_(bridge) {
    addAndMakeVisible(web_);
    // 800x500 minimum keeps the WebView load-failure fallback message readable
    // on small standalone windows (SRE P0/P1 fallback diagnostic visibility).
    setSize(1200, 800);
}

void MainComponent::resized() { web_.setBounds(getLocalBounds()); }
