#include "ScopeView.h"
#include <cmath>

namespace
{
    // The trigger searches this far back (at least) for a crossing, so notes
    // down to ~33 Hz still lock even at the shortest window.
    constexpr double kMinSearchMs = 30.0;

    // Hysteresis: the signal must dip below -kArm before a rising crossing of 0
    // counts, so low-level noise around zero doesn't retrigger.
    constexpr float kArm = 0.01f;

    const juce::Colour kScopeBg    { 0xff141414 };
    const juce::Colour kScopeGrid  { 0xff2e2e2e };
    const juce::Colour kScopeTrace { 0xff7fd6a0 };
}

ScopeView::ScopeView (const ScopeBuffer& buffer)
    : buffer_ (buffer)
{
    samples_.resize ((size_t) ScopeBuffer::kSize);
    setOpaque (true);
    startTimerHz (60);
}

ScopeView::~ScopeView()
{
    stopTimer();
}

double ScopeView::findTrigger (int searchEnd) const noexcept
{
    // Forward scan with an arm/fire state; keep the LAST crossing found so the
    // display is as fresh as possible while still leaving a full window after it.
    double found = -1.0;
    bool armed = false;
    for (int i = 1; i <= searchEnd; ++i)
    {
        const float prev = samples_[(size_t) (i - 1)];
        const float cur  = samples_[(size_t) i];
        if (prev < -kArm)
            armed = true;
        if (armed && prev < 0.0f && cur >= 0.0f)
        {
            const float denom = cur - prev;   // > 0 here
            found = (double) (i - 1) + (double) (-prev / denom);
            armed = false;
        }
    }
    return found;
}

void ScopeView::timerCallback()
{
    const double sr = buffer_.getSampleRate();
    const int maxWindow = ScopeBuffer::kSize / 2;
    const int window = juce::jlimit (16, maxWindow, juce::roundToInt (sr * windowMs_ / 1000.0));
    const int search = triggerOn_
                     ? juce::jlimit (0, ScopeBuffer::kSize - window - 2,
                                     juce::jmax (window, juce::roundToInt (sr * kMinSearchMs / 1000.0)))
                     : 0;

    // Layout: [search region | window], newest sample last.  One extra sample
    // so the interpolated start still has `window` samples after it.
    const int total = search + window + 1;
    buffer_.readLatest (samples_.data(), total);

    double start = (double) (search);   // free-run: the newest full window
    if (triggerOn_)
    {
        const double t = findTrigger (search);
        if (t >= 0.0)
            start = t;
    }

    const auto r = getLocalBounds().toFloat().reduced (2.0f);
    const float midY  = r.getCentreY();
    const float halfH = r.getHeight() * 0.5f;
    const double pxPerSample = (double) r.getWidth() / (double) (window - 1);

    // Draw from the first whole sample at/after `start`, offset by the fraction.
    const int first = (int) std::ceil (start);
    const double offset = (double) first - start;   // 0..1 samples

    path_.clear();
    path_.preallocateSpace (3 * window + 8);
    for (int i = 0; i < window; ++i)
    {
        const int idx = first + i;
        if (idx >= total)
            break;
        const float x = r.getX() + (float) (((double) i + offset) * pxPerSample);
        if (x > r.getRight())
            break;
        const float y = midY - juce::jlimit (-1.0f, 1.0f, samples_[(size_t) idx]) * halfH;
        if (i == 0) path_.startNewSubPath (x, y);
        else        path_.lineTo (x, y);
    }

    repaint();
}

void ScopeView::paint (juce::Graphics& g)
{
    g.fillAll (kScopeBg);

    const auto r = getLocalBounds().toFloat().reduced (2.0f);
    g.setColour (kScopeGrid);
    g.drawHorizontalLine (juce::roundToInt (r.getCentreY()), r.getX(), r.getRight());
    for (int i = 1; i < 10; ++i)
        g.drawVerticalLine (juce::roundToInt (r.getX() + r.getWidth() * (float) i / 10.0f),
                            r.getY(), r.getBottom());

    g.setColour (kScopeTrace);
    g.strokePath (path_, juce::PathStrokeType (1.5f));
}
