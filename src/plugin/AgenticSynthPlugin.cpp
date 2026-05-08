#include "plugin/AgenticSynthPlugin.h"

#include "plugin/PluginEditor.h"

using APVTS = juce::AudioProcessorValueTreeState;

//==============================================================================
APVTS::ParameterLayout AgenticSynthPlugin::createParameterLayout() {
    APVTS::ParameterLayout layout;

    layout.add(std::make_unique<juce::AudioParameterFloat>("masterGain", "Master Gain",
                                                           juce::NormalisableRange<float>(0.0f, 1.0f), 0.8f));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "filterCutoff", "Filter Cutoff", juce::NormalisableRange<float>(20.0f, 20000.0f, 0.0f, 0.25f), 5000.0f));

    layout.add(std::make_unique<juce::AudioParameterFloat>("filterResonance", "Filter Resonance",
                                                           juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "ampAttack", "Amp Attack", juce::NormalisableRange<float>(0.001f, 10.0f, 0.0f, 0.3f), 0.005f));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "ampDecay", "Amp Decay", juce::NormalisableRange<float>(0.001f, 10.0f, 0.0f, 0.3f), 0.1f));

    layout.add(std::make_unique<juce::AudioParameterFloat>("ampSustain", "Amp Sustain",
                                                           juce::NormalisableRange<float>(0.0f, 1.0f), 1.0f));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "ampRelease", "Amp Release", juce::NormalisableRange<float>(0.001f, 20.0f, 0.0f, 0.3f), 0.1f));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "portamento", "Portamento", juce::NormalisableRange<float>(0.0f, 5.0f, 0.0f, 0.5f), 0.0f));

    return layout;
}

//==============================================================================
AgenticSynthPlugin::AgenticSynthPlugin()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts_(*this, nullptr, "Parameters", createParameterLayout()) {
    masterGainParam_ = apvts_.getRawParameterValue("masterGain");
    filterCutoffParam_ = apvts_.getRawParameterValue("filterCutoff");
    filterResParam_ = apvts_.getRawParameterValue("filterResonance");
    ampAttackParam_ = apvts_.getRawParameterValue("ampAttack");
    ampDecayParam_ = apvts_.getRawParameterValue("ampDecay");
    ampSustainParam_ = apvts_.getRawParameterValue("ampSustain");
    ampReleaseParam_ = apvts_.getRawParameterValue("ampRelease");
    portamentoParam_ = apvts_.getRawParameterValue("portamento");
}

//==============================================================================
void AgenticSynthPlugin::prepareToPlay(double sampleRate, int /*samplesPerBlock*/) {
    voiceManager_.prepare(sampleRate);
    applyParameters();
}

void AgenticSynthPlugin::releaseResources() {}

void AgenticSynthPlugin::applyParameters() noexcept {
    voiceManager_.setFilterCutoff(filterCutoffParam_->load());
    voiceManager_.setFilterResonance(filterResParam_->load());

    agentic_synth::engine::ADSREnvelope::Params env;
    env.attackSeconds = ampAttackParam_->load();
    env.decaySeconds = ampDecayParam_->load();
    env.sustainLevel = ampSustainParam_->load();
    env.releaseSeconds = ampReleaseParam_->load();
    voiceManager_.setAmpEnvelope(env);

    voiceManager_.setPortamento(portamentoParam_->load());
}

void AgenticSynthPlugin::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) {
    juce::ScopedNoDenormals noDenormals;

    applyParameters();

    for (const auto metadata : midiMessages) {
        const auto msg = metadata.getMessage();
        if (msg.isNoteOn())
            voiceManager_.noteOn(msg.getNoteNumber(), msg.getFloatVelocity());
        else if (msg.isNoteOff())
            voiceManager_.noteOff(msg.getNoteNumber());
    }

    const int numSamples = buffer.getNumSamples();
    buffer.clear();

    voiceManager_.renderBlock(buffer.getWritePointer(0), numSamples);

    const float gain = masterGainParam_->load();
    buffer.applyGain(0, 0, numSamples, gain);

    if (buffer.getNumChannels() > 1)
        buffer.copyFrom(1, 0, buffer, 0, 0, numSamples);
}

//==============================================================================
juce::AudioProcessorEditor* AgenticSynthPlugin::createEditor() { return new AgenticSynthPluginEditor(*this); }

bool AgenticSynthPlugin::hasEditor() const { return true; }

//==============================================================================
const juce::String AgenticSynthPlugin::getName() const { return "Agentic Synth"; }

bool AgenticSynthPlugin::acceptsMidi() const { return true; }
bool AgenticSynthPlugin::producesMidi() const { return false; }
bool AgenticSynthPlugin::isMidiEffect() const { return false; }
double AgenticSynthPlugin::getTailLengthSeconds() const { return 0.0; }

//==============================================================================
int AgenticSynthPlugin::getNumPrograms() { return 1; }
int AgenticSynthPlugin::getCurrentProgram() { return 0; }
void AgenticSynthPlugin::setCurrentProgram(int) {}
const juce::String AgenticSynthPlugin::getProgramName(int) { return {}; }
void AgenticSynthPlugin::changeProgramName(int, const juce::String&) {}

//==============================================================================
void AgenticSynthPlugin::getStateInformation(juce::MemoryBlock& destData) {
    const auto state = apvts_.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void AgenticSynthPlugin::setStateInformation(const void* data, int sizeInBytes) {
    std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, sizeInBytes));
    if (xml != nullptr && xml->hasTagName(apvts_.state.getType()))
        apvts_.replaceState(juce::ValueTree::fromXml(*xml));
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new AgenticSynthPlugin(); }
