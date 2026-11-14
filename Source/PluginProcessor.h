#pragma once

#include <atomic>

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

    bool isBusesLayoutSupported (const BusesLayout&) const override;

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
    static const juce::String paramAttack;
    static const juce::String paramDecay;
    static const juce::String paramSustain;
    static const juce::String paramRelease;
    static const juce::String paramBendRange;
    static const juce::String paramVoices;
    static const juce::String paramMix;
    static const juce::String paramAuxLevel;
    static const juce::String paramCoarse;
    static const juce::String paramFine;
    static const juce::String paramLpgColour;
    static const juce::String paramLpgDecay;
    // ---- Task 4c-1: modulation matrix (envelope / velocity / mod wheel) ----
    // Bipolar -1..+1 mod amounts.  All default to 0.0 EXCEPT modWheelTimbre
    // which defaults to 0.5 so the existing (temporary) CC1->timbre routing is
    // preserved by the matrix.  The "mod" prefix lets the UI group them.
    static const juce::String paramModEnvHarmonics;
    static const juce::String paramModEnvTimbre;
    static const juce::String paramModEnvMorph;
    static const juce::String paramModEnvPitch;
    static const juce::String paramModVelHarmonics;
    static const juce::String paramModVelTimbre;
    static const juce::String paramModVelMorph;
    static const juce::String paramModVelPitch;
    static const juce::String paramModWheelHarmonics;
    static const juce::String paramModWheelTimbre;
    static const juce::String paramModWheelMorph;
    static const juce::String paramModWheelPitch;
    // ---- Task 4c-2a: LFO (shape, rate) and its four mod amounts -------------
    static const juce::String paramLfoShape;
    static const juce::String paramLfoRate;
    static const juce::String paramModLfoHarmonics;
    static const juce::String paramModLfoTimbre;
    static const juce::String paramModLfoMorph;
    static const juce::String paramModLfoPitch;

    //==========================================================================
    // Maximum number of voices — a compile-time constant (Phase 3d).  All
    // kMaxVoices voices are allocated once and never added/removed at runtime;
    // the user selects how many of them are active via the "voices" parameter
    // (1..kMaxVoices, default kMaxVoices).
    static constexpr int kMaxVoices = 8;

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
    juce::RangedAudioParameter* pAttack    = nullptr;
    juce::RangedAudioParameter* pDecay     = nullptr;
    juce::RangedAudioParameter* pSustain   = nullptr;
    juce::RangedAudioParameter* pRelease   = nullptr;
    juce::RangedAudioParameter* pBendRange = nullptr;
    juce::RangedAudioParameter* pVoices    = nullptr;
    juce::RangedAudioParameter* pMix       = nullptr;
    juce::RangedAudioParameter* pAuxLevel  = nullptr;
    juce::RangedAudioParameter* pCoarse    = nullptr;
    juce::RangedAudioParameter* pFine      = nullptr;
    juce::RangedAudioParameter* pLpgColour = nullptr;
    juce::RangedAudioParameter* pLpgDecay  = nullptr;
    // ---- Task 4c-1: modulation matrix amounts (bipolar -1..+1) -------------
    juce::RangedAudioParameter* pModEnvHarmonics   = nullptr;
    juce::RangedAudioParameter* pModEnvTimbre      = nullptr;
    juce::RangedAudioParameter* pModEnvMorph       = nullptr;
    juce::RangedAudioParameter* pModEnvPitch       = nullptr;
    juce::RangedAudioParameter* pModVelHarmonics   = nullptr;
    juce::RangedAudioParameter* pModVelTimbre      = nullptr;
    juce::RangedAudioParameter* pModVelMorph       = nullptr;
    juce::RangedAudioParameter* pModVelPitch       = nullptr;
    juce::RangedAudioParameter* pModWheelHarmonics = nullptr;
    juce::RangedAudioParameter* pModWheelTimbre    = nullptr;
    juce::RangedAudioParameter* pModWheelMorph     = nullptr;
    juce::RangedAudioParameter* pModWheelPitch     = nullptr;
    // ---- Task 4c-2a: LFO parameters -----------------------------------------
    juce::RangedAudioParameter* pLfoShape          = nullptr;
    juce::RangedAudioParameter* pLfoRate           = nullptr;
    juce::RangedAudioParameter* pModLfoHarmonics   = nullptr;
    juce::RangedAudioParameter* pModLfoTimbre      = nullptr;
    juce::RangedAudioParameter* pModLfoMorph       = nullptr;
    juce::RangedAudioParameter* pModLfoPitch       = nullptr;

    //==========================================================================
    // Shared active-voice count (Phase 3d).  Owned by the processor; every
    // PlaitsSynthVoice holds a pointer to it.  processBlock() writes the
    // selected voice count into this atomic once per block; each voice reads it
    // (lock-free) in canPlaySound() to gate allocation/stealing.
    std::atomic<int> activeVoiceCount_;

    //==========================================================================
    // Build a skewed NormalisableRange for a time parameter so the short end
    // has fine control.  skew ≈ 0.3 (lower = more skew toward the short end).
    static juce::NormalisableRange<float>
        makeSkewedRange (float minVal, float maxVal, float defaultVal,
                         float skew = 0.3f)
    {
        return juce::NormalisableRange<float> (minVal, maxVal,
                                               defaultVal, skew);
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MutableAudioProcessor)
};