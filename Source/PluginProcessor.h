#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PlaitsVoice.h"

//==============================================================================
/// The plugin processor.
///
/// Phase 2c: single Plaits voice (last-note priority), one MIDI note at a time.
/// Polyphony arrives in Phase 3 (JUCE synthesiser voice pool + ADSR).
class MutableAudioProcessor final
    : public juce::AudioProcessor
{
public:
    //==============================================================================
    MutableAudioProcessor();
    ~MutableAudioProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    //==============================================================================
    bool hasEditor() const override;
    juce::AudioProcessorEditor* createEditor() override;

    //==============================================================================
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;
    bool acceptsMidi() const override;
    bool producesMidi() const override;

    //==============================================================================
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    //==============================================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==============================================================================
    const juce::String getName() const override;

    //==========================================================================
    // (parameterChanged is not a JUCE 8 APVTS virtual — parameters are read
    //  directly via cached atomics in processBlock().)

private:
    //==============================================================================
    // Parameter IDs — stable strings, used for both the APVTS and state I/O.
    static const juce::String paramEngine;
    static const juce::String paramHarmonics;
    static const juce::String paramTimbre;
    static const juce::String paramMorph;
    static const juce::String paramOutGain;

    //==============================================================================
    // The single Plaits voice.  Owned by the processor; created in prepare()
    // and destroyed in releaseResources(), both on the message thread —
    // never on the audio thread.
    PlaitsVoice voice_;

    // AudioProcessorValueTreeState: owns all parameters and their state.
    // Declared after voice_ so its constructor (which installs the
    // ParameterListener) sees a fully-constructed voice_.
    juce::AudioProcessorValueTreeState apvts_;

    // Cached parameter pointers for audio-thread reads.
    // Populated once in the constructor (message thread).  APVTS::getParameter()
    // returns RangedAudioParameter*; we keep that pointer and cast to the
    // concrete type (AudioParameterInt / AudioParameterFloat) when reading.
    // No string lookups in the audio thread.
    juce::RangedAudioParameter* pEngine    = nullptr;
    juce::RangedAudioParameter* pHarmonics = nullptr;
    juce::RangedAudioParameter* pTimbre    = nullptr;
    juce::RangedAudioParameter* pMorph     = nullptr;
    juce::RangedAudioParameter* pOutGain   = nullptr;

    // Track the last engine index sent to the voice so we can skip redundant
    // setEngine() calls when the parameter hasn't changed between blocks.
    int lastEngineSent_ = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MutableAudioProcessor)
};