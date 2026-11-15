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

    constexpr float kLabelH = 14.0f;
    constexpr float kDotR   = 4.0f;

    const juce::Colour kBg    { 0xff141414 };
    const juce::Colour kGrid  { 0xff2e2e2e };
    const juce::Colour kCurve { 0xff8ab4f8 };
    const juce::Colour kText  { 0xffa0a0a0 };
    const juce::Colour kDot   { 0xfff2f2f2 };

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

EnvelopeView::EnvelopeView (juce::AudioProcessorValueTreeState& apvts,
                            const VoiceDisplayState* voices, int numVoices)
    : voices_ (voices), numVoices_ (numVoices)
{
    using P = MutableAudioProcessor;
    attack_  = apvts.getRawParameterValue (P::paramAttack);
    decay_   = apvts.getRawParameterValue (P::paramDecay);
    sustain_ = apvts.getRawParameterValue (P::paramSustain);
    release_ = apvts.getRawParameterValue (P::paramRelease);
    shown_ = read();
    setOpaque (true);
    startTimerHz (60);
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

EnvelopeView::Geometry EnvelopeView::geometry() const noexcept
{
    auto area = getLocalBounds().toFloat().reduced (6.0f);
    area.removeFromBottom (kLabelH);
    area.removeFromTop (kDotR);

    Geometry g {};
    g.w      = area.getWidth();
    g.h      = area.getHeight();
    g.x0     = area.getX();
    g.top    = area.getY();
    g.bottom = area.getBottom();
    g.xA = g.x0 + stageWidth (shown_.a, kMaxAttack, kShareA, g.w);
    g.xD = g.xA + stageWidth (shown_.d, kMaxDecay,  kShareD, g.w);
    g.xS = g.xD + kShareS * g.w;
    g.xR = g.xS + stageWidth (shown_.r, kMaxRelease, kShareR, g.w);
    return g;
}

void EnvelopeView::timerCallback()
{
    bool dirty = false;

    const auto v = read();
    if (v.a != shown_.a || v.d != shown_.d || v.s != shown_.s || v.r != shown_.r)
    {
        shown_ = v;
        dirty = true;
    }

    // Repaint every frame while any voice is sounding, plus one frame after
    // the last one stops (to clear its dot).
    bool anyActive = false;
    for (int i = 0; i < numVoices_ && voices_ != nullptr; ++i)
        if (voices_[i].stage.load (std::memory_order_relaxed) != VoiceDisplayState::kIdle)
            anyActive = true;

    if (anyActive || anyActiveLastFrame_)
        dirty = true;
    anyActiveLastFrame_ = anyActive;

    if (dirty)
        repaint();
}

void EnvelopeView::paint (juce::Graphics& g)
{
    g.fillAll (kBg);

    const auto geo = geometry();
    const float sustain = juce::jlimit (0.0f, 1.0f, shown_.s);
    const float yS = geo.bottom - sustain * geo.h;

    // Grid: 0 / 0.5 / 1 levels.
    g.setColour (kGrid);
    for (float lv : { 0.0f, 0.5f, 1.0f })
        g.drawHorizontalLine (juce::roundToInt (geo.bottom - lv * geo.h), geo.x0, geo.x0 + geo.w);

    juce::Path curve;
    curve.startNewSubPath (geo.x0, geo.bottom);
    curve.lineTo (geo.xA, geo.top);
    curve.lineTo (geo.xD, yS);
    curve.lineTo (geo.xS, yS);
    curve.lineTo (geo.xR, geo.bottom);

    juce::Path fill (curve);
    fill.closeSubPath();
    g.setColour (kCurve.withAlpha (0.15f));
    g.fillPath (fill);

    g.setColour (kCurve);
    g.strokePath (curve, juce::PathStrokeType (2.0f));

    // Stage boundaries (note-off marker at the end of the sustain hold).
    g.setColour (kGrid.brighter (0.4f));
    for (float x : { geo.xA, geo.xD, geo.xS })
        g.drawVerticalLine (juce::roundToInt (x), geo.top, geo.bottom);

    // Per-voice dots (5e).
    for (int i = 0; i < numVoices_ && voices_ != nullptr; ++i)
    {
        const auto& vs = voices_[i];
        const int stage = vs.stage.load (std::memory_order_relaxed);
        if (stage == VoiceDisplayState::kIdle)
            continue;

        const float lv = juce::jlimit (0.0f, 1.0f, vs.level.load (std::memory_order_relaxed));
        const float pr = juce::jlimit (0.0f, 1.0f, vs.progress.load (std::memory_order_relaxed));

        float x = geo.x0;
        switch (stage)
        {
            case VoiceDisplayState::kAttack:  x = geo.x0 + pr * (geo.xA - geo.x0); break;
            case VoiceDisplayState::kDecay:   x = geo.xA + pr * (geo.xD - geo.xA); break;
            case VoiceDisplayState::kSustain: x = 0.5f * (geo.xD + geo.xS);        break;
            case VoiceDisplayState::kRelease: x = geo.xS + pr * (geo.xR - geo.xS); break;
            default: break;
        }
        const float y = geo.bottom - lv * geo.h;

        // Ghost release line: juce::ADSR releases linearly from the level at
        // note-off to 0 over the release time (whatever that start level is),
        // so this voice's true path runs from (xS, start) to (xR, 0) and the
        // dot rides it.  Drawn only when it differs from the main curve.
        if (stage == VoiceDisplayState::kRelease)
        {
            const float start = juce::jlimit (0.0f, 1.0f,
                                              vs.releaseStart.load (std::memory_order_relaxed));
            if (std::abs (start - sustain) > 0.005f)
            {
                const float yStart = geo.bottom - start * geo.h;
                g.setColour (kDot.withAlpha (0.35f));
                g.drawLine (geo.xS, yStart, geo.xR, geo.bottom, 1.0f);

                // Lead-in: from where the key was let go on the attack / decay
                // slope, level, across to the start of the ghost release line.
                const int   from   = vs.releaseFromStage.load (std::memory_order_relaxed);
                const float fromPr = juce::jlimit (0.0f, 1.0f,
                                                   vs.releaseFromProgress.load (std::memory_order_relaxed));
                float xFrom = -1.0f;
                if (from == VoiceDisplayState::kAttack)
                    xFrom = geo.x0 + fromPr * (geo.xA - geo.x0);
                else if (from == VoiceDisplayState::kDecay)
                    xFrom = geo.xA + fromPr * (geo.xD - geo.xA);

                if (xFrom >= 0.0f && xFrom < geo.xS)
                    g.drawLine (xFrom, yStart, geo.xS, yStart, 1.0f);
            }
        }

        g.setColour (kDot);
        g.fillEllipse (x - kDotR, y - kDotR, 2.0f * kDotR, 2.0f * kDotR);
        g.setColour (kCurve);
        g.drawEllipse (x - kDotR, y - kDotR, 2.0f * kDotR, 2.0f * kDotR, 1.0f);
    }

    // Values along the bottom.
    auto labels = getLocalBounds().toFloat().reduced (6.0f).removeFromBottom (kLabelH);
    g.setColour (kText);
    g.setFont (juce::Font (juce::FontOptions (11.0f)));
    const juce::String text = "A " + formatTime (shown_.a) + "   D " + formatTime (shown_.d)
                            + "   S " + juce::String (sustain, 2) + "   R " + formatTime (shown_.r);
    g.drawText (text, labels, juce::Justification::centredLeft);
}
