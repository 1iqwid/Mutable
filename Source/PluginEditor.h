#pragma once

#include <memory>
#include <vector>

#include "PluginProcessor.h"
#include "ScopeView.h"
#include "EnvelopeView.h"

//==============================================================================
/// Phase 4e: plain functional editor.  Stock JUCE components only, bound to the
/// APVTS through attachments; the editor never touches DSP objects.  Custom
/// graphics come in Phase 6.
class MutableAudioProcessorEditor final
    : public juce::AudioProcessorEditor,
      private juce::Timer
{
public:
    explicit MutableAudioProcessorEditor (MutableAudioProcessor&);
    ~MutableAudioProcessorEditor() override;

    //==============================================================================
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    // Preset bar (Phase 4f).  The name label is refreshed on a slow timer so it
    // also follows state restored by the host while the editor is open.
    void timerCallback() override;
    void setupPresetBar();
    void refreshPresetName();
    void showLoadDialog();
    void showSaveDialog();

    juce::TextButton initButton_ { "Init" }, loadButton_ { "Load..." },
                     saveButton_ { "Save..." }, prevButton_ { "<" }, nextButton_ { ">" };
    juce::Label presetName_;
    std::unique_ptr<juce::FileChooser> chooser_;   // must outlive launchAsync

    using APVTS = juce::AudioProcessorValueTreeState;

    //==========================================================================
    /// Title label, rotary slider with a text box, and an optional caption
    /// (used for the engine-specific meaning of Harmonics / Timbre / Morph).
    class Knob : public juce::Component
    {
    public:
        Knob (APVTS& apvts, const juce::String& paramId, const juce::String& title,
              int decimals, const juce::String& suffix);
        void resized() override;
        void setCaption (const juce::String& text);

    private:
        juce::Label  label_;
        juce::Slider slider_;
        juce::Label  caption_;
        std::unique_ptr<APVTS::SliderAttachment> attachment_;
    };

    /// Title label + combo box bound to a parameter (engine, LFO shape).
    class Choice : public juce::Component
    {
    public:
        Choice (APVTS& apvts, const juce::String& paramId, const juce::String& title,
                const juce::StringArray& items);
        void resized() override;
        juce::ComboBox& box() noexcept { return box_; }

    private:
        juce::Label    label_;
        juce::ComboBox box_;
        std::unique_ptr<APVTS::ComboBoxAttachment> attachment_;
    };

    struct Section
    {
        juce::String title;
        std::vector<juce::Component*> items;
        juce::Rectangle<int> bounds;   // set in resized(), drawn in paint()
    };

    Knob& knob (const juce::String& paramId, const juce::String& title,
                int decimals = 2, const juce::String& suffix = {});
    void updateEngineCaptions();
    static int itemWidth (const juce::Component* c) noexcept;
    static int sectionWidth (const Section& s) noexcept;

    MutableAudioProcessor& processorRef;

    std::vector<std::unique_ptr<juce::Component>> owned_;
    std::vector<Section> sections_;
    std::vector<std::vector<int>> rows_;   // section indices per row

    Choice* engine_   = nullptr;
    Knob*   harmonics_ = nullptr;
    Knob*   timbre_    = nullptr;
    Knob*   morph_     = nullptr;

    // Phase 5: visualizer row below the parameter rows.
    ScopeView scope_;
    juce::Rectangle<int> scopeSection_;
    juce::Label scopeZoomLabel_;
    juce::Slider scopeZoom_;            // view setting (ms), not a parameter
    juce::ToggleButton scopeTrigger_ { "Trigger" };
    void setupScopeControls();

    EnvelopeView envelope_;
    juce::Rectangle<int> envSection_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MutableAudioProcessorEditor)
};
