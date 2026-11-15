#pragma once

#include <atomic>
#include <juce_audio_processors/juce_audio_processors.h>

#include "VoiceDisplayState.h"

//==============================================================================
/// Phase 5d/5e: ADSR display.  Draws the envelope shape from the APVTS attack /
/// decay / sustain / release values (read via getRawParameterValue atomics on
/// the UI thread), so it is visible with no notes held, plus one dot per
/// sounding voice from the processor's VoiceDisplayState slots (lock-free).
///
/// juce::ADSR is linear per stage, so each stage is a straight segment.  Time
/// axis: each stage has a fixed maximum share of the width and is scaled by
/// sqrt(time / maxTime), so 1 ms and 10 s are both readable and the scale does
/// not jump when another stage changes.  Sustain gets a fixed-width hold.
///
/// Dots: x = stage start + progress through the stage; y = the voice's actual
/// envelope level (so a release from mid-attack sits below the drawn curve).
/// Held notes in sustain sit in the middle of the hold.
class EnvelopeView final : public juce::Component,
                           private juce::Timer
{
public:
    EnvelopeView (juce::AudioProcessorValueTreeState& apvts,
                  const VoiceDisplayState* voices, int numVoices);
    ~EnvelopeView() override;

    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;

    struct Values { float a = 0, d = 0, s = 0, r = 0; };
    Values read() const noexcept;

    /// Segment x positions and level scale for the current bounds and values.
    struct Geometry { float x0, xA, xD, xS, xR, top, bottom, h, w; };
    Geometry geometry() const noexcept;

    std::atomic<float>* attack_  = nullptr;
    std::atomic<float>* decay_   = nullptr;
    std::atomic<float>* sustain_ = nullptr;
    std::atomic<float>* release_ = nullptr;
    Values shown_;

    const VoiceDisplayState* voices_ = nullptr;
    int numVoices_ = 0;
    bool anyActiveLastFrame_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EnvelopeView)
};
