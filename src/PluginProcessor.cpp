#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
AgenticSynthAudioProcessor::AgenticSynthAudioProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
}

//==============================================================================
void AgenticSynthAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused (sampleRate, samplesPerBlock);
}

void AgenticSynthAudioProcessor::releaseResources()
{
}

void AgenticSynthAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ignoreUnused (midiMessages);

    // Placeholder: silence output
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        buffer.clear (channel, 0, buffer.getNumSamples());
}

//==============================================================================
juce::AudioProcessorEditor* AgenticSynthAudioProcessor::createEditor()
{
    return new AgenticSynthAudioProcessorEditor (*this);
}

bool AgenticSynthAudioProcessor::hasEditor() const
{
    return true;
}

//==============================================================================
const juce::String AgenticSynthAudioProcessor::getName() const
{
    return "Agentic Synth";
}

bool AgenticSynthAudioProcessor::acceptsMidi() const   { return true;  }
bool AgenticSynthAudioProcessor::producesMidi() const  { return false; }
bool AgenticSynthAudioProcessor::isMidiEffect() const  { return false; }
double AgenticSynthAudioProcessor::getTailLengthSeconds() const { return 0.0; }

//==============================================================================
int AgenticSynthAudioProcessor::getNumPrograms()
{
    return 1;
}

int AgenticSynthAudioProcessor::getCurrentProgram()
{
    return 0;
}

void AgenticSynthAudioProcessor::setCurrentProgram (int index)
{
    juce::ignoreUnused (index);
}

const juce::String AgenticSynthAudioProcessor::getProgramName (int index)
{
    juce::ignoreUnused (index);
    return {};
}

void AgenticSynthAudioProcessor::changeProgramName (int index, const juce::String& newName)
{
    juce::ignoreUnused (index, newName);
}

//==============================================================================
void AgenticSynthAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::ignoreUnused (destData);
}

void AgenticSynthAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    juce::ignoreUnused (data, sizeInBytes);
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AgenticSynthAudioProcessor();
}
