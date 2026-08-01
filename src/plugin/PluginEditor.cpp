#include "plugin/PluginEditor.h"

#if JucePlugin_Build_Standalone
    // Pulls in juce::StandalonePluginHolder so the React SETTINGS panel can
    // open the device dialog directly. getInstance() returns nullptr when this
    // same shared code is loaded as a VST3/AU, so the guard below is a runtime
    // check, not just a compile-time one.
    // juce_audio_utils first: the standalone header uses AudioDeviceManager,
    // AudioProcessorPlayer, and AudioDeviceSelectorComponent unqualified and
    // does not include them itself.
    #include <juce_audio_utils/juce_audio_utils.h>

    #include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>
#endif

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
    // Audio device picker. JUCE's standalone wrapper buries this behind an
    // "Options" text button in the title bar; surface it in the React SETTINGS
    // panel instead. Only the standalone wrapper has a device to configure —
    // under VST3/AU getInstance() is null and the handler stays unset, so the
    // panel hides the section.
#if JucePlugin_Build_Standalone
    if (juce::StandalonePluginHolder::getInstance() != nullptr) {
        // Re-resolve inside the lambda rather than capturing the pointer: the
        // holder outlives this editor in practice, but a captured raw pointer
        // would dangle if that ever stopped being true.
        web_.setAudioSettingsHandler([] {
            if (auto* holder = juce::StandalonePluginHolder::getInstance())
                holder->showAudioSettingsDialog();
        });
    }
#endif

    setResizable(true, true);
    // 800x500 minimum guarantees the WebView load-failure fallback diagnostic
    // (P0/P1 SRE fix) stays unclipped on hosts that allow small editor windows.
    setResizeLimits(800, 500, 4096, 4096);
    // Display-relative default; a fixed 1200x800 opens far too small on a 4K
    // panel. Shared with the standalone app via WebUiComponent.
    const auto size = agentic_synth::ui::WebUiComponent::defaultWindowSize();
    setSize(size.getWidth(), size.getHeight());
}

void AgenticSynthPluginEditor::resized() { web_.setBounds(getLocalBounds()); }
