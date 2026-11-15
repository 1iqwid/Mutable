#include "EnvelopeView.h"
#include "PluginProcessor.h"
#include <cmath>

namespace
{
    // Maximum stage times (must match the parameter ranges in PluginProcessor.cpp).
    constexpr float kMaxAttack  = 5.0f;
    constexpr float kMaxDecay   = 5.0f;
    constexpr float kMaxRelease = 10.0f;

    // Width shares (sum = 1).
    constexpr float kShareA = 0.25f, kShareD = 0.25f, kShareS = 0.20f, kShareR = 0.30f;

    const juce::Colour kBg    { 0xff141414 };
    const juce::Colour kGrid  { 0xff2e2e2e };
    const juce::Colour kCurve { 0xff8ab4f8 };
    const juce::Colour kText  { 0xffa0a0a0 };

    float stageWidth (float t, float tMax, float share, float w) noexcept
    {
        return share * w * std::sqrt (juce::jlimit (0.0f, 1.0f, t / tMax));
    }

    juce::String formatTime (float s)
    {
        return s < 1.0f ? juce::String (juce::roundToInt (s * 1000.0f)) + " ms"
                        : juce::String (s, 2) + " s";
    }
}

EnvelopeView::EnvelopeView (juce::AudioProcessorValueTreeState& apvts)
{
    using P = MutableAudioProcessor;
    attack_  = apvts.getRawParameterValue (P::paramAttack);
    decay_   = apvts.getRawParameterValue (P::paramDecay);
    sustain_ = apvts.getRawParameterValue (P::paramSustain);
    release_ = apvts.getRawParameterValue (P::paramRelease);
    shown_ = read();
    setOpaque (true);
    startTimerHz (30);
}

EnvelopeView::~EnvelopeView()
{
    stopTimer();
}

EnvelopeView::Values EnvelopeView::read() const noexcept
{
    auto get = [] (std::atomic<float>* p) { return p != nullptr ? p->load() : 0.0f; };
    return { get (attack_), get (decay_), get (sustain_), get (release_) };
}

void EnvelopeView::timerCallback()
{
    const auto v = read();
    if (v.a != shown_.a || v.d != shown_.d || v.s != shown_.s || v.r != shown_.r)
    {
        shown_ = v;
        repaint();
    }
}

void EnvelopeView::paint (juce::Graphics& g)
{
    g.fillAll (kBg);

    auto area = getLocalBounds().toFloat().reduced (6.0f);
    const auto labels = area.removeFromBottom (14.0f);
    area.removeFromTop (4.0f);

    const float w = area.getWidth();
    const float x0 = area.getX();
    const float top = area.getY();
    const float bottom = area.getBottom();
    const float h = area.getHeight();

    const float sustain = juce::jlimit (0.0f, 1.0f, shown_.s);
    const float xA = x0 + stageWidth (shown_.a, kMaxAttack, kShareA, w);
    const float xD = xA + stageWidth (shown_.d, kMaxDecay,  kShareD, w);
    const float xS = xD + kShareS * w;
    const float xR = xS + stageWidth (shown_.r, kMaxRelease, kShareR, w);
    const float yS = bottom - sustain * h;

    // Grid: 0 / 0.5 / 1 levels.
    g.setColour (kGrid);
    for (float lv : { 0.0f, 0.5f, 1.0f })
        g.drawHorizontalLine (juce::roundToInt (bottom - lv * h), x0, x0 + w);

    juce::Path curve;
    curve.startNewSubPath (x0, bottom);
    curve.lineTo (xA, top);
    curve.lineTo (xD, yS);
    curve.lineTo (xS, yS);
    curve.lineTo (xR, bottom);

    juce::Path fill (curve);
    fill.closeSubPath();
    g.setColour (kCurve.withAlpha (0.15f));
    g.fillPath (fill);

    g.setColour (kCurve);
    g.strokePath (curve, juce::PathStrokeType (2.0f));

    // Stage boundaries (note-off marker at the end of the sustain hold).
    g.setColour (kGrid.brighter (0.4f));
    for (float x : { xA, xD, xS })
        g.drawVerticalLine (juce::roundToInt (x), top, bottom);

    // Values along the bottom.
    g.setColour (kText);
    g.setFont (juce::Font (juce::FontOptions (11.0f)));
    const juce::String text = "A " + formatTime (shown_.a) + "   D " + formatTime (shown_.d)
                            + "   S " + juce::String (sustain, 2) + "   R " + formatTime (shown_.r);
    g.drawText (text, labels, juce::Justification::centredLeft);
}
