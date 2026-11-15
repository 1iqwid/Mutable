#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

//==============================================================================
/// Phase 4f: basic preset save / load.  MESSAGE THREAD ONLY (file I/O).
///
/// A preset file is the same XML the DAW stores (the APVTS state tree, tag
/// "State"), with extension .mutablepreset, kept in
/// <user documents>/Mutable/Presets.  The current preset name is stored as a
/// property of the state tree, so it is saved and restored with the DAW session.
///
/// applyState() resets every parameter to its default before replacing the
/// state, so a preset or session saved before a parameter existed loads that
/// parameter at its default instead of keeping whatever value was current.
class PresetManager
{
public:
    static constexpr const char* kExtension = ".mutablepreset";
    static constexpr const char* kStateTag  = "State";

    PresetManager (juce::AudioProcessor& processor, juce::AudioProcessorValueTreeState& apvts);

    static juce::File getPresetFolder();

    bool savePreset (const juce::File& file);
    bool loadPreset (const juce::File& file);
    void initPreset();                      // all params to defaults, name "Init"

    /// Step through the presets in the folder (sorted by name); delta = -1 / +1.
    /// Returns false if the folder has no presets.
    bool loadAdjacentPreset (int delta);

    juce::String getPresetName() const;

    /// Shared with setStateInformation: validate the tag, reset to defaults,
    /// then replace the state.  Returns false (state unchanged) if invalid.
    bool applyState (const juce::XmlElement& xml);

private:
    void resetToDefaults();
    void setPresetName (const juce::String& name);
    juce::Array<juce::File> listPresets() const;

    juce::AudioProcessor& processor_;
    juce::AudioProcessorValueTreeState& apvts_;

    JUCE_DECLARE_NON_COPYABLE (PresetManager)
};
