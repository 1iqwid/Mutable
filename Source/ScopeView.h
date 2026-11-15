#pragma once

#include <vector>
#include <juce_gui_basics/juce_gui_basics.h>

#include "ScopeBuffer.h"

//==============================================================================
/// Phase 5a: oscilloscope view.  Reads the processor's ScopeBuffer on a 60 Hz
/// timer (UI thread) and draws the newest samples.  Free-running for now; the
/// zero-crossing trigger and zoom come in 5b.
class ScopeView final : public juce::Component,
                        private juce::Timer
{
public:
    explicit ScopeView (const ScopeBuffer& buffer);
    ~ScopeView() override;

    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;

    const ScopeBuffer& buffer_;
    std::vector<float> samples_;   // UI-thread scratch, sized once
    juce::Path path_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ScopeView)
};
