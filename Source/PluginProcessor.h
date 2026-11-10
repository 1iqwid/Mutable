#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PlaitsSynthVoice.h"

//==============================================================================
/// The plugin processor.
///
/// Phase 3a: 8-voice pool via juce::Synthesiser, each voice owning a
/// PlaitsVoice + ADSR.  Voice count is a compile-time constant (8) for now.
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
    //==========================================================================
    // Parameter IDs — stable strings, used for both the APVTS and state I/O.
    static const juce::String paramEngine;
    static const juce::String paramHarmonics;
    static const juce::String paramTimbre;
    static const juce::String paramMorph;
    static const juce::String paramOutGain;

    //==========================================================================
    // Number of voices — fixed constant for now (Phase 3a).
    static constexpr int kNumVoices = 8;

    //==========================================================================
    // The synthesiser owns all voices and the sound.  Voices are heap-
    // allocated in the constructor (message thread) and deleted by the
    // synthesiser's destructor.
    juce::Synthesiser synth_;

    // AudioProcessorValueTreeState: owns all parameters and their state.
    juce::AudioProcessorValueTreeState apvts_;

    // Cached parameter pointers for audio-thread reads.
    // Populated once in the constructor (message thread).
    juce::RangedAudioParameter* pEngine    = nullptr;
    juce::RangedAudioParameter* pHarmonics = nullptr;
    juce::RangedAudioParameter* pTimbre    = nullptr;
    juce::RangedAudioParameter* pMorph     = nullptr;
    juce::RangedAudioParameter* pOutGain   = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MutableAudioProcessor)
};