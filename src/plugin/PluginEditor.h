#pragma once

#include "plugin/AgenticSynthPlugin.h"
#include "ui/WebUiComponent.h"

#include <juce_audio_processors/juce_audio_processors.h>

//==============================================================================
// Phase 4: the plugin editor is now entirely the React UI hosted in a
// JUCE 8 WebBrowserComponent. The 8 rotary sliders are gone — control state
// flows through AgentBridge::handleKnobTweak via the native bridge.
class AgenticSynthPluginEditor final : public juce::AudioProcessorEditor {
public:
    explicit AgenticSynthPluginEditor(AgenticSynthPlugin& p);
    ~AgenticSynthPluginEditor() override = default;

    void resized() override;

private:
    AgenticSynthPlugin& processor_;
    agentic_synth::ui::WebUiComponent web_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AgenticSynthPluginEditor)
};
