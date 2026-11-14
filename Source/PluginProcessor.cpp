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
const juce::String MutableAudioProcessor::paramAttack    ("attack");
const juce::String MutableAudioProcessor::paramDecay     ("decay");
const juce::String MutableAudioProcessor::paramSustain   ("sustain");
const juce::String MutableAudioProcessor::paramRelease   ("release");
const juce::String MutableAudioProcessor::paramBendRange ("bendRange");
const juce::String MutableAudioProcessor::paramVoices    ("voices");
const juce::String MutableAudioProcessor::paramMix       ("mix");
const juce::String MutableAudioProcessor::paramAuxLevel  ("auxLevel");
const juce::String MutableAudioProcessor::paramCoarse    ("coarse");
const juce::String MutableAudioProcessor::paramFine      ("fine");
const juce::String MutableAudioProcessor::paramLpgColour ("lpgColour");
const juce::String MutableAudioProcessor::paramLpgDecay  ("lpgDecay");
const juce::String MutableAudioProcessor::paramModEnvHarmonics   ("modEnvHarmonics");
const juce::String MutableAudioProcessor::paramModEnvTimbre      ("modEnvTimbre");
const juce::String MutableAudioProcessor::paramModEnvMorph       ("modEnvMorph");
const juce::String MutableAudioProcessor::paramModEnvPitch       ("modEnvPitch");
const juce::String MutableAudioProcessor::paramModVelHarmonics   ("modVelHarmonics");
const juce::String MutableAudioProcessor::paramModVelTimbre      ("modVelTimbre");
const juce::String MutableAudioProcessor::paramModVelMorph       ("modVelMorph");
const juce::String MutableAudioProcessor::paramModVelPitch       ("modVelPitch");
const juce::String MutableAudioProcessor::paramModWheelHarmonics ("modWheelHarmonics");
const juce::String MutableAudioProcessor::paramModWheelTimbre    ("modWheelTimbre");
const juce::String MutableAudioProcessor::paramModWheelMorph     ("modWheelMorph");
const juce::String MutableAudioProcessor::paramModWheelPitch     ("modWheelPitch");
const juce::String MutableAudioProcessor::paramLfoShape          ("lfoShape");
const juce::String MutableAudioProcessor::paramLfoRate           ("lfoRate");
const juce::String MutableAudioProcessor::paramModLfoHarmonics   ("modLfoHarmonics");
const juce::String MutableAudioProcessor::paramModLfoTimbre      ("modLfoTimbre");
const juce::String MutableAudioProcessor::paramModLfoMorph       ("modLfoMorph");
const juce::String MutableAudioProcessor::paramModLfoPitch       ("modLfoPitch");

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

    // --- Envelope (ADSR) parameters -------------------------------------------
    //
    // The three time parameters use a skewed range (skew ≈ 0.3) so the short
    // end has fine control.  Sustain is a plain 0..1 range.
    pAttack    = apvts_.createAndAddParameter (
        std::make_unique<juce::AudioParameterFloat>(
            paramAttack, "Attack",
            makeSkewedRange (0.001f, 5.0f, 0.005f), 0.3f));
    pDecay     = apvts_.createAndAddParameter (
        std::make_unique<juce::AudioParameterFloat>(
            paramDecay, "Decay",
            makeSkewedRange (0.001f, 5.0f, 0.2f), 0.3f));
    pSustain   = apvts_.createAndAddParameter (
        std::make_unique<juce::AudioParameterFloat>(
            paramSustain, "Sustain",
            juce::NormalisableRange<float>(0.0f, 1.0f), 0.7f));
    pRelease   = apvts_.createAndAddParameter (
        std::make_unique<juce::AudioParameterFloat>(
            paramRelease, "Release",
            makeSkewedRange (0.001f, 10.0f, 0.3f), 0.3f));

    // --- Pitch-bend range (Phase 3c) ----------------------------------------
    //
    // 0–24 semitones, integer steps, default 2.  AudioParameterInt's stepSize
    // is 1.0 by default, which satisfies the "step 1" requirement.
    pBendRange = apvts_.createAndAddParameter (
        std::make_unique<juce::AudioParameterInt>(
            paramBendRange, "Bend Range", 0, 24, 2));

    // --- Voice count (Phase 3d) ----------------------------------------------
    //
    // User-selectable polyphony: 1..kMaxVoices, integer steps, default
    // kMaxVoices (8).  All kMaxVoices voices are allocated up front and never
    // removed; this value only controls how many are eligible to play (see
    // PlaitsSynthVoice::canPlaySound and the processBlock release path).
    pVoices = apvts_.createAndAddParameter (
        std::make_unique<juce::AudioParameterInt>(
            paramVoices, "Voices", 1, kMaxVoices, kMaxVoices));

    // --- Aux output (Task 4a) -------------------------------------------------
    //
    // Plaits has two outputs: Out and Aux.  "Mix" crossfades between them
    // (0 = Out only, 1 = Aux only) and "Aux Level" sets the Aux loudness before
    // the blend.  Per-voice blend (PlaitsSynthVoice::renderNextBlock):
    //     blend = out * (1 - mix) + aux * auxLevel * mix
    // Both are linear 0..1.  At the default mix = 0 the output is exactly the
    // Out signal, so existing presets are unaffected.
    pMix      = apvts_.createAndAddParameter (
        std::make_unique<juce::AudioParameterFloat>(
            paramMix, "Mix",
            juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f));
    pAuxLevel = apvts_.createAndAddParameter (
        std::make_unique<juce::AudioParameterFloat>(
            paramAuxLevel, "Aux Level",
            juce::NormalisableRange<float>(0.0f, 1.0f), 1.0f));

    // --- Tuning (Task 4b) ----------------------------------------------------
    //
    // Coarse: integer semitones, -24..+24, default 0.  AudioParameterInt with
    // stepSize 1.0 (default) → integer steps.
    // Fine: cents, -100..+100, default 0.  Rendered as fine / 100 semitones.
    // Both are added on top of the live pitch bend in
    // PlaitsSynthVoice::renderNextBlock (total = bend + coarse + fine/100).
    // At coarse = 0, fine = 0, and a centred wheel the offset is exactly 0,
    // so the note is exactly in tune.
    pCoarse = apvts_.createAndAddParameter (
        std::make_unique<juce::AudioParameterInt>(
            paramCoarse, "Coarse Tune", -24, 24, 0));
    pFine = apvts_.createAndAddParameter (
        std::make_unique<juce::AudioParameterFloat>(
            paramFine, "Fine Tune",
            juce::NormalisableRange<float>(-100.0f, 100.0f), 0.0f));

    // --- LPG colour / decay (Task 4b) ----------------------------------------
    //
    // Drives the Plaits low-pass gate (PlaitsVoice::render → Patch):
    //   lpgColour → patch.lpg_colour (HF bleed; raises gate cutoff)
    //   lpgDecay  → patch.decay      (gate release length; 0.5 = mid)
    // Both are 0..1 linear, default 0.5, applied each block so they affect
    // sounding notes (incl. release) immediately.  See STATUS.md for the
    // exact voice.cc semantics.
    pLpgColour = apvts_.createAndAddParameter (
        std::make_unique<juce::AudioParameterFloat>(
            paramLpgColour, "LPG Colour",
            juce::NormalisableRange<float>(0.0f, 1.0f), 0.5f));
    pLpgDecay = apvts_.createAndAddParameter (
        std::make_unique<juce::AudioParameterFloat>(
            paramLpgDecay, "LPG Decay",
            juce::NormalisableRange<float>(0.0f, 1.0f), 0.5f));

    // --- Modulation matrix (Task 4c-1) --------------------------------------
    //
    // 12 bipolar (-1..+1) mod-amount parameters: three sources (envelope,
    // velocity, mod wheel) x four destinations (harmonics, timbre, morph,
    // pitch).  All default to 0.0 EXCEPT modWheelTimbre, which defaults to
    // 0.5 so the existing CC1->timbre routing is preserved by the matrix.
    // The "mod" prefix lets the UI group them.  Computed once per block in
    // PlaitsSynthVoice::renderNextBlock (see there for the formulas).
    const juce::NormalisableRange<float> bipolar (-1.0f, 1.0f);
    pModEnvHarmonics   = apvts_.createAndAddParameter (
        std::make_unique<juce::AudioParameterFloat>(
            paramModEnvHarmonics,   "Env -> Harmonics",  bipolar, 0.0f));
    pModEnvTimbre      = apvts_.createAndAddParameter (
        std::make_unique<juce::AudioParameterFloat>(
            paramModEnvTimbre,      "Env -> Timbre",     bipolar, 0.0f));
    pModEnvMorph       = apvts_.createAndAddParameter (
        std::make_unique<juce::AudioParameterFloat>(
            paramModEnvMorph,       "Env -> Morph",      bipolar, 0.0f));
    pModEnvPitch       = apvts_.createAndAddParameter (
        std::make_unique<juce::AudioParameterFloat>(
            paramModEnvPitch,       "Env -> Pitch",      bipolar, 0.0f));
    pModVelHarmonics   = apvts_.createAndAddParameter (
        std::make_unique<juce::AudioParameterFloat>(
            paramModVelHarmonics,   "Velocity -> Harmonics", bipolar, 0.0f));
    pModVelTimbre      = apvts_.createAndAddParameter (
        std::make_unique<juce::AudioParameterFloat>(
            paramModVelTimbre,      "Velocity -> Timbre",  bipolar, 0.0f));
    pModVelMorph       = apvts_.createAndAddParameter (
        std::make_unique<juce::AudioParameterFloat>(
            paramModVelMorph,       "Velocity -> Morph",   bipolar, 0.0f));
    pModVelPitch       = apvts_.createAndAddParameter (
        std::make_unique<juce::AudioParameterFloat>(
            paramModVelPitch,       "Velocity -> Pitch",   bipolar, 0.0f));
    pModWheelHarmonics = apvts_.createAndAddParameter (
        std::make_unique<juce::AudioParameterFloat>(
            paramModWheelHarmonics, "Mod Wheel -> Harmonics", bipolar, 0.0f));
    pModWheelTimbre    = apvts_.createAndAddParameter (
        std::make_unique<juce::AudioParameterFloat>(
            paramModWheelTimbre,    "Mod Wheel -> Timbre",  bipolar, 0.5f));
    pModWheelMorph     = apvts_.createAndAddParameter (
        std::make_unique<juce::AudioParameterFloat>(
            paramModWheelMorph,     "Mod Wheel -> Morph",   bipolar, 0.0f));
    pModWheelPitch     = apvts_.createAndAddParameter (
        std::make_unique<juce::AudioParameterFloat>(
            paramModWheelPitch,     "Mod Wheel -> Pitch",   bipolar, 0.0f));

    // --- LFO (Task 4c-2a) ------------------------------------------------------
    // Shape (0..5: Sine, Triangle, Saw Up, Saw Down, Square, S&H) and rate
    // (0.05..20 Hz, skewed to centre 1 Hz, default 1 Hz), plus the four
    // bipolar mod amounts (same pattern as the Task 4c-1 mod matrix).
    {
        auto lfoShape = std::make_unique<juce::AudioParameterChoice> (
            paramLfoShape, "LFO Shape",
            juce::StringArray {"Sine", "Triangle", "Saw Up", "Saw Down",
                               "Square", "S&H"},
            0);
        pLfoShape = apvts_.createAndAddParameter (std::move (lfoShape));
    }
    {
        juce::NormalisableRange<float> lfoRateRange (0.05f, 20.0f, 0.0f);
                lfoRateRange.setSkewForCentre (1.0f);
        auto lfoRate = std::make_unique<juce::AudioParameterFloat> (
                paramLfoRate, "LFO Rate", lfoRateRange, 1.0f);
        pLfoRate = apvts_.createAndAddParameter (std::move (lfoRate));
    }
    pModLfoHarmonics = apvts_.createAndAddParameter (
        std::make_unique<juce::AudioParameterFloat>(
            paramModLfoHarmonics,   "LFO -> Harmonics",  bipolar, 0.0f));
    pModLfoTimbre    = apvts_.createAndAddParameter (
        std::make_unique<juce::AudioParameterFloat>(
            paramModLfoTimbre,      "LFO -> Timbre",     bipolar, 0.0f));
    pModLfoMorph     = apvts_.createAndAddParameter (
        std::make_unique<juce::AudioParameterFloat>(
            paramModLfoMorph,       "LFO -> Morph",      bipolar, 0.0f));
    pModLfoPitch     = apvts_.createAndAddParameter (
        std::make_unique<juce::AudioParameterFloat>(
            paramModLfoPitch,       "LFO -> Pitch",      bipolar, 0.0f));

    // --- Create the 8-voice pool (message thread) ----------------------------
    //
    // A minimal SynthesiserSound that applies to all notes and channels.
    // The sound is reference-counted (ReferenceCountedObject); the synthesiser
    // keeps its own reference.
    class AnySound : public juce::SynthesiserSound
    {
    public:
        bool appliesToNote (int) override    { return true; }
        bool appliesToChannel (int) override { return true; }
    };

    // SynthesiserSound is a ReferenceCountedObject — use Ptr, not shared_ptr.
    juce::SynthesiserSound::Ptr sound = new AnySound();
    synth_.addSound (sound);

    // Allocate the full pool once (message thread — allocation only happens
    // here, never at runtime).  Every voice gets its index and a pointer to
    // the shared active-voice count so canPlaySound() can gate allocation.
    activeVoiceCount_.store (kMaxVoices, std::memory_order_relaxed);
    for (int i = 0; i < kMaxVoices; ++i)
    {
        auto* voice = new PlaitsSynthVoice();
        voice->setIndex (i);
        voice->setActiveVoiceCount (&activeVoiceCount_);
        synth_.addVoice (voice);
    }
}

MutableAudioProcessor::~MutableAudioProcessor()
{
}

//==============================================================================
void MutableAudioProcessor::prepareToPlay (double sampleRate, int)
{
    // Set the synthesiser's playback sample rate.  This calls
    // SynthesiserVoice::setCurrentPlaybackSampleRate() on every voice.
    // Must be called before the first renderNextBlock.
    synth_.setCurrentPlaybackSampleRate (sampleRate);

    // Prepare each voice's internal PlaitsVoice (creates/initialises the
    // plaits::Voice, BufferAllocator, and resamplers).  This is the
    // only place where heap allocation happens — prepareToPlay runs on
    // the message thread, never on the audio thread.
    for (int i = 0; i < synth_.getNumVoices(); ++i)
    {
        auto* voice = dynamic_cast<PlaitsSynthVoice*>(synth_.getVoice (i));
        if (voice != nullptr)
            voice->prepare (sampleRate);
    }
}

void MutableAudioProcessor::releaseResources()
{
    // Voices are owned by the synthesiser; their destructors (called when
    // the processor is destroyed) delete the plaits::Voice objects.
    // No additional cleanup needed here.
}

bool MutableAudioProcessor::isBusesLayoutSupported (const BusesLayout&) const
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
    const float attack    = (pAttack    != nullptr)
                            ? static_cast<juce::AudioParameterFloat*>(pAttack)->get()    : 0.005f;
    const float decay     = (pDecay     != nullptr)
                            ? static_cast<juce::AudioParameterFloat*>(pDecay)->get()     : 0.2f;
    const float sustain   = (pSustain   != nullptr)
                            ? static_cast<juce::AudioParameterFloat*>(pSustain)->get()   : 0.7f;
    const float release   = (pRelease   != nullptr)
                            ? static_cast<juce::AudioParameterFloat*>(pRelease)->get()   : 0.3f;
    const float bendRange = (pBendRange != nullptr)
                            ? static_cast<float>(static_cast<juce::AudioParameterInt*>(pBendRange)->get())
                            : 2.0f;
    const int   numVoices = (pVoices    != nullptr)
                            ? static_cast<juce::AudioParameterInt*>(pVoices)->get()
                            : kMaxVoices;

    const float mix       = (pMix       != nullptr)
                             ? static_cast<juce::AudioParameterFloat*>(pMix)->get()
                             : 0.0f;
    const float auxLevel  = (pAuxLevel  != nullptr)
                             ? static_cast<juce::AudioParameterFloat*>(pAuxLevel)->get()
                             : 1.0f;

    // ---- Tuning + LPG (Task 4b) ---------------------------------------------
    const float coarse    = (pCoarse    != nullptr)
                             ? static_cast<float>(static_cast<juce::AudioParameterInt*>(pCoarse)->get())
                             : 0.0f;
    const float fine      = (pFine      != nullptr)
                             ? static_cast<juce::AudioParameterFloat*>(pFine)->get()
                             : 0.0f;
    const float lpgColour = (pLpgColour != nullptr)
                             ? static_cast<juce::AudioParameterFloat*>(pLpgColour)->get()
                             : 0.5f;
    const float lpgDecay  = (pLpgDecay  != nullptr)
                             ? static_cast<juce::AudioParameterFloat*>(pLpgDecay)->get()
                             : 0.5f;

    // ---- Modulation matrix amounts (Task 4c-1) -------------------------------
    // Bipolar -1..+1, default 0 (modWheelTimbre defaults to 0.5).  Read once
    // per block into the snapshot below.
    const float modEnvHarmonics   = (pModEnvHarmonics   != nullptr)
                                     ? static_cast<juce::AudioParameterFloat*>(pModEnvHarmonics)->get()   : 0.0f;
    const float modEnvTimbre      = (pModEnvTimbre      != nullptr)
                                     ? static_cast<juce::AudioParameterFloat*>(pModEnvTimbre)->get()      : 0.0f;
    const float modEnvMorph       = (pModEnvMorph       != nullptr)
                                     ? static_cast<juce::AudioParameterFloat*>(pModEnvMorph)->get()       : 0.0f;
    const float modEnvPitch       = (pModEnvPitch       != nullptr)
                                     ? static_cast<juce::AudioParameterFloat*>(pModEnvPitch)->get()       : 0.0f;
    const float modVelHarmonics   = (pModVelHarmonics   != nullptr)
                                     ? static_cast<juce::AudioParameterFloat*>(pModVelHarmonics)->get()   : 0.0f;
    const float modVelTimbre      = (pModVelTimbre      != nullptr)
                                     ? static_cast<juce::AudioParameterFloat*>(pModVelTimbre)->get()      : 0.0f;
    const float modVelMorph       = (pModVelMorph       != nullptr)
                                     ? static_cast<juce::AudioParameterFloat*>(pModVelMorph)->get()       : 0.0f;
    const float modVelPitch       = (pModVelPitch       != nullptr)
                                     ? static_cast<juce::AudioParameterFloat*>(pModVelPitch)->get()       : 0.0f;
    const float modWheelHarmonics = (pModWheelHarmonics != nullptr)
                                     ? static_cast<juce::AudioParameterFloat*>(pModWheelHarmonics)->get() : 0.0f;
    const float modWheelTimbre    = (pModWheelTimbre    != nullptr)
                                     ? static_cast<juce::AudioParameterFloat*>(pModWheelTimbre)->get()    : 0.5f;
    const float modWheelMorph     = (pModWheelMorph     != nullptr)
                                     ? static_cast<juce::AudioParameterFloat*>(pModWheelMorph)->get()     : 0.0f;
    const float modWheelPitch     = (pModWheelPitch     != nullptr)
                                     ? static_cast<juce::AudioParameterFloat*>(pModWheelPitch)->get()     : 0.0f;

    // ---- Take a snapshot and give it to every voice -------------------------
    //
    // The snapshot is a plain struct written on the audio thread immediately
    // before renderNextBlock, so no locking is needed.  Each voice stores
    // the snapshot and applies it in its own renderNextBlock.
    PlaitsSynthVoice::Parameters voiceParams;
    voiceParams.engine    = engine;
    voiceParams.harmonics = harmonics;
    voiceParams.timbre    = timbre;
    voiceParams.morph     = morph;
    voiceParams.attack    = attack;
    voiceParams.decay     = decay;
    voiceParams.sustain   = sustain;
    voiceParams.release   = release;
    voiceParams.bendRange = bendRange;
    voiceParams.mix       = mix;
    voiceParams.auxLevel  = auxLevel;
    voiceParams.coarse    = coarse;
    voiceParams.fine      = fine;
    voiceParams.lpgColour = lpgColour;
    voiceParams.lpgDecay  = lpgDecay;
    voiceParams.modEnvHarmonics   = modEnvHarmonics;
    voiceParams.modEnvTimbre      = modEnvTimbre;
    voiceParams.modEnvMorph       = modEnvMorph;
    voiceParams.modEnvPitch       = modEnvPitch;
    voiceParams.modVelHarmonics   = modVelHarmonics;
    voiceParams.modVelTimbre      = modVelTimbre;
    voiceParams.modVelMorph       = modVelMorph;
    voiceParams.modVelPitch       = modVelPitch;
    voiceParams.modWheelHarmonics = modWheelHarmonics;
    voiceParams.modWheelTimbre    = modWheelTimbre;
    voiceParams.modWheelMorph     = modWheelMorph;
    voiceParams.modWheelPitch     = modWheelPitch;

    for (int i = 0; i < synth_.getNumVoices(); ++i)
    {
        auto* voice = dynamic_cast<PlaitsSynthVoice*>(synth_.getVoice (i));
        if (voice != nullptr)
            voice->setParameters (voiceParams);
    }

    const int prevCount = activeVoiceCount_.load (std::memory_order_relaxed);
    activeVoiceCount_.store (numVoices, std::memory_order_relaxed);

    // ---- Selectable polyphony (Phase 3d) ------------------------------------
    //
    // When the count decreases, any sounding voice whose index is now at or
    // above the new count is no longer eligible to play.  Release it smoothly
    // NOW (do not wait for the note to end): stopNote(1.0f, true) sets the key
    // up and starts the ADSR release phase, so it fades out with no click.
    //
    // This runs on the audio thread: it is a fixed loop over the static
    // kMaxVoices pool, a few atomic loads, and a virtual call per affected
    // voice.  No allocation, no locks (no note event is being processed), and
    // no file I/O.  Voices are never added or removed here.
    for (int i = 0; i < synth_.getNumVoices(); ++i)
    {
        auto* voice = dynamic_cast<PlaitsSynthVoice*>(synth_.getVoice (i));
        if (voice == nullptr)
            continue;

        if (i >= numVoices && i < prevCount && voice->isVoiceActive())
            voice->stopNote (1.0f, true);   // allowTailOff → release via ADSR
    }

    // ---- Render via the synthesiser ----------------------------------------
    //
    // synth_.renderNextBlock() handles:
    //   1. Splitting the block at each MIDI event's sample position.
    //   2. Calling voice->renderNextBlock() for each sub-segment.
    //   3. Dispatching noteOn / noteOff / pitchWheel / aftertouch etc.
    //
    // The buffer is already cleared; renderVoices() adds to it (does not
    // overwrite), so multiple voices sum into the same buffer.
    //
    // No allocation happens here: each voice's renderNextBlock writes into
    // the output buffer's existing memory (PlaitsVoice::render uses its own
    // pre-allocated scratch buffers, and the resamplers are stateful).
    synth_.renderNextBlock (buffer, midiMessages, 0, numSamples);

    // ---- Scale by outGain ---------------------------------------------------
    //
    // The synthesiser renders at full voice level; we apply the output gain
    // here so all voices are scaled uniformly.
    for (int ch = 0; ch < numChannels; ++ch)
        juce::FloatVectorOperations::multiply (buffer.getWritePointer (ch),
                                               outGain, numSamples);

    // ---- NaN / infinity guard -----------------------------------------------
    //
    // A NaN or infinity in the output buffer would cause the DAW's metering
    // to clip permanently and may crash the audio engine.  Scan channel 0
    // (the other channels are copies of it, or at worst have the same issue);
    // if any value is not finite, clear the entire buffer for this block.
    if (numChannels > 0)
    {
        float* ch0 = buffer.getWritePointer (0);
        bool bad = false;
        for (int i = 0; i < numSamples; ++i)
        {
            if (! juce_isfinite (ch0[i]))
            {
                bad = true;
                break;
            }
        }
        if (bad)
            buffer.clear();
    }
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