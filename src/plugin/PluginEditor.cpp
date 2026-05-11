#include "plugin/PluginEditor.h"

//==============================================================================
AgenticSynthPluginEditor::AgenticSynthPluginEditor(AgenticSynthPlugin& p)
    : AudioProcessorEditor(p), processor_(p), web_(p.agentBridge()) {
    addAndMakeVisible(web_);
    setResizable(true, true);
    // 800x500 minimum guarantees the WebView load-failure fallback diagnostic
    // (P0/P1 SRE fix) stays unclipped on hosts that allow small editor windows.
    setResizeLimits(800, 500, 4096, 4096);
    setSize(1200, 800);
}

void AgenticSynthPluginEditor::resized() { web_.setBounds(getLocalBounds()); }
