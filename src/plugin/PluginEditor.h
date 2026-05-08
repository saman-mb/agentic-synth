#pragma once

#include "plugin/AgenticSynthPlugin.h"

#include <juce_audio_processors/juce_audio_processors.h>

//==============================================================================
class AgenticSynthPluginEditor final : public juce::AudioProcessorEditor {
public:
    explicit AgenticSynthPluginEditor(AgenticSynthPlugin& p);
    ~AgenticSynthPluginEditor() override = default;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    AgenticSynthPlugin& processor_;

    juce::Slider gainSlider_;
    juce::Slider cutoffSlider_;
    juce::Slider resonanceSlider_;
    juce::Slider attackSlider_;
    juce::Slider decaySlider_;
    juce::Slider sustainSlider_;
    juce::Slider releaseSlider_;
    juce::Slider portamentoSlider_;

    juce::Label gainLabel_;
    juce::Label cutoffLabel_;
    juce::Label resonanceLabel_;
    juce::Label attackLabel_;
    juce::Label decayLabel_;
    juce::Label sustainLabel_;
    juce::Label releaseLabel_;
    juce::Label portamentoLabel_;

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    SliderAttachment gainAttach_;
    SliderAttachment cutoffAttach_;
    SliderAttachment resonanceAttach_;
    SliderAttachment attackAttach_;
    SliderAttachment decayAttach_;
    SliderAttachment sustainAttach_;
    SliderAttachment releaseAttach_;
    SliderAttachment portamentoAttach_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AgenticSynthPluginEditor)
};
