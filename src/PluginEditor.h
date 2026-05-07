#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

//==============================================================================
class AgenticSynthAudioProcessorEditor final : public juce::AudioProcessorEditor
{
public:
    explicit AgenticSynthAudioProcessorEditor (AgenticSynthAudioProcessor&);
    ~AgenticSynthAudioProcessorEditor() override = default;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::Label placeholderLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AgenticSynthAudioProcessorEditor)
};
