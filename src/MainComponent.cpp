#include "MainComponent.h"

//==============================================================================
MainComponent::MainComponent()
{
    addAndMakeVisible (welcomeLabel);
    welcomeLabel.setText ("Agentic Synth — ready.",
                          juce::dontSendNotification);
    welcomeLabel.setFont (juce::Font (18.0f, juce::Font::bold));
    welcomeLabel.setJustificationType (juce::Justification::centred);

    setSize (600, 400);
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));
}

void MainComponent::resized()
{
    welcomeLabel.setBounds (getLocalBounds());
}
