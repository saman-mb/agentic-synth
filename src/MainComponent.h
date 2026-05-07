#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

//==============================================================================
class MainComponent final : public juce::Component {
public:
    MainComponent();
    ~MainComponent() override = default;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    juce::Label welcomeLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
