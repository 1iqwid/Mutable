#include "PresetManager.h"

namespace
{
    const juce::Identifier kPresetNameId ("presetName");
}

PresetManager::PresetManager (juce::AudioProcessor& processor,
                              juce::AudioProcessorValueTreeState& apvts)
    : processor_ (processor), apvts_ (apvts)
{
}

juce::File PresetManager::getPresetFolder()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
        .getChildFile ("Mutable")
        .getChildFile ("Presets");
}

bool PresetManager::savePreset (const juce::File& file)
{
    const auto target = file.withFileExtension (kExtension);
    if (! target.getParentDirectory().createDirectory())
        return false;

    // Store the name first so it is inside the saved file.
    setPresetName (target.getFileNameWithoutExtension());

    auto xml = apvts_.copyState().createXml();
    if (xml == nullptr)
        return false;

    return xml->writeTo (target);
}

bool PresetManager::loadPreset (const juce::File& file)
{
    auto xml = juce::XmlDocument::parse (file);
    if (xml == nullptr || ! applyState (*xml))
        return false;

    // The file name is the preset name (also covers renamed files).
    setPresetName (file.getFileNameWithoutExtension());
    return true;
}

void PresetManager::initPreset()
{
    resetToDefaults();
    setPresetName ("Init");
}

bool PresetManager::loadAdjacentPreset (int delta)
{
    const auto presets = listPresets();
    if (presets.isEmpty())
        return false;

    const auto current = getPresetName();
    int index = -1;
    for (int i = 0; i < presets.size(); ++i)
        if (presets[i].getFileNameWithoutExtension() == current)
            index = i;

    // Not in the folder (e.g. "Init"): next starts at the first, prev at the last.
    int next = 0;
    if (index < 0)
        next = delta > 0 ? 0 : presets.size() - 1;
    else
        next = (index + delta + presets.size()) % presets.size();

    return loadPreset (presets[next]);
}

juce::String PresetManager::getPresetName() const
{
    return apvts_.state.getProperty (kPresetNameId, "Init").toString();
}

bool PresetManager::applyState (const juce::XmlElement& xml)
{
    if (! xml.hasTagName (kStateTag))
        return false;

    resetToDefaults();
    apvts_.replaceState (juce::ValueTree::fromXml (xml));
    return true;
}

void PresetManager::resetToDefaults()
{
    for (auto* p : processor_.getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p))
            ranged->setValueNotifyingHost (ranged->getDefaultValue());
}

void PresetManager::setPresetName (const juce::String& name)
{
    apvts_.state.setProperty (kPresetNameId, name, nullptr);
}

juce::Array<juce::File> PresetManager::listPresets() const
{
    auto files = getPresetFolder().findChildFiles (juce::File::findFiles, false,
                                                   juce::String ("*") + kExtension);
    files.sort();   // juce::File compares by full path -> alphabetical
    return files;
}
