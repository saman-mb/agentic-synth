#include "PluginEditor.h"
#include "PluginProcessor.h"

//==============================================================================
AgenticSynthAudioProcessorEditor::AgenticSynthAudioProcessorEditor(AgenticSynthAudioProcessor& p)
    : AudioProcessorEditor(p) {
    addAndMakeVisible(placeholderLabel);
    placeholderLabel.setText("Agentic Synth — plugin placeholder", juce::dontSendNotification);
    placeholderLabel.setFont(juce::Font(18.0f, juce::Font::bold));
    placeholderLabel.setJustificationType(juce::Justification::centred);

    setSize(600, 400);
}

void AgenticSynthAudioProcessorEditor::paint(juce::Graphics& g) {
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
}

void AgenticSynthAudioProcessorEditor::resized() { placeholderLabel.setBounds(getLocalBounds()); }
