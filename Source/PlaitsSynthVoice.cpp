#include "PlaitsSynthVoice.h"
#include <JuceHeader.h>
#include <algorithm>   // std::clamp (mod-wheel temporary routing)

//==============================================================================
PlaitsSynthVoice::PlaitsSynthVoice()
{
    // juce::ADSR has no constructor taking Parameters — use the default
    // constructor and setParameters() after setSampleRate (called in prepare()).
    // The default parameters are fine here; they are overwritten by
    // setParameters() in prepare().

    // Enable external-level mode so the ADSR drives Plaits' accent.
    // When useExternalLevel_ is true, Plaits' LPG uses Process (no
    // built-in decay), so our ADSR is the sole envelope source.
    plaitsVoice_.setUseExternalLevel (true);
}

PlaitsSynthVoice::~PlaitsSynthVoice() = default;

//==============================================================================
void PlaitsSynthVoice::prepare (double hostSampleRate)
{
    plaitsVoice_.prepare (hostSampleRate);
    adsr_.setSampleRate (hostSampleRate);
    adsr_.setParameters (makeAdsrParams());
    adsr_.reset();

    // Pre-size the private scratch buffer once, on the message thread, so
    // renderNextBlock (audio thread) never allocates.
    ensureScratch (kMaxBlockSamples);
}

//==============================================================================
bool PlaitsSynthVoice::canPlaySound (juce::SynthesiserSound* sound)
{
    // We only have one sound type, but we only play when this voice's index is
    // below the shared active-voice count (Phase 3d selectable polyphony).
    //
    // JUCE calls this from findFreeVoice() and findVoiceToSteal()
    // (juce_Synthesiser.cpp) — both verified to consult canPlaySound() — so
    // returning false here simply excludes this voice from allocation and
    // stealing.  This is a single lock-free atomic read on the audio thread.
    (void) sound;
    if (activeCount_ == nullptr)
        return true;   // not wired up (shouldn't happen) — play by default
    return index_ < activeCount_->load (std::memory_order_relaxed);
}

//==============================================================================
void PlaitsSynthVoice::startNote (int midiNoteNumber,
                                  float velocity,
                                  juce::SynthesiserSound* sound,
                                  int currentPitchWheelPosition)
{
    (void) sound;

    // Pitch bend: a note that starts while the wheel is already bent must
    // already be bent.  JUCE passes the CURRENT wheel position in
    // currentPitchWheelPosition (juce_Synthesiser.cpp: startVoice() passes
    // lastPitchWheelValues[channel-1], which is initialised to 0x2000 = centred).
    // wheelValueToSemitones() maps 0x2000 → 0.0; 0x0000 → -bendRange;
    // 0x3FFF → +bendRange.  At zero bend (wheel centred) this is a no-op and
    // the note passes through to Plaits unmodified → exact tuning.
    const float semitones = wheelValueToSemitones (currentPitchWheelPosition,
                                                   params_.bendRange);

    // Apply the bend immediately so the very first rendered sample is already
    // at the bent pitch.  At zero bend (wheel centred) this is a no-op and the
    // note passes through to Plaits unmodified → exact tuning.
    plaitsVoice_.setPitchOffset (semitones);

    // Push per-block parameters into the PlaitsVoice before rendering.
    plaitsVoice_.setEngine (params_.engine);
    plaitsVoice_.setParams (params_.harmonics, params_.timbre, params_.morph);

    // Start the ADSR envelope.
    adsr_.noteOn();

    // Trigger the Plaits engine (sets noteOn_ and possibly retrigger_).
    plaitsVoice_.noteOn (midiNoteNumber, velocity);

    // Set the initial level to the first ADSR sample value.
    // (The actual per-sample envelope is applied in renderNextBlock.)
    plaitsVoice_.setLevel (adsr_.getNextSample());
}

//==============================================================================
void PlaitsSynthVoice::stopNote (float velocity, bool allowTailOff)
{
    (void) velocity;

    if (allowTailOff)
    {
        // Start the ADSR release phase.  The voice stays "active" until
        // renderNextBlock detects the ADSR going idle, at which point
        // clearCurrentNote() is called.
        adsr_.noteOff();
    }
    else
    {
        // Immediate stop: reset ADSR and clear the note immediately.
        adsr_.reset();
        plaitsVoice_.noteOff();
        clearCurrentNote();
    }
}

//==============================================================================
void PlaitsSynthVoice::pitchWheelMoved (int newPitchWheelValue)
{
    // newPitchWheelValue is the raw MIDI pitch-wheel value (0..0x3FFF, 0x2000
    // = centred).  wheelValueToSemitones() maps 0x2000 → 0.0; 0x0000 →
    // -bendRange; 0x3FFF → +bendRange.  This runs on every wheel move, including
    // moves on sounding voices (JUCE's handlePitchWheel calls pitchWheelMoved on
    // all active voices), so a held note tracks the wheel live.  A note started
    // while the wheel is already bent is handled in startNote() via the same
    // helper, so the two paths can never disagree.
    plaitsVoice_.setPitchOffset (
        wheelValueToSemitones (newPitchWheelValue, params_.bendRange));
}

//==============================================================================
void PlaitsSynthVoice::controllerMoved (int controllerNumber, int newControllerValue)
{
    if (controllerNumber == 1)   // CC1 = mod wheel
    {
        // Store the per-voice mod-wheel value as a 0..1 float.  (The voice is
        // only reached by JUCE when it is playing this note's channel.)
        modWheel_ = static_cast<float> (newControllerValue) / 127.0f;

        // TEMPORARY routing (Phase 3c): until the real mod section exists in
        // Phase 4, add modWheel * 0.5 to timbre, clamped to 0..1.  The actual
        // application happens in renderNextBlock() (single source of truth) so
        // this site and the per-block path can never disagree.  Remove this
        // block (and the modWheel_ member) once Phase 4 wires CC1 to the
        // proper mod-amount parameter.
        // (No setParams() call here — renderNextBlock() recomputes timbre from
        //  params_.timbre + modWheel_ * 0.5f every block.)
    }
}

//==============================================================================
void PlaitsSynthVoice::renderNextBlock (juce::AudioBuffer<float>& outputBuffer,
                                        int startSample,
                                        int numSamples)
{
    // If the voice is not active (no note assigned), do nothing.
    if (! isVoiceActive())
        return;

    // If the ADSR has gone idle (release finished), stop the note and
    // release this voice back to the synthesiser.
    if (! adsr_.isActive())
    {
        plaitsVoice_.noteOff();
        clearCurrentNote();
        return;
    }

    // Push the current per-block parameters (the processor calls
    // setParameters() before renderNextBlock for each block).
    //
    // TEMPORARY routing (Phase 3c): until the real mod section exists in
    // Phase 4, add modWheel * 0.5 to timbre, clamped to 0..1.  This is the
    // SINGLE SOURCE OF TRUTH for the CC1→timbre route (controllerMoved() only
    // stores modWheel_; it no longer calls setParams() itself), so a held note
    // is not reset to the base timbre every block while the wheel is moving.
    // Remove this modWheel_ term (and the modWheel_ member) once Phase 4 wires
    // CC1 to the proper mod-amount parameter.
    const float timbreWithMod =
        std::clamp (params_.timbre + modWheel_ * 0.5f, 0.0f, 1.0f);
    plaitsVoice_.setEngine (params_.engine);
    plaitsVoice_.setParams (params_.harmonics, timbreWithMod, params_.morph);

    // ---- Render into a local mono buffer, apply ADSR sample-by-sample ----
    //
    // We render into a single-channel scratch buffer, multiply each sample
    // by the corresponding ADSR value, and feed the ADSR value to
    // PlaitsVoice::setLevel() so the Plaits LPG (external-level mode)
    // uses the envelope as its accent.
    //
    // PlaitsVoice::render() writes into the out buffer; we then scale
    // each sample by the ADSR.

    const int numChannels = outputBuffer.getNumChannels();

    // Make sure our private scratch buffer is big enough for this block.
    // Pre-sized in prepare(); this is a no-op on the normal path.
    ensureScratch (numSamples);

    // Render the Plaits voice into our PRIVATE mono scratch buffer (NOT the
    // shared output buffer).  PlaitsVoice::render() overwrites its target,
    // which is fine here because scratch_ is voice-private.
    float* scratch = scratch_.getWritePointer (0);
    plaitsVoice_.render (scratch, nullptr, numSamples);

    // Apply the ADSR sample-by-sample and feed each value to setLevel().
    //
    // Important: we must call adsr_.getNextSample() in order to advance
    // the envelope state machine.  We multiply each scratch sample by the
    // envelope value AND pass it to setLevel() so Plaits' internal
    // external-level path uses the same value.
    //
    // This is done sample-by-sample because the ADSR is a stateful
    // sample-rate-corrected envelope generator.
    for (int i = 0; i < numSamples; ++i)
    {
        const float env = adsr_.getNextSample();
        scratch[i] *= env;
        plaitsVoice_.setLevel (env);
    }

    // Apply the per-voice gain (kVoiceGain) so an 8-note chord has headroom
    // and does not clip at the default outGain.  A plain multiply on our
    // private mono scratch — no allocation, locks, or file I/O.
    juce::FloatVectorOperations::multiply (scratch, kVoiceGain, numSamples);

    // Accumulate (ADD) our mono result into EVERY channel of the shared
    // output buffer.  ADD is required: Synthesiser::renderVoices() hands the
    // same already-cleared buffer to each voice and expects them to sum, so
    // that all active voices contribute (polyphony).  Copying channel 0 into
    // the others happens here, as before.
    for (int ch = 0; ch < numChannels; ++ch)
    {
        juce::FloatVectorOperations::add (
            outputBuffer.getWritePointer (ch) + startSample,
            scratch,
            numSamples);
    }

    // After rendering, check if the ADSR has just gone idle (release
    // completed within this block).  If so, stop the Plaits note and
    // release this voice.
    if (! adsr_.isActive())
    {
        plaitsVoice_.noteOff();
        clearCurrentNote();
    }
}