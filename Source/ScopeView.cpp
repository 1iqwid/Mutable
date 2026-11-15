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

    const juce::Colour kScopeBg   { 0xff141414 };
    const juce::Colour kScopeGrid { 0xff2e2e2e };
}

const juce::Colour ScopeView::kOutColour { 0xff7fd6a0 };   // green
const juce::Colour ScopeView::kAuxColour { 0xffe8a050 };   // amber

ScopeView::ScopeView (const ScopeBuffer& buffer)
    : buffer_ (buffer)
{
    out_.resize ((size_t) ScopeBuffer::kSize);
    aux_.resize ((size_t) ScopeBuffer::kSize);
    setOpaque (true);
    startTimerHz (60);
}

ScopeView::~ScopeView()
{
    stopTimer();
}

double ScopeView::findTrigger (const float* s, int searchEnd) noexcept
{
    // Forward scan with an arm/fire state; keep the LAST crossing found so the
    // display is as fresh as possible while still leaving a full window after it.
    double found = -1.0;
    bool armed = false;
    for (int i = 1; i <= searchEnd; ++i)
    {
        const float prev = s[i - 1];
        const float cur  = s[i];
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

void ScopeView::buildPath (juce::Path& path, const float* s, int first, double offset,
                           int window, int total) const
{
    const auto r = getLocalBounds().toFloat().reduced (2.0f);
    const float midY  = r.getCentreY();
    const float halfH = r.getHeight() * 0.5f;
    const double pxPerSample = (double) r.getWidth() / (double) (window - 1);

    path.clear();
    path.preallocateSpace (3 * window + 8);
    for (int i = 0; i < window; ++i)
    {
        const int idx = first + i;
        if (idx >= total)
            break;
        const float x = r.getX() + (float) (((double) i + offset) * pxPerSample);
        if (x > r.getRight())
            break;
        const float y = midY - juce::jlimit (-1.0f, 1.0f, s[idx]) * halfH;
        if (i == 0) path.startNewSubPath (x, y);
        else        path.lineTo (x, y);
    }
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
    buffer_.readLatest (out_.data(), aux_.data(), total);

    double start = (double) search;   // free-run: the newest full window
    if (triggerOn_)
    {
        double t = findTrigger (out_.data(), search);
        if (t < 0.0)
            t = findTrigger (aux_.data(), search);   // e.g. Out silent / DC
        if (t >= 0.0)
            start = t;
    }

    // Draw from the first whole sample at/after `start`, offset by the fraction.
    // Both traces use the same start so their relative phase is preserved.
    const int first = (int) std::ceil (start);
    const double offset = (double) first - start;   // 0..1 samples

    buildPath (outPath_, out_.data(), first, offset, window, total);
    buildPath (auxPath_, aux_.data(), first, offset, window, total);

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

    // Aux first (behind), Out on top.
    g.setColour (kAuxColour.withAlpha (0.85f));
    g.strokePath (auxPath_, juce::PathStrokeType (1.5f));
    g.setColour (kOutColour);
    g.strokePath (outPath_, juce::PathStrokeType (1.5f));

    // Legend, top-left.
    g.setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)));
    auto legend = getLocalBounds().reduced (6, 4).removeFromTop (14);
    g.setColour (kOutColour);
    g.drawText ("Out", legend.removeFromLeft (30), juce::Justification::centredLeft);
    g.setColour (kAuxColour);
    g.drawText ("Aux", legend.removeFromLeft (30), juce::Justification::centredLeft);
}
