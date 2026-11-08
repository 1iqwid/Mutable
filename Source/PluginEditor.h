#pragma once

#include "PluginProcessor.h"

//==============================================================================
/// The plugin editor. Phase 1: an empty window, nothing drawn.
class MutableAudioProcessorEditor final
    : public juce::AudioProcessorEditor
{
public:
    explicit MutableAudioProcessorEditor (MutableAudioProcessor&);
    ~MutableAudioProcessorEditor() override;

    //==============================================================================
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    MutableAudioProcessor& processorRef;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MutableAudioProcessorEditor)
};