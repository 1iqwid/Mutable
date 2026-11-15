#include "ScopeView.h"

namespace
{
    constexpr double kWindowMs = 20.0;   // fixed time window (zoom comes in 5b)

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

void ScopeView::timerCallback()
{
    const double sr = buffer_.getSampleRate();
    const int num = juce::jlimit (16, ScopeBuffer::kSize,
                                  juce::roundToInt (sr * kWindowMs / 1000.0));
    buffer_.readLatest (samples_.data(), num);

    const auto r = getLocalBounds().toFloat().reduced (2.0f);
    const float midY = r.getCentreY();
    const float halfH = r.getHeight() * 0.5f;

    path_.clear();
    path_.preallocateSpace (3 * num + 8);
    for (int i = 0; i < num; ++i)
    {
        const float x = r.getX() + r.getWidth() * (float) i / (float) (num - 1);
        const float y = midY - juce::jlimit (-1.0f, 1.0f, samples_[(size_t) i]) * halfH;
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
