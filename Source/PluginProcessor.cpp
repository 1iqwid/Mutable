#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <JuceHeader.h>

//==============================================================================
// Parameter ID strings — must match exactly what the APVTS registers.
const juce::String MutableAudioProcessor::paramEngine    ("engine");
const juce::String MutableAudioProcessor::paramHarmonics ("harmonics");
const juce::String MutableAudioProcessor::paramTimbre    ("timbre");
const juce::String MutableAudioProcessor::paramMorph     ("morph");
const juce::String MutableAudioProcessor::paramOutGain   ("outGain");

//==============================================================================
MutableAudioProcessor::MutableAudioProcessor()
    : AudioProcessor(BusesProperties()
                     .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts_(*this, nullptr)
{
    // --- Register parameters (message thread only) --------------------------
    // JUCE 8: createAndAddParameter(std::make_unique<RangedAudioParameter>(...))
    pEngine    = apvts_.createAndAddParameter (
        std::make_unique<juce::AudioParameterInt>(
            paramEngine, "Engine", 0, 23, 8));
    pHarmonics = apvts_.createAndAddParameter (
        std::make_unique<juce::AudioParameterFloat>(
            paramHarmonics, "Harmonics",
            juce::NormalisableRange<float>(0.0f, 1.0f), 0.5f));
    pTimbre    = apvts_.createAndAddParameter (
        std::make_unique<juce::AudioParameterFloat>(
            paramTimbre, "Timbre",
            juce::NormalisableRange<float>(0.0f, 1.0f), 0.5f));
    pMorph     = apvts_.createAndAddParameter (
        std::make_unique<juce::AudioParameterFloat>(
            paramMorph, "Morph",
            juce::NormalisableRange<float>(0.0f, 1.0f), 0.5f));
    pOutGain   = apvts_.createAndAddParameter (
        std::make_unique<juce::AudioParameterFloat>(
            paramOutGain, "Out Gain",
            juce::NormalisableRange<float>(0.0f, 1.0f), 0.7f));
}

MutableAudioProcessor::~MutableAudioProcessor()
{
}

//==============================================================================
void MutableAudioProcessor::prepareToPlay (double sampleRate, int)
{
    voice_.prepare (sampleRate);
}

void MutableAudioProcessor::releaseResources()
{
    // voice_ is a member; its destructor deletes the plaits::Voice.
}

bool MutableAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return true;
}

void MutableAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                          juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();

    const int numSamples  = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();
    if (numSamples <= 0)
        return;

    // ---- Read parameters once per block (cached atomics, no string lookups)
    //
    // AudioParameterInt::get()   → reads std::atomic<float> value, roundToInt → int
    // AudioParameterFloat::get() → reads std::atomic<float> value → float
    // Both are lock-free and safe on the audio thread.
    //
    // Note: get() returns the NON-normalised value (the actual parameter value),
    // which is what we want.  getValue() would return the normalised 0..1 value.

    const int   engine    = (pEngine    != nullptr)
                            ? static_cast<juce::AudioParameterInt*>(pEngine)->get()
                            : 8;
    const float harmonics = (pHarmonics != nullptr)
                            ? static_cast<juce::AudioParameterFloat*>(pHarmonics)->get() : 0.5f;
    const float timbre    = (pTimbre    != nullptr)
                            ? static_cast<juce::AudioParameterFloat*>(pTimbre)->get()    : 0.5f;
    const float morph     = (pMorph     != nullptr)
                            ? static_cast<juce::AudioParameterFloat*>(pMorph)->get()     : 0.5f;
    const float outGain   = (pOutGain   != nullptr)
                            ? static_cast<juce::AudioParameterFloat*>(pOutGain)->get()   : 0.7f;

    // Push parameters into the voice (message-thread-safe setters).
    if (engine != lastEngineSent_)
    {
        voice_.setEngine (engine);
        lastEngineSent_ = engine;
    }
    voice_.setParams (harmonics, timbre, morph);

    // ---- Render in segments split at each MIDI event's sample position ----
    //
    // PlaitsVoice::render() writes into a float buffer.  We render directly
    // into channel 0 of the output buffer at the appropriate offset, then
    // scale by outGain and copy to the remaining channels.
    //
    // render() handles the null-voice case (outputs silence) internally.

    auto renderSegment = [&](int start, int end)
    {
        const int len = end - start;
        if (len <= 0)
            return;

        float* ch0 = buffer.getWritePointer (0);
        voice_.render (ch0 + start, nullptr, len);

        // Scale by outGain in place.
        juce::FloatVectorOperations::multiply (ch0 + start, outGain, len);

        // Copy to remaining channels.
        for (int ch = 1; ch < numChannels; ++ch)
        {
            juce::FloatVectorOperations::copy (buffer.getWritePointer (ch) + start,
                                               ch0 + start, len);
        }
    };

    // Walk events in sample order.
    // MidiBufferIterator dereferences to MidiMessageMetadata.
    int writePos = 0;
    for (auto it = midiMessages.begin(); it != midiMessages.end(); ++it)
    {
        const auto& meta = *it;
        const int eventPos = meta.samplePosition;  // 0-based sample index

        // Render everything up to (but not including) this event.
        if (eventPos > writePos)
            renderSegment (writePos, eventPos);
        writePos = eventPos;

        // Apply the MIDI event.
        // JUCE 8: MidiMessageMetadata::getMessage() takes no args.
        const juce::MidiMessage msg = meta.getMessage();
        if (msg.isNoteOnOrOff())
        {
            const int   note = msg.getNoteNumber();
            const float vel  = msg.getFloatVelocity();  // 0..1

            if (vel > 0.0f)
                voice_.noteOn (note, vel);
            else
                voice_.noteOff();  // velocity 0 counts as noteOff
        }
    }

    // Render the tail after the last event.
    if (writePos < numSamples)
        renderSegment (writePos, numSamples);
}

//==============================================================================
bool MutableAudioProcessor::hasEditor() const
{
    return true;
}

juce::AudioProcessorEditor* MutableAudioProcessor::createEditor()
{
    // TEMPORARY: use GenericAudioProcessorEditor so parameters appear as
    // sliders.  Replace with the custom MutableAudioProcessorEditor in Phase 6.
    return new juce::GenericAudioProcessorEditor (*this);
}

//==============================================================================
bool MutableAudioProcessor::isMidiEffect() const
{
    return false;
}

double MutableAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

//==============================================================================
bool MutableAudioProcessor::acceptsMidi() const
{
    return true; // This is a synth — it wants to receive MIDI notes.
}

bool MutableAudioProcessor::producesMidi() const
{
    return false;
}

//==============================================================================
int MutableAudioProcessor::getNumPrograms()
{
    return 1; // No per-program state yet.
}

int MutableAudioProcessor::getCurrentProgram()
{
    return 0;
}

void MutableAudioProcessor::setCurrentProgram (int)
{
    // No per-program state yet.
}

const juce::String MutableAudioProcessor::getProgramName (int)
{
    return {};
}

void MutableAudioProcessor::changeProgramName (int, const juce::String&)
{
    // No per-program state yet.
}

//==============================================================================
void MutableAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // copyState() flushes pending updates and returns a thread-safe copy.
    auto state = apvts_.copyState();
    auto xml = state.createXml();
    if (xml != nullptr)
    {
        auto str = xml->toString();
        auto bytes = str.toRawUTF8();
        destData.replaceAll (bytes, static_cast<int>(str.getNumBytesAsUTF8()));
    }
    else
    {
        destData.setSize (0);
    }
}

void MutableAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = juce::XmlDocument::parse (juce::String::createStringFromData (data, sizeInBytes));
    if (xml != nullptr && xml->getTagName() == "State")
        apvts_.replaceState (juce::ValueTree::fromXml (*xml));
}

//==============================================================================
const juce::String MutableAudioProcessor::getName() const
{
    return "Mutable";
}

//==============================================================================
// This creates new instances of the plugin.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MutableAudioProcessor();
}