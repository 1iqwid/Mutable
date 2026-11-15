#pragma once

#include <atomic>
#include <juce_audio_processors/juce_audio_processors.h>

//==============================================================================
/// Phase 5d: ADSR display.  Draws the envelope shape from the APVTS attack /
/// decay / sustain / release values (read via getRawParameterValue atomics on
/// the UI thread), so it is visible with no notes held.  Repaints only when a
/// value changes.  Per-voice position dots come in 5e.
///
/// juce::ADSR is linear per stage, so each stage is a straight segment.  Time
/// axis: each stage has a fixed maximum share of the width and is scaled by
/// sqrt(time / maxTime), so 1 ms and 10 s are both readable and the scale does
/// not jump when another stage changes.  Sustain gets a fixed-width hold.
class EnvelopeView final : public juce::Component,
                           private juce::Timer
{
public:
    explicit EnvelopeView (juce::AudioProcessorValueTreeState& apvts);
    ~EnvelopeView() override;

    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;

    struct Values { float a = 0, d = 0, s = 0, r = 0; };
    Values read() const noexcept;

    std::atomic<float>* attack_  = nullptr;
    std::atomic<float>* decay_   = nullptr;
    std::atomic<float>* sustain_ = nullptr;
    std::atomic<float>* release_ = nullptr;
    Values shown_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EnvelopeView)
};
