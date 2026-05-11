#include "plugin/PluginEditor.h"

namespace {
void initKnob(juce::Slider& slider, juce::Label& label, const char* text) {
    slider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 18);
    label.setText(text, juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centred);
    label.setFont(juce::Font(12.0f));
}
} // namespace

//==============================================================================
AgenticSynthPluginEditor::AgenticSynthPluginEditor(AgenticSynthPlugin& p)
    : AudioProcessorEditor(p), processor_(p), gainAttach_(p.getAPVTS(), "masterGain", gainSlider_),
      cutoffAttach_(p.getAPVTS(), "filterCutoff", cutoffSlider_),
      resonanceAttach_(p.getAPVTS(), "filterResonance", resonanceSlider_),
      attackAttach_(p.getAPVTS(), "ampAttack", attackSlider_), decayAttach_(p.getAPVTS(), "ampDecay", decaySlider_),
      sustainAttach_(p.getAPVTS(), "ampSustain", sustainSlider_),
      releaseAttach_(p.getAPVTS(), "ampRelease", releaseSlider_),
      portamentoAttach_(p.getAPVTS(), "portamento", portamentoSlider_) {
    initKnob(gainSlider_, gainLabel_, "Gain");
    initKnob(cutoffSlider_, cutoffLabel_, "Cutoff");
    initKnob(resonanceSlider_, resonanceLabel_, "Resonance");
    initKnob(attackSlider_, attackLabel_, "Attack");
    initKnob(decaySlider_, decayLabel_, "Decay");
    initKnob(sustainSlider_, sustainLabel_, "Sustain");
    initKnob(releaseSlider_, releaseLabel_, "Release");
    initKnob(portamentoSlider_, portamentoLabel_, "Portamento");

    addAndMakeVisible(gainSlider_);
    addAndMakeVisible(cutoffSlider_);
    addAndMakeVisible(resonanceSlider_);
    addAndMakeVisible(attackSlider_);
    addAndMakeVisible(decaySlider_);
    addAndMakeVisible(sustainSlider_);
    addAndMakeVisible(releaseSlider_);
    addAndMakeVisible(portamentoSlider_);
    addAndMakeVisible(gainLabel_);
    addAndMakeVisible(cutoffLabel_);
    addAndMakeVisible(resonanceLabel_);
    addAndMakeVisible(attackLabel_);
    addAndMakeVisible(decayLabel_);
    addAndMakeVisible(sustainLabel_);
    addAndMakeVisible(releaseLabel_);
    addAndMakeVisible(portamentoLabel_);

    setSize(640, 300);
}

void AgenticSynthPluginEditor::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour(0xff1e1e2e));
    g.setColour(juce::Colour(0xffcba6f7));
    g.setFont(juce::Font(16.0f, juce::Font::bold));
    g.drawText("Agentic Synth", getLocalBounds().removeFromTop(30), juce::Justification::centred);
}

void AgenticSynthPluginEditor::resized() {
    auto area = getLocalBounds().reduced(8);
    area.removeFromTop(30);
    const int numKnobs = 8;
    const int knobW = area.getWidth() / numKnobs;
    const int labelH = 18;

    auto layoutKnob = [&](juce::Slider& s, juce::Label& l) {
        auto col = area.removeFromLeft(knobW);
        l.setBounds(col.removeFromBottom(labelH));
        s.setBounds(col);
    };

    layoutKnob(gainSlider_, gainLabel_);
    layoutKnob(cutoffSlider_, cutoffLabel_);
    layoutKnob(resonanceSlider_, resonanceLabel_);
    layoutKnob(attackSlider_, attackLabel_);
    layoutKnob(decaySlider_, decayLabel_);
    layoutKnob(sustainSlider_, sustainLabel_);
    layoutKnob(releaseSlider_, releaseLabel_);
    layoutKnob(portamentoSlider_, portamentoLabel_);
}
