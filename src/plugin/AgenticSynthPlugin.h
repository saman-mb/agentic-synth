#pragma once

#include "agent/AgentBridge.h"
#include "engine/ADSREnvelope.h"
#include "engine/MidiHandler.h"
#include "engine/PatchStruct.h"
#include "engine/VoiceManager.h"

#include <juce_audio_processors/juce_audio_processors.h>

//==============================================================================
class AgenticSynthPlugin final : public juce::AudioProcessor {
public:
    AgenticSynthPlugin();
    ~AgenticSynthPlugin() override;

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

    // Phase 4: editor needs access to the AgentBridge so the WebView native
    // bridge can submit prompts, record feedback, push knob tweaks, etc.
    agentic_synth::agent::AgentBridge& agentBridge() noexcept { return agentBridge_; }

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

private:
    void applyParameters() noexcept;
    void applyPatch(const agentic_synth::PatchStruct& patch) noexcept;

    agentic_synth::engine::VoiceManager voiceManager_;
    agentic_synth::agent::AgentBridge agentBridge_;
    agentic_synth::engine::MidiHandler midiHandler_;
    juce::AudioProcessorValueTreeState apvts_;

    // Thread-safe FIFO for non-audio-thread MIDI producers (audition keyboard,
    // future remote control). processBlock drains into the host's MidiBuffer
    // under audio-thread exclusivity. CriticalSection-protected MidiBuffer
    // chosen over juce::MidiMessageCollector to avoid linking
    // juce_audio_devices for one tiny FIFO.
    juce::MidiBuffer auditionPending_;
    juce::CriticalSection auditionMutex_;

    std::atomic<float>* masterGainParam_ = nullptr;
    std::atomic<float>* filterCutoffParam_ = nullptr;
    std::atomic<float>* filterResParam_ = nullptr;
    std::atomic<float>* ampAttackParam_ = nullptr;
    std::atomic<float>* ampDecayParam_ = nullptr;
    std::atomic<float>* ampSustainParam_ = nullptr;
    std::atomic<float>* ampReleaseParam_ = nullptr;
    std::atomic<float>* portamentoParam_ = nullptr;

    // Shadow values for dirty-flag skipping in applyParameters().
    float lastCutoff_{-1.0f};
    float lastRes_{-1.0f};
    float lastAttack_{-1.0f};
    float lastDecay_{-1.0f};
    float lastSustain_{-1.0f};
    float lastRelease_{-1.0f};
    float lastPortamento_{-1.0f};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AgenticSynthPlugin)
};
