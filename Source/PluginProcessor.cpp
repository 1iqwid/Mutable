#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <JuceHeader.h>

//==============================================================================
MutableAudioProcessor::MutableAudioProcessor()
    : AudioProcessor(BusesProperties()
                     .withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
}

MutableAudioProcessor::~MutableAudioProcessor()
{
}

//==============================================================================
void MutableAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    // Nothing to prepare yet — no DSP.
}

void MutableAudioProcessor::releaseResources()
{
    // Nothing to release yet.
}

bool MutableAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return true;
}

void MutableAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear(); // Synth with no inputs — the buffer arrives with garbage, so zero it out.
}

//==============================================================================
bool MutableAudioProcessor::hasEditor() const
{
    return true;
}

juce::AudioProcessorEditor* MutableAudioProcessor::createEditor()
{
    return new MutableAudioProcessorEditor(*this);
}

//==============================================================================
bool MutableAudioProcessor::isMidiEffect() const
{
    return false;
}

double MutableAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

//==============================================================================
bool MutableAudioProcessor::acceptsMidi() const
{
    return true; // This is a synth — it wants to receive MIDI notes.
}

bool MutableAudioProcessor::producesMidi() const
{
    return false;
}

//==============================================================================
int MutableAudioProcessor::getNumPrograms()
{
    return 1; // No per-program state yet.
}

int MutableAudioProcessor::getCurrentProgram()
{
    return 0;
}

void MutableAudioProcessor::setCurrentProgram (int)
{
    // No per-program state yet.
}

const juce::String MutableAudioProcessor::getProgramName (int)
{
    return {};
}

void MutableAudioProcessor::changeProgramName (int, const juce::String&)
{
    // No per-program state yet.
}

//==============================================================================
void MutableAudioProcessor::getStateInformation (juce::MemoryBlock&)
{
    // No state to save yet.
}

void MutableAudioProcessor::setStateInformation (const void*, int)
{
    // No state to load yet.
}

//==============================================================================
const juce::String MutableAudioProcessor::getName() const
{
    return "Mutable";
}

//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MutableAudioProcessor();
}