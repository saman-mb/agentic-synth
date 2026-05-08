#pragma once

#include "engine/ADSREnvelope.h"
#include "engine/VoiceManager.h"

#include <juce_audio_processors/juce_audio_processors.h>

//==============================================================================
class AgenticSynthPlugin final : public juce::AudioProcessor {
public:
    AgenticSynthPlugin();
    ~AgenticSynthPlugin() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int index, const juce::String& newName) override;

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getAPVTS() noexcept { return apvts_; }

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

private:
    void applyParameters() noexcept;

    agentic_synth::engine::VoiceManager voiceManager_;
    juce::AudioProcessorValueTreeState apvts_;

    std::atomic<float>* masterGainParam_ = nullptr;
    std::atomic<float>* filterCutoffParam_ = nullptr;
    std::atomic<float>* filterResParam_ = nullptr;
    std::atomic<float>* ampAttackParam_ = nullptr;
    std::atomic<float>* ampDecayParam_ = nullptr;
    std::atomic<float>* ampSustainParam_ = nullptr;
    std::atomic<float>* ampReleaseParam_ = nullptr;
    std::atomic<float>* portamentoParam_ = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AgenticSynthPlugin)
};
