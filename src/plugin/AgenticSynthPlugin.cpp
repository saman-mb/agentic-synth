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
      midiHandler_(voiceManager_), apvts_(*this, nullptr, "Parameters", createParameterLayout()) {
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
    const float cutoff = filterCutoffParam_->load();
    const float res = filterResParam_->load();
    const float attack = ampAttackParam_->load();
    const float decay = ampDecayParam_->load();
    const float sustain = ampSustainParam_->load();
    const float release = ampReleaseParam_->load();
    const float portamento = portamentoParam_->load();

    if (cutoff == lastCutoff_ && res == lastRes_ && attack == lastAttack_ && decay == lastDecay_ &&
        sustain == lastSustain_ && release == lastRelease_ && portamento == lastPortamento_)
        return;

    lastCutoff_ = cutoff;
    lastRes_ = res;
    lastAttack_ = attack;
    lastDecay_ = decay;
    lastSustain_ = sustain;
    lastRelease_ = release;
    lastPortamento_ = portamento;

    voiceManager_.setFilterCutoff(cutoff);
    voiceManager_.setFilterResonance(res);

    agentic_synth::engine::ADSREnvelope::Params env;
    env.attackSeconds = attack;
    env.decaySeconds = decay;
    env.sustainLevel = sustain;
    env.releaseSeconds = release;
    voiceManager_.setAmpEnvelope(env);

    voiceManager_.setPortamento(portamento);
}

void AgenticSynthPlugin::applyPatch(const agentic_synth::PatchStruct& patch) noexcept {
    voiceManager_.setFilterCutoff(patch.filter.cutoff_hz);
    voiceManager_.setFilterResonance(patch.filter.resonance);

    agentic_synth::engine::ADSREnvelope::Params env;
    env.attackSeconds = patch.amp_env.attack_s;
    env.decaySeconds = patch.amp_env.decay_s;
    env.sustainLevel = patch.amp_env.sustain;
    env.releaseSeconds = patch.amp_env.release_s;
    voiceManager_.setAmpEnvelope(env);

    voiceManager_.setPortamento(patch.portamento_s);
}

void AgenticSynthPlugin::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) {
    juce::ScopedNoDenormals noDenormals;

    // Phase 4: stamp the RT thread ID so AgentBridge::dispatch()'s tripwire
    // catches any future code that tries to emit subscriber callbacks from
    // the audio callback (callAsync allocates → never legal in RT).
    agentBridge_.markAudioThread();

    applyParameters();

    // Sync DAW transport tempo to all voice LFOs.
    if (auto* ph = getPlayHead()) {
        if (const auto pos = ph->getPosition())
            if (const auto bpm = pos->getBpm())
                voiceManager_.setHostTempo(*bpm);
    }

    // Apply any AI-generated patch from the AgentBridge pipeline.
    if (const auto patch = agentBridge_.pollPatch())
        applyPatch(*patch);

    // Route all MIDI through MidiHandler (note on/off + CC mapping).
    for (const auto metadata : midiMessages) {
        const auto& msg = metadata.getMessage();
        const auto* raw = msg.getRawData();
        const int sz = msg.getRawDataSize();
        agentic_synth::engine::RawMidiMsg rmsg;
        rmsg.status = raw[0];
        rmsg.data1 = sz > 1 ? raw[1] : 0;
        rmsg.data2 = sz > 2 ? raw[2] : 0;
        midiHandler_.process(rmsg);

        // Notify AgentBridge of CC movements for AI context.
        if (msg.isController())
            agentBridge_.onMidiCC(msg.getControllerNumber(), msg.getControllerValue());
    }

    const int numSamples = buffer.getNumSamples();
    buffer.clear();

    const int numChannels = buffer.getNumChannels();
    if (numChannels >= 2) {
        voiceManager_.renderBlock(buffer.getWritePointer(0), buffer.getWritePointer(1), numSamples);
    } else {
        voiceManager_.renderBlock(buffer.getWritePointer(0), numSamples);
    }

    const float gain = masterGainParam_->load();
    for (int ch = 0; ch < numChannels; ++ch)
        buffer.applyGain(ch, 0, numSamples, gain);
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
