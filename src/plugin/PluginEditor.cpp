#include "plugin/PluginEditor.h"

//==============================================================================
AgenticSynthPluginEditor::AgenticSynthPluginEditor(AgenticSynthPlugin& p)
    : AudioProcessorEditor(p), processor_(p), web_(p.agentBridge()) {
    addAndMakeVisible(web_);
    // Phase 12: wire the visualizer audio tap. WebUiComponent's
    // `getScopeSamples` native function pulls drained samples through this
    // provider on the JUCE message thread (lock-free SPSC consumer side).
    // The lambda only references the AudioProcessor, whose lifetime strictly
    // outlives this editor (host-owned), so capture-by-reference is safe.
    web_.setScopeSampleProvider([&p](float* dest, int max) noexcept {
        return p.pullScopeSamples(dest, max);
    });
    setResizable(true, true);
    // 800x500 minimum guarantees the WebView load-failure fallback diagnostic
    // (P0/P1 SRE fix) stays unclipped on hosts that allow small editor windows.
    setResizeLimits(800, 500, 4096, 4096);
    setSize(1200, 800);
}

void AgenticSynthPluginEditor::resized() { web_.setBounds(getLocalBounds()); }
