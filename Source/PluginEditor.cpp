#include "PluginEditor.h"
#include "EngineInfo.h"
#include <JuceHeader.h>
#include <cmath>

namespace
{
    constexpr int kKnobW   = 78;    // width of one knob column
    constexpr int kKnobH   = 112;   // label + rotary + text box + caption
    constexpr int kChoiceW = 170;   // width of a combo-box item
    constexpr int kPad     = 8;     // inner padding of a section
    constexpr int kTitleH  = 22;    // section title strip
    constexpr int kGap     = 10;    // gap between sections / rows

    const juce::Colour kBackground  { 0xff262626 };
    const juce::Colour kSectionFill { 0xff353535 };
    const juce::Colour kCaption     { 0xffa0a0a0 };

    /// Formats a value with a fixed number of decimals and an optional suffix,
    /// without printing "-0.00" for values that round to zero.
    juce::String formatValue (double v, int decimals, const juce::String& suffix)
    {
        const double eps = 0.5 * std::pow (10.0, -decimals);
        if (std::abs (v) < eps)
            v = 0.0;
        const juce::String text = decimals > 0 ? juce::String (v, decimals)
                                               : juce::String (juce::roundToInt (v));
        return suffix.isEmpty() ? text : text + " " + suffix;
    }
}

//==============================================================================
// Knob
//==============================================================================
MutableAudioProcessorEditor::Knob::Knob (APVTS& apvts, const juce::String& paramId,
                                         const juce::String& title,
                                         int decimals, const juce::String& suffix)
{
    label_.setText (title, juce::dontSendNotification);
    label_.setJustificationType (juce::Justification::centred);
    label_.setFont (juce::Font (juce::FontOptions (13.0f)));
    addAndMakeVisible (label_);

    slider_.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider_.setTextBoxStyle (juce::Slider::TextBoxBelow, false, kKnobW - 6, 18);
    addAndMakeVisible (slider_);

    // The attachment sets the range, value, and a text function from the
    // parameter; we then replace only the display text (the default float text
    // shows 7 decimals).  valueFromTextFunction (typing a value) is kept.
    attachment_ = std::make_unique<APVTS::SliderAttachment> (apvts, paramId, slider_);
    slider_.textFromValueFunction = [decimals, suffix] (double v)
    {
        return formatValue (v, decimals, suffix);
    };
    slider_.updateText();

    caption_.setJustificationType (juce::Justification::centred);
    caption_.setFont (juce::Font (juce::FontOptions (11.0f)));
    caption_.setColour (juce::Label::textColourId, kCaption);
    caption_.setMinimumHorizontalScale (0.7f);
    addAndMakeVisible (caption_);
}

void MutableAudioProcessorEditor::Knob::resized()
{
    auto r = getLocalBounds();
    label_.setBounds (r.removeFromTop (16));
    caption_.setBounds (r.removeFromBottom (14));
    slider_.setBounds (r);
}

void MutableAudioProcessorEditor::Knob::setCaption (const juce::String& text)
{
    caption_.setText (text, juce::dontSendNotification);
}

//==============================================================================
// Choice
//==============================================================================
MutableAudioProcessorEditor::Choice::Choice (APVTS& apvts, const juce::String& paramId,
                                             const juce::String& title,
                                             const juce::StringArray& items)
{
    label_.setText (title, juce::dontSendNotification);
    label_.setFont (juce::Font (juce::FontOptions (13.0f)));
    addAndMakeVisible (label_);

    // Items must be added BEFORE the attachment is created.  The attachment
    // maps item index <-> normalised value, so items must be in parameter order.
    box_.addItemList (items, 1);
    addAndMakeVisible (box_);
    attachment_ = std::make_unique<APVTS::ComboBoxAttachment> (apvts, paramId, box_);
}

void MutableAudioProcessorEditor::Choice::resized()
{
    auto r = getLocalBounds();
    label_.setBounds (r.removeFromTop (16));
    r.removeFromTop (4);
    box_.setBounds (r.removeFromTop (26));
}

//==============================================================================
// Editor
//==============================================================================
MutableAudioProcessorEditor::MutableAudioProcessorEditor (MutableAudioProcessor& p)
    : AudioProcessorEditor (&p),
      processorRef (p)
{
    using P = MutableAudioProcessor;
    auto& apvts = processorRef.getApvts();

    // ---- Engine ----------------------------------------------------------------
    juce::StringArray engineNames;
    for (int i = 0; i < engine_info::kNumEngines; ++i)
        engineNames.add (juce::String (i + 1) + "  " + engine_info::kEngines[i].name);

    auto engine = std::make_unique<Choice> (apvts, P::paramEngine, "Engine", engineNames);
    engine_ = engine.get();
    owned_.push_back (std::move (engine));

    harmonics_ = &knob (P::paramHarmonics, "Harmonics");
    timbre_    = &knob (P::paramTimbre,    "Timbre");
    morph_     = &knob (P::paramMorph,     "Morph");

    sections_.push_back ({ "Engine", { engine_, harmonics_, timbre_, morph_ } });
    sections_.push_back ({ "Output", { &knob (P::paramMix, "Out/Aux Mix"),
                                       &knob (P::paramAuxLevel, "Aux Level"),
                                       &knob (P::paramOutGain, "Out Gain") } });

    // ---- Envelope / LPG / tuning ----------------------------------------------
    sections_.push_back ({ "Envelope", { &knob (P::paramAttack,  "Attack",  3, "s"),
                                         &knob (P::paramDecay,   "Decay",   3, "s"),
                                         &knob (P::paramSustain, "Sustain"),
                                         &knob (P::paramRelease, "Release", 3, "s") } });
    sections_.push_back ({ "Low-pass gate", { &knob (P::paramLpgColour, "Colour"),
                                              &knob (P::paramLpgDecay,  "Decay") } });
    sections_.push_back ({ "Tuning", { &knob (P::paramCoarse,    "Coarse", 0, "st"),
                                       &knob (P::paramFine,      "Fine",   0, "ct"),
                                       &knob (P::paramBendRange, "Bend",   0, "st") } });

    // ---- Voices / LFO ----------------------------------------------------------
    sections_.push_back ({ "Voices", { &knob (P::paramVoices, "Voices", 0),
                                       &knob (P::paramUnison, "Unison", 0),
                                       &knob (P::paramDetune, "Detune", 1, "ct"),
                                       &knob (P::paramSpread, "Spread") } });

    juce::StringArray lfoShapes;
    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter (P::paramLfoShape)))
        lfoShapes = choice->choices;   // use the parameter's own list (no duplication)
    auto lfoShape = std::make_unique<Choice> (apvts, P::paramLfoShape, "Shape", lfoShapes);
    juce::Component* lfoShapePtr = lfoShape.get();
    owned_.push_back (std::move (lfoShape));

    sections_.push_back ({ "LFO", { lfoShapePtr,
                                    &knob (P::paramLfoRate,         "Rate", 2, "Hz"),
                                    &knob (P::paramModLfoHarmonics, "> Harmonics"),
                                    &knob (P::paramModLfoTimbre,    "> Timbre"),
                                    &knob (P::paramModLfoMorph,     "> Morph"),
                                    &knob (P::paramModLfoPitch,     "> Pitch") } });

    // ---- Mod matrix ------------------------------------------------------------
    sections_.push_back ({ "Envelope mod", { &knob (P::paramModEnvHarmonics, "> Harmonics"),
                                             &knob (P::paramModEnvTimbre,    "> Timbre"),
                                             &knob (P::paramModEnvMorph,     "> Morph"),
                                             &knob (P::paramModEnvPitch,     "> Pitch") } });
    sections_.push_back ({ "Velocity mod", { &knob (P::paramModVelHarmonics, "> Harmonics"),
                                             &knob (P::paramModVelTimbre,    "> Timbre"),
                                             &knob (P::paramModVelMorph,     "> Morph"),
                                             &knob (P::paramModVelPitch,     "> Pitch") } });
    sections_.push_back ({ "Mod wheel", { &knob (P::paramModWheelHarmonics, "> Harmonics"),
                                          &knob (P::paramModWheelTimbre,    "> Timbre"),
                                          &knob (P::paramModWheelMorph,     "> Morph"),
                                          &knob (P::paramModWheelPitch,     "> Pitch") } });

    rows_ = { { 0, 1 }, { 2, 3, 4 }, { 5, 6 }, { 7, 8, 9 } };

    for (auto& c : owned_)
        addAndMakeVisible (*c);

    // Engine captions follow the combo box.  The attachment updates the box
    // (with a synchronous notification) when the host or a preset changes the
    // engine, so onChange covers user and host changes alike.
    engine_->box().onChange = [this] { updateEngineCaptions(); };
    updateEngineCaptions();

    // Size to fit the widest row.
    int width = 0;
    for (const auto& row : rows_)
    {
        int w = kGap;
        for (int idx : row)
            w += sectionWidth (sections_[(size_t) idx]) + kGap;
        width = juce::jmax (width, w);
    }
    const int height = kGap + (int) rows_.size() * (kTitleH + kKnobH + kPad + kGap);
    setSize (width, height);
}

MutableAudioProcessorEditor::~MutableAudioProcessorEditor() = default;

//==============================================================================
MutableAudioProcessorEditor::Knob&
MutableAudioProcessorEditor::knob (const juce::String& paramId, const juce::String& title,
                                   int decimals, const juce::String& suffix)
{
    auto k = std::make_unique<Knob> (processorRef.getApvts(), paramId, title, decimals, suffix);
    Knob& ref = *k;
    owned_.push_back (std::move (k));
    return ref;
}

void MutableAudioProcessorEditor::updateEngineCaptions()
{
    const int idx = juce::jlimit (0, engine_info::kNumEngines - 1,
                                  engine_->box().getSelectedItemIndex());
    const auto& e = engine_info::kEngines[idx];
    harmonics_->setCaption (e.harmonics);
    timbre_->setCaption (e.timbre);
    morph_->setCaption (e.morph);
}

int MutableAudioProcessorEditor::itemWidth (const juce::Component* c) noexcept
{
    return dynamic_cast<const Choice*> (c) != nullptr ? kChoiceW : kKnobW;
}

int MutableAudioProcessorEditor::sectionWidth (const Section& s) noexcept
{
    int w = 2 * kPad;
    for (const auto* c : s.items)
        w += itemWidth (c);
    return w;
}

//==============================================================================
void MutableAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (kBackground);

    for (const auto& s : sections_)
    {
        g.setColour (kSectionFill);
        g.fillRoundedRectangle (s.bounds.toFloat(), 6.0f);

        g.setColour (juce::Colours::white);
        g.setFont (juce::Font (juce::FontOptions (14.0f, juce::Font::bold)));
        g.drawText (s.title, s.bounds.withHeight (kTitleH).reduced (kPad, 0),
                    juce::Justification::centredLeft);
    }
}

void MutableAudioProcessorEditor::resized()
{
    int y = kGap;
    for (const auto& row : rows_)
    {
        int x = kGap;
        for (int idx : row)
        {
            auto& s = sections_[(size_t) idx];
            const int w = sectionWidth (s);
            s.bounds = { x, y, w, kTitleH + kKnobH + kPad };

            int ix = x + kPad;
            for (auto* c : s.items)
            {
                const int iw = itemWidth (c);
                c->setBounds (ix, y + kTitleH, iw, kKnobH);
                ix += iw;
            }
            x += w + kGap;
        }
        y += kTitleH + kKnobH + kPad + kGap;
    }
}
