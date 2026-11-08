#include "PluginEditor.h"
#include <JuceHeader.h>

//==============================================================================
MutableAudioProcessorEditor::MutableAudioProcessorEditor (MutableAudioProcessor& p)
    : AudioProcessorEditor(&p),
      processorRef(p)
{
    setSize(512, 384);
}

MutableAudioProcessorEditor::~MutableAudioProcessorEditor()
{
}

//==============================================================================
void MutableAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll(juce::Colours::darkgrey);
    // Nothing to draw yet.
}

void MutableAudioProcessorEditor::resized()
{
}