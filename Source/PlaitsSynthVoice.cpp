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

    // Capture the note-on velocity (0..1) for the mod matrix.  This is the
    // "velocity" source for the current voice; held until the next noteOn on
    // this voice.  Set BEFORE noteOn so the first render block already has
    // the correct value.
    velocity_ = velocity;

    // Pitch bend: a note that starts while the wheel is already bent must
    // already be bent.  JUCE passes the CURRENT wheel position in
    // currentPitchWheelPosition (juce_Synthesiser.cpp: startVoice() passes
    // lastPitchWheelValues[channel-1], which is initialised to 0x2000 = centred).
    // wheelValueToSemitones() maps 0x2000 → 0.0; 0x0000 → -bendRange;
    // 0x3FFF → +bendRange.  At zero bend (wheel centred) this stores 0.0 and the
    // note passes through to Plaits unmodified (renderNextBlock adds coarse/fine
    // on top, which are 0 at the factory defaults) → exact tuning.
    //
    // We only STORE the bend here.  renderNextBlock() is the single source of
    // truth for the total pitch offset (bend + coarse + fine) and the ONLY call
    // site of plaitsVoice_.setPitchOffset(), so this path and the per-block path
    // can never disagree.
    bendSemis_ = wheelValueToSemitones (currentPitchWheelPosition,
                                        params_.bendRange);

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
    //
    // We only STORE the new bend here — we do NOT call setPitchOffset().
    // renderNextBlock() recomputes total = bendSemis_ + coarse + fine/100 every
    // block and is the sole caller of setPitchOffset(), so the live bend and
    // the tuning knobs always combine correctly, including on sounding voices
    // (and notes in release).
    bendSemis_ = wheelValueToSemitones (newPitchWheelValue, params_.bendRange);
}

//==============================================================================
void PlaitsSynthVoice::controllerMoved (int controllerNumber, int newControllerValue)
{
    if (controllerNumber == 1)   // CC1 = mod wheel
    {
        // Store the per-voice mod-wheel value as a 0..1 float.  (The voice is
        // only reached by JUCE when it is playing this note's channel.)
        wheel_ = static_cast<float> (newControllerValue) / 127.0f;
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

    // ---- Modulation matrix (Task 4c-1) --------------------------------------
    //
    // Compute the effective harmonics / timbre / morph and the mod-pitch
    // offset ONCE per block from the per-voice sources (envSource_, velocity_,
    // wheel_) and the 12 mod-amount parameters in the snapshot.  The formula
    // is exactly as specified:
    //
    //   effective = clamp (base + env·amtEnv + vel·amtVel + wheel·amtWheel, 0, 1)
    //   for harmonics, timbre, morph.
    //
    //   modPitch (semitones) = 12 × (env·amtEnv + vel·amtVel + wheel·amtWheel)
    //   for pitch.
    //
    // At the default settings (all mod amounts 0 except modWheelTimbre = 0.5)
    // this reduces to timbre = clamp (timbre + wheel · 0.5, 0, 1) and
    // harmonics / morph / pitch = base — i.e. exactly the previous behaviour.
    //
    // No allocation, no locks, no file I/O.  The values are pushed into the
    // PlaitsVoice below; PlaitsVoice::render() ramps them linearly across the
    // 24-sample chunks (zipper-noise avoidance) and snaps on note start
    // (PlaitsVoice::noteOn → snapToParams()).
    const float envAmtH = params_.modEnvHarmonics;
    const float envAmtT = params_.modEnvTimbre;
    const float envAmtM = params_.modEnvMorph;
    const float envAmtP = params_.modEnvPitch;
    const float velAmtH = params_.modVelHarmonics;
    const float velAmtT = params_.modVelTimbre;
    const float velAmtM = params_.modVelMorph;
    const float velAmtP = params_.modVelPitch;
    const float whlAmtH = params_.modWheelHarmonics;
    const float whlAmtT = params_.modWheelTimbre;
    const float whlAmtM = params_.modWheelMorph;
    const float whlAmtP = params_.modWheelPitch;

    // The mod contribution is source-dependent (each destination has its own
    // three amounts), so compute each destination independently.
    const float harmMod = envSource_ * envAmtH + velocity_ * velAmtH + wheel_ * whlAmtH;
    const float timbMod = envSource_ * envAmtT + velocity_ * velAmtT + wheel_ * whlAmtT;
    const float morphMod = envSource_ * envAmtM + velocity_ * velAmtM + wheel_ * whlAmtM;
    const float pitchModSemitones =
        12.0f * (envSource_ * envAmtP + velocity_ * velAmtP + wheel_ * whlAmtP);

    const float effHarmonics =
        std::clamp (params_.harmonics + harmMod,   0.0f, 1.0f);
    const float effTimbre    =
        std::clamp (params_.timbre    + timbMod,   0.0f, 1.0f);
    const float effMorph     =
        std::clamp (params_.morph     + morphMod,  0.0f, 1.0f);

    // Push the effective harmonics / timbre / morph into the PlaitsVoice.
    plaitsVoice_.setEngine (params_.engine);
    plaitsVoice_.setParams (effHarmonics, effTimbre, effMorph);

    // ---- Pitch (single source of truth) ------------------------------------
    //
    // Total semitone offset = live bend (bendSemis_) + coarse + fine/100
    //                        + modPitch (12 · mod-matrix pitch contribution).
    // Computed HERE and applied via the ONLY setPitchOffset() call site.
    // The modPitch term is added to the existing total, so the single-source-
    // of-truth rule is preserved: every path that affects pitch flows through
    // this one setPitchOffset() call.
    //
    // PlaitsVoice::render() ramps the pitch offset linearly across the 24-
    // sample chunks (zipper-noise avoidance) and snaps on note start
    // (PlaitsVoice::noteOn → snapToParams()).
    plaitsVoice_.setPitchOffset (
        bendSemis_ + params_.coarse + params_.fine * 0.01f + pitchModSemitones);

    // ---- LPG colour / decay (Task 4b) ---------------------------------------
    //
    // Pushed into the Plaits Patch each block so changing the knobs affects
    // sounding notes (including release) immediately.  Defaults lpgColour 0.5,
    // lpgDecay 0.5.
    plaitsVoice_.setLpg (params_.lpgColour, params_.lpgDecay);

    // ---- Render both outputs, apply ADSR + voice gain, blend Out/Aux -------
    //
    // We render the Out and Aux channels into two voice-private mono scratch
    // buffers (pre-sized in prepare(); no allocation on the audio path).
    //
    // The ADSR drives the Plaits level via setLevel() each sample (external-
    // level mode: the LPG uses Process with no built-in decay) AND is
    // multiplied into both Out and Aux here, so both outputs share the same
    // envelope.  kVoiceGain is applied to both for 8-voice headroom.
    //
    // Mix / auxLevel are ramped linearly across the block from the previous
    // block's value to the current value (zipper-noise avoidance).  The blend
    // is per-voice:  blend = out * (1 - mix) + aux * auxLevel * mix, which
    // reduces to exactly the old (Out-only) signal at the default mix = 0.

    const int numChannels = outputBuffer.getNumChannels();

    // Make sure our private scratch buffers are big enough for this block.
    // Pre-sized in prepare(); this is a no-op on the normal path.
    ensureScratch (numSamples);

    float* outBuf  = scratch_.getWritePointer (0);
    float* auxBuf  = scratchAux_.getWritePointer (0);

    // Render the Plaits voice into our PRIVATE scratch buffers (NOT the shared
    // output buffer).  PlaitsVoice::render() overwrites its targets, which is
    // fine because these are voice-private.
    plaitsVoice_.render (outBuf, auxBuf, numSamples);

    // Ramp mix / auxLevel linearly across this block (start = previous block's
    // value, end = current value), apply the ADSR sample-by-sample, apply the
    // per-voice gain, and blend the two channels.
    const float invNum  = (numSamples > 0) ? (1.0f / static_cast<float> (numSamples)) : 0.0f;
    const float mixDelta  = params_.mix      - prevMix;
    const float auxDelta  = params_.auxLevel - prevAuxLevel;

    for (int i = 0; i < numSamples; ++i)
    {
        const float env  = adsr_.getNextSample();   // stateful — must run in order
        plaitsVoice_.setLevel (env);
        lastAdsrValue_ = env;   // capture for the NEXT block's mod matrix

        const float t = static_cast<float> (i) * invNum;   // 0 at the first sample
        const float mix = prevMix + mixDelta * t;
        const float auxLvl = prevAuxLevel + auxDelta * t;

        const float blended =
            outBuf[i] * (1.0f - mix) + auxBuf[i] * auxLvl * mix;

        outBuf[i] = blended * env * kVoiceGain;
    }

    // Finish the ramp: the next block starts from this block's end values.
    prevMix      = params_.mix;
    prevAuxLevel = params_.auxLevel;

    // Capture the LAST ADSR value from this block as the envelope source for
    // the NEXT block's mod-matrix computation (Task 4c-1).  envSource_ is the
    // "envelope" source: the voice's ADSR value (0..1).  Using the last value
    // of the previous block (rather than the first value of the current block)
    // is the convention specified in the task: the mod matrix uses the
    // envelope value that was in effect at the END of the last render, which
    // is the most recent sample we actually produced.  This is a single float
    // write — no allocation, no lock, no file I/O.
    envSource_ = lastAdsrValue_;

    // Accumulate (ADD) our mono result into EVERY channel of the shared
    // output buffer.  ADD is required: Synthesiser::renderVoices() hands the
    // same already-cleared buffer to each voice and expects them to sum, so
    // that all active voices contribute (polyphony).  Copying channel 0 into
    // the others happens here, as before.
    for (int ch = 0; ch < numChannels; ++ch)
    {
        juce::FloatVectorOperations::add (
            outputBuffer.getWritePointer (ch) + startSample,
            outBuf,
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