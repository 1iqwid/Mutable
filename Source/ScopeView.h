#pragma once

#include <vector>
#include <juce_gui_basics/juce_gui_basics.h>

#include "ScopeBuffer.h"

//==============================================================================
/// Phase 5: oscilloscope view.  Reads the processor's ScopeBuffer on a 60 Hz
/// timer (UI thread) and draws the newest samples.
///   5a: free-running display.
///   5b: rising zero-crossing trigger (with hysteresis, sub-sample aligned) and a
///       variable time window (zoom).  Both are view settings, not parameters.
class ScopeView final : public juce::Component,
                        private juce::Timer
{
public:
    explicit ScopeView (const ScopeBuffer& buffer);
    ~ScopeView() override;

    void paint (juce::Graphics&) override;

    /// Time window shown across the full width, in milliseconds.
    void setWindowMs (double ms) noexcept { windowMs_ = ms; }
    void setTriggerEnabled (bool on) noexcept { triggerOn_ = on; }

    static constexpr double kMinWindowMs     = 1.0;
    static constexpr double kMaxWindowMs     = 100.0;
    static constexpr double kDefaultWindowMs = 20.0;

private:
    void timerCallback() override;

    /// Returns the (fractional) index of the newest rising zero crossing in
    /// samples_[0 .. searchEnd], or -1 if there is none.
    double findTrigger (int searchEnd) const noexcept;

    const ScopeBuffer& buffer_;
    std::vector<float> samples_;   // UI-thread scratch, sized once
    juce::Path path_;

    double windowMs_  = kDefaultWindowMs;
    bool   triggerOn_ = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ScopeView)
};
