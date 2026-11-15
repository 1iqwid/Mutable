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
    for (auto& layer : layers_)
        layer.setUseExternalLevel (true);
}

PlaitsSynthVoice::~PlaitsSynthVoice() = default;

//==============================================================================
void PlaitsSynthVoice::prepare (double hostSampleRate)
{
    // All kMaxUnison layers are prepared (allocated) here, on the message
    // thread, so changing the unison count never allocates on the audio path.
    for (auto& layer : layers_)
        layer.prepare (hostSampleRate);
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
    // site of setPitchOffset() (per layer), so this path and the per-block path
    // can never disagree.
    bendSemis_ = wheelValueToSemitones (currentPitchWheelPosition,
                                        params_.bendRange);

    // Unison (Task 4d): latch the layer count for this note.  Changing the
    // unison param while a note is held takes effect on the next note.
    activeLayers_ = std::clamp (params_.unison, 1, kMaxUnison);

    // Start the ADSR envelope.  The first sample is consumed once (not once per
    // layer) and used as every layer's initial level.
    adsr_.noteOn();
    const float firstEnv = adsr_.getNextSample();

    // One LFO seed per note, shared by all layers, so S&H steps line up.
    ++lfoSeedCounter_;
    const uint32_t lfoSeed = lfoSeedCounter_ * 2654435761u + 1u;

    for (int k = 0; k < kMaxUnison; ++k)
    {
        auto& layer = layers_[k];
        if (k >= activeLayers_)
        {
            layer.noteOff();   // unused by this note: never rendered
            continue;
        }

        // Push per-block parameters into the layer before rendering.
        layer.setEngine (params_.engine);
        layer.setParams (params_.harmonics, params_.timbre, params_.morph);

        // Trigger the Plaits engine (sets noteOn_ and possibly retrigger_).
        layer.noteOn (midiNoteNumber, velocity);
        layer.resetLfo (lfoSeed);

        // Initial level (the per-sample envelope is applied in renderNextBlock).
        layer.setLevel (firstEnv);
    }
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
        allLayersOff();
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
        allLayersOff();
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

    // ---- Pitch (single source of truth) ------------------------------------
    //
    // Total semitone offset = live bend (bendSemis_) + coarse + fine/100
    //                        + modPitch (12 · mod-matrix pitch contribution)
    //                        + per-layer unison detune (added in the layer loop).
    // Computed HERE and applied via the ONLY setPitchOffset() call site (the
    // layer loop below), so every path that affects pitch flows through it.
    //
    // PlaitsVoice::render() ramps the pitch offset linearly across the 24-
    // sample chunks (zipper-noise avoidance) and snaps on note start
    // (PlaitsVoice::noteOn → snapToParams()).
    const float basePitch =
        bendSemis_ + params_.coarse + params_.fine * 0.01f + pitchModSemitones;

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
    float* accL    = acc_.getWritePointer (0);
    float* accR    = acc_.getWritePointer (1);
    juce::FloatVectorOperations::clear (accL, numSamples);
    juce::FloatVectorOperations::clear (accR, numSamples);

    // Mix / auxLevel are ramped linearly across this block (start = previous
    // block's value, end = current value).
    const float invNum  = (numSamples > 0) ? (1.0f / static_cast<float> (numSamples)) : 0.0f;
    const float mixDelta  = params_.mix      - prevMix;
    const float auxDelta  = params_.auxLevel - prevAuxLevel;

    // ---- Unison layers (Task 4d) -------------------------------------------
    //
    // Each active layer gets the same params plus its own detune (pos · detune
    // cents) and pan (pos · spread), where pos is evenly spaced in -1..+1.
    // Each layer renders into the per-layer scratch, is blended Out/Aux, and is
    // ADDED into the stereo accumulator with balance-law pan gains (centre =
    // 1/1, so unison 1 is identical to the pre-4d mono output).
    const int   numLayers = activeLayers_;
    const float detuneSemis = params_.detune * 0.01f;
    const float spread = std::clamp (params_.spread, 0.0f, 1.0f);

    for (int k = 0; k < numLayers; ++k)
    {
        auto& layer = layers_[k];
        const float pos = layerPosition (k, numLayers);

        layer.setEngine (params_.engine);
        layer.setParams (effHarmonics, effTimbre, effMorph);
        layer.setPitchOffset (basePitch + pos * detuneSemis);

        // LPG colour / decay (Task 4b): pushed each block so the knobs affect
        // sounding notes (including release) immediately.
        layer.setLpg (params_.lpgColour, params_.lpgDecay);

        // LFO (Task 4c-2b): the pitch amount is scaled by 12.0f (semitones per
        // unit LFO value), exactly like modEnvPitch in the mod matrix.
        layer.setLfo (params_.lfoShape,
                      params_.lfoRate,
                      params_.modLfoHarmonics,
                      params_.modLfoTimbre,
                      params_.modLfoMorph,
                      12.0f * params_.modLfoPitch);

        // Render into the voice-private scratch (overwritten by render()).
        layer.render (outBuf, auxBuf, numSamples);

        const float p  = spread * pos;                  // -1 (L) .. +1 (R)
        const float gL = (p > 0.0f) ? 1.0f - p : 1.0f;
        const float gR = (p < 0.0f) ? 1.0f + p : 1.0f;

        for (int i = 0; i < numSamples; ++i)
        {
            const float t = static_cast<float> (i) * invNum;   // 0 at the first sample
            const float mix = prevMix + mixDelta * t;
            const float auxLvl = prevAuxLevel + auxDelta * t;

            const float blended =
                outBuf[i] * (1.0f - mix) + auxBuf[i] * auxLvl * mix;

            accL[i] += blended * gL;
            accR[i] += blended * gR;
        }
    }

    // Apply the ADSR sample-by-sample (once per sample, NOT per layer), the
    // per-voice gain, and 1/sqrt(layers) loudness compensation (1.0 for 1 layer).
    static constexpr float kLayerNorm[kMaxUnison + 1] =
        { 1.0f, 1.0f, 0.70710678f, 0.57735027f, 0.5f };
    const float voiceGain = kVoiceGain * kLayerNorm[numLayers];

    for (int i = 0; i < numSamples; ++i)
    {
        const float env  = adsr_.getNextSample();   // stateful — must run in order
        lastAdsrValue_ = env;   // capture for the NEXT block's mod matrix
        const float g = env * voiceGain;
        accL[i] *= g;
        accR[i] *= g;
    }

    // setLevel() only stores a value read once per render() call, so setting
    // it to the block's last envelope value is equivalent to the old
    // per-sample call.
    for (int k = 0; k < numLayers; ++k)
        layers_[k].setLevel (lastAdsrValue_);

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

    // Accumulate (ADD) into the shared output buffer.  ADD is required:
    // Synthesiser::renderVoices() hands the same already-cleared buffer to each
    // voice and expects them to sum (polyphony).  Channel 0 = L, channel 1 = R;
    // a mono bus or any extra channels get the L/R average.
    for (int ch = 0; ch < numChannels; ++ch)
    {
        float* dest = outputBuffer.getWritePointer (ch) + startSample;
        if (numChannels >= 2 && ch == 0)
            juce::FloatVectorOperations::add (dest, accL, numSamples);
        else if (numChannels >= 2 && ch == 1)
            juce::FloatVectorOperations::add (dest, accR, numSamples);
        else
        {
            juce::FloatVectorOperations::addWithMultiply (dest, accL, 0.5f, numSamples);
            juce::FloatVectorOperations::addWithMultiply (dest, accR, 0.5f, numSamples);
        }
    }

    // After rendering, check if the ADSR has just gone idle (release
    // completed within this block).  If so, stop the Plaits note and
    // release this voice.
    if (! adsr_.isActive())
    {
        allLayersOff();
        clearCurrentNote();
    }
}