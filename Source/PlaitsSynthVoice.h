#pragma once

#include <atomic>

#include <juce_audio_basics/juce_audio_basics.h>
#include "PlaitsVoice.h"
#include "VoiceDisplayState.h"

//==============================================================================
/// A juce::SynthesiserVoice that owns one PlaitsVoice and one juce::ADSR.
///
/// The ADSR drives the Plaits level via external-level mode
/// (setUseExternalLevel(true) + setLevel(adsrValue)), so Plaits' internal
/// LPG is used without its built-in decay, and our envelope handles release.
///
/// Fixed ADSR defaults for now (Phase 3a):
///   attack   5 ms
///   decay   200 ms
///   sustain 0.7
///   release 300 ms
///
/// No allocation, locks, or file I/O in any method.
class PlaitsSynthVoice : public juce::SynthesiserVoice
{
public:
    PlaitsSynthVoice();
    ~PlaitsSynthVoice() override;

    //==========================================================================
    // juce::SynthesiserVoice overrides
    //==========================================================================
    bool canPlaySound (juce::SynthesiserSound* sound) override;
    void startNote (int midiNoteNumber,
                    float velocity,
                    juce::SynthesiserSound* sound,
                    int currentPitchWheelPosition) override;
    void stopNote (float velocity, bool allowTailOff) override;
    void pitchWheelMoved (int newPitchWheelValue) override;
    void controllerMoved (int controllerNumber, int newControllerValue) override;
    void renderNextBlock (juce::AudioBuffer<float>& outputBuffer,
                          int startSample,
                          int numSamples) override;

    //==========================================================================
    /// Called from the processor's prepareToPlay (message thread, NOT audio
    /// thread).  Prepares the internal PlaitsVoice for the given sample rate.
    /// Must be called before the first renderNextBlock.
    void prepare (double hostSampleRate);

    //==========================================================================
    // Voice-count gating (Phase 3d).
    //
    // All 8 PlaitsSynthVoice instances are allocated once and never added or
    // removed at runtime (addVoice/removeVoice would allocate).  Instead the
    // processor owns a single shared std::atomic<int> "active voice count" and
    // each voice is given its own index.  canPlaySound() returns true only
    // while index_ is below that count, so JUCE's voice allocator
    // (findFreeVoice / findVoiceToSteal, both of which check canPlaySound)
    // naturally limits new notes and steals to the first N voices.
    //
    // setIndex() / setActiveVoiceCount() are message-thread only (the processor
    // calls them in the constructor); they only store a pointer and an int and
    // take no locks and allocate nothing.
    //==========================================================================
    void setIndex (int voiceIndex) noexcept
    {
        index_ = voiceIndex;
    }

    void setActiveVoiceCount (std::atomic<int>* count) noexcept
    {
        activeCount_ = count;
    }

    /// Phase 5c: processor-owned 2-channel scope tap (ch 0 = Out, ch 1 = Aux).
    /// The processor clears it each block; renderNextBlock ADDS this voice's
    /// pre-Mix Out / Aux (after envelope and voice gain) at startSample.
    /// Message thread only (set once in the processor constructor).
    void setScopeTap (juce::AudioBuffer<float>* tap) noexcept
    {
        scopeTap_ = tap;
    }

    /// Phase 5e: processor-owned envelope position for the UI (may be null).
    /// Message thread only (set once in the processor constructor).
    void setDisplayState (VoiceDisplayState* state) noexcept
    {
        display_ = state;
    }

    //==========================================================================
    /// Per-block parameter snapshot, written by the processor (audio thread)
    /// before each renderNextBlock call.  A plain struct — no locks needed
    /// because it is written on the audio thread immediately before render.
    struct Parameters
    {
        int   engine    = 8;    // 0..23
        float harmonics = 0.5f; // 0..1
        float timbre    = 0.5f; // 0..1
        float morph     = 0.5f; // 0..1
        float attack    = 0.005f; // seconds (0.001–5)
        float decay     = 0.2f;   // seconds (0.001–5)
        float sustain   = 0.7f;   // 0..1
        float release   = 0.3f;   // seconds (0.001–10)
        float bendRange = 2.0f;   // semitones (0–24), pitch-bend amount
        float mix       = 0.0f;   // 0..1, 0 = Out only, 1 = Aux only
        float auxLevel  = 1.0f;   // 0..1, Aux loudness before the blend
        // ---- Task 4b: tuning + LPG colour/decay ---------------------------
        // Pitch tuning (added on top of the live bend in renderNextBlock):
        //   total semitones = bendSemis_ + coarse + fine / 100.
        float coarse = 0.0f;      // semitones, integer -24..+24, default 0
        float fine   = 0.0f;      // cents, -100..+100, default 0 (== 0.0 semitones)
        // Plaits low-pass gate (see PlaitsVoice::render):
        float lpgColour = 0.5f;   // 0..1 HF bleed (patch.lpg_colour)
        float lpgDecay  = 0.5f;   // 0..1 gate release length (patch.decay)
        // ---- Task 4c-1: modulation matrix (bipolar -1..+1) ------------------
        // Three sources (envelope, velocity, mod wheel) x four destinations
        // (harmonics, timbre, morph, pitch).  All default to 0.0 EXCEPT
        // modWheelTimbre = 0.5, which preserves the existing CC1->timbre
        // routing.  Computed once per block in renderNextBlock(); see there
        // for the exact formulas.
        float modEnvHarmonics   = 0.0f;
        float modEnvTimbre      = 0.0f;
        float modEnvMorph       = 0.0f;
        float modEnvPitch       = 0.0f;
        float modVelHarmonics   = 0.0f;
        float modVelTimbre      = 0.0f;
        float modVelMorph       = 0.0f;
        float modVelPitch       = 0.0f;
        float modWheelHarmonics = 0.0f;
        float modWheelTimbre    = 0.5f;   // default 0.5 (was the temp CC1->timbre)
        float modWheelMorph     = 0.0f;
        float modWheelPitch     = 0.0f;
        // ---- Task 4c-2b: LFO (bipolar -1..+1 amounts) -----------------------
        // lfoShape is the Lfo.h shape index (0=Sine,1=Triangle,2=SawUp,
        // 3=SawDown,4=Square,5=S&H).  lfoRate is Hz (0.05..20).  The four mod
        // amounts are bipolar -1..+1 (same convention as the mod matrix above).
        // The LFO pitch amount is a bipolar amount like the others; PlaitsSynthVoice
        // scales it by 12 (semitones per unit), exactly like modEnvPitch.
        int   lfoShape         = 0;
        float lfoRate          = 1.0f;
        float modLfoHarmonics  = 0.0f;
        float modLfoTimbre     = 0.0f;
        float modLfoMorph      = 0.0f;
        float modLfoPitch      = 0.0f;
        // ---- Task 4d: unison ------------------------------------------------
        // unison: number of stacked PlaitsVoice layers (1..kMaxUnison), latched
        // at note start.  detune: cents offset of the OUTER layers (inner layers
        // spaced evenly between -detune and +detune).  spread: 0..1 stereo
        // width (0 = all layers centred).  unison = 1 is identical to pre-4d.
        int   unison = 1;
        float detune = 10.0f;     // cents, 0..50
        float spread = 0.5f;      // 0..1
    };

    /// Maximum unison layers per voice (Task 4d).  8 voices x 4 layers = 32
    /// PlaitsVoice instances, all allocated in prepare().
    static constexpr int kMaxUnison = 4;

    /// Called from the processor's processBlock (AUDIO THREAD) immediately
    /// before each renderNextBlock, with the latest parameter snapshot.
    ///
    /// Stores the snapshot, and — only if a value actually changed — pushes
    /// the ADSR times/level into adsr_ via setParameters().  Calling
    /// setParameters() recalculates the envelope rates without resetting the
    /// envelope state, so in-progress attack/decay/sustain/release continue
    /// with the new values and there are no clicks.  No allocation, locks, or
    /// string lookups occur here.
    void setParameters (const Parameters& p) noexcept
    {
        const bool adsrChanged =
            p.attack    != params_.attack ||
            p.decay     != params_.decay ||
            p.sustain   != params_.sustain ||
            p.release   != params_.release;

        params_ = p;

        if (adsrChanged)
        {
            // Guard against zero / negative times: JUCE's ADSR treats a time
            // of <= 0 as "skip that stage instantly" (rate == -1), which we
            // don't want.  The parameter ranges already exclude 0, so this
            // only protects against unexpected values.
            const float minTime = 0.001f;
            juce::ADSR::Parameters a;
            a.attack  = (p.attack  > minTime) ? p.attack  : minTime;
            a.decay   = (p.decay   > minTime) ? p.decay   : minTime;
            a.release = (p.release > minTime) ? p.release : minTime;
            a.sustain = p.sustain < 0.0f ? 0.0f : (p.sustain > 1.0f ? 1.0f : p.sustain);
            adsr_.setParameters (a);
        }
    }

    //==========================================================================
    PlaitsVoice& getPlaitsVoice() noexcept  { return layers_[0]; }

private:
    //==========================================================================
    /// Convert a raw MIDI pitch-wheel value (0..0x3FFF, 0x2000 = centred) to a
    /// signed semitone offset, using the voice's current bend range.
    ///
    /// Shared by startNote() (note started while the wheel is already bent) and
    /// pitchWheelMoved() (live wheel moves on sounding voices) so the two paths
    /// can never disagree.  Centred wheel (8192) → 0.0 semitones, which leaves
    /// the note value unmodified → exact tuning at zero bend.  Full range
    /// (0 / 0x3FFF) → ∓bendRange.
    static float wheelValueToSemitones (int wheelValue, float bendRange) noexcept
    {
        const float normalised =
            (static_cast<float> (wheelValue) - 8192.0f) / 8192.0f;  // -1.0 .. +1.0
        return normalised * bendRange;
    }

private:
    //==========================================================================
    // Fixed ADSR defaults (Phase 3a — to be parameterised in Phase 4).
    static juce::ADSR::Parameters makeAdsrParams() noexcept
    {
        return juce::ADSR::Parameters (0.005f,  // attack   (5 ms)
                                       0.200f,  // decay   (200 ms)
                                       0.700f,  // sustain (0.7)
                                       0.300f); // release (300 ms)
    }

    //==========================================================================
    // Private mono scratch buffer for this voice's render.
    //
    // JUCE's Synthesiser::renderVoices() calls renderNextBlock() on every
    // voice with the SAME already-cleared output buffer, and the contract
    // (juce_Synthesiser.h, SynthesiserVoice::renderNextBlock) is that each
    // voice must ADD its samples into that buffer.  We therefore render into
    // this private mono buffer and then accumulate it into the shared output,
    // which is what lets all 8 voices sum together (polyphony).
    //
    // Pre-sized in prepare() (message thread) to kMaxBlockSamples so the audio
    // path never allocates; ensureScratch() is a no-op when capacity is enough.
    static constexpr int kMaxBlockSamples = 8192;
    void ensureScratch (int numSamples) noexcept
    {
        if (scratch_.getNumSamples() < numSamples)
            scratch_.setSize (1, numSamples);
        if (scratchAux_.getNumSamples() < numSamples)
            scratchAux_.setSize (1, numSamples);
        if (acc_.getNumSamples() < numSamples)
            acc_.setSize (2, numSamples);
        if (scopeAcc_.getNumSamples() < numSamples)
            scopeAcc_.setSize (2, numSamples);
    }

    /// Unison layer position in -1..+1 (outer layers at the ends, evenly
    /// spaced); 0 when there is only one layer.  Used for detune and pan.
    static float layerPosition (int layer, int numLayers) noexcept
    {
        if (numLayers <= 1)
            return 0.0f;
        return 2.0f * static_cast<float> (layer) / static_cast<float> (numLayers - 1) - 1.0f;
    }

    /// noteOff on every layer (including ones unused by the current note).
    void allLayersOff() noexcept
    {
        for (auto& layer : layers_)
            layer.noteOff();
    }

    //==========================================================================
    // Per-voice output gain applied before this voice is added into the shared
    // output buffer.  With 8 voices summing, an 8-note chord at full level would
    // otherwise clip; 0.35f leaves headroom so the default outGain is safe.
    // Applied in renderNextBlock() (audio thread) — a plain multiply, no
    // allocation or locks.
    static constexpr float kVoiceGain = 0.35f;

    //==========================================================================
    // Unison layers (Task 4d).  Only the first activeLayers_ are triggered and
    // rendered; activeLayers_ is latched from params_.unison in startNote().
    PlaitsVoice         layers_[kMaxUnison];
    int                 activeLayers_ = 1;
    uint32_t            lfoSeedCounter_ = 0;   // shared per-note LFO seed source
    juce::ADSR          adsr_;
    Parameters          params_;
    juce::AudioBuffer<float> scratch_;     // mono, per-layer render scratch (Out)
    juce::AudioBuffer<float> scratchAux_;  // mono, per-layer render scratch (Aux)
    juce::AudioBuffer<float> acc_;         // stereo, sum of all layers (L, R)
    juce::AudioBuffer<float> scopeAcc_;    // Phase 5c: pre-Mix Out (0) / Aux (1), mono sums
    juce::AudioBuffer<float>* scopeTap_ = nullptr;   // processor-owned, may be null

    // Phase 5e: envelope stage tracking for the display.  juce::ADSR does not
    // expose its state, so it is inferred: attack until the level reaches 1,
    // decay until it reaches sustain, then sustain; release after a tail-off
    // stopNote (linear from releaseStartLevel_ to 0, as juce::ADSR does).
    VoiceDisplayState* display_ = nullptr;
    bool  released_          = false;
    bool  peakReached_       = false;
    float releaseStartLevel_ = 0.0f;
    int   releaseFromStage_    = VoiceDisplayState::kIdle;   // stage at note-off
    float releaseFromProgress_ = 0.0f;                      // progress at note-off
    /// Pre-release stage + progress from the current level (attack/decay/sustain).
    void heldStage (float level, int& stage, float& progress) const noexcept;
    void publishDisplay() noexcept;
    void publishIdle() noexcept
    {
        if (display_ != nullptr)
            display_->publish (VoiceDisplayState::kIdle, 0.0f, 0.0f);
    }

    // Previous per-block mix / auxLevel, used to ramp linearly across each
    // block (start = previous value, end = current value) so parameter changes
    // never cause zipper noise.  prevMix starts at 0, which is exactly the
    // factory mix default, so the first block renders at the default (Out
    // only).  prevAuxLevel starts at 0 (ramps to the auxLevel default 1.0 over
    // the first block) — at the default mix = 0 the aux path contributes
    // nothing, so this has no audible effect.
    float prevMix      = 0.0f;
    float prevAuxLevel = 0.0f;

    // Modulation sources (Task 4c-1), all per-voice:
    //   envSource_  — the voice's ADSR value 0..1; the LAST value from the
    //                 previous block (stored as a member, updated at the end
    //                 of renderNextBlock).  0.0 before the first block.
    //   velocity_   — the note-on velocity 0..1 (set in startNote()).
    //   wheel_      — CC1 normalised 0..1 (set in controllerMoved()).
    // These feed the mod matrix in renderNextBlock (see there).
    float envSource_ = 0.0f;
    float velocity_  = 1.0f;
    float wheel_     = 0.0f;

    // The last ADSR sample value produced in the previous renderNextBlock()
    // call.  Captured inside the sample loop (line "lastAdsrValue_ = env;")
    // and written into envSource_ at the end of renderNextBlock so the NEXT
    // block's mod-matrix computation uses the envelope value that was in
    // effect at the END of the last render (Task 4c-1 convention).  Plain
    // member — no allocation, no lock, no file I/O.
    float lastAdsrValue_ = 0.0f;

    // Pitch bend in SEMITONES — the single source of truth for the live bend.
    // Written ONLY by pitchWheelMoved() and startNote() (message thread).
    // renderNextBlock() is the ONLY place that calls setPitchOffset() (per layer),
    // using total = bendSemis_ + params_.coarse + params_.fine / 100.0f.
    // Centred wheel (bendSemis_ = 0) with coarse = 0, fine = 0 → total = 0 →
    // the MIDI note is passed to Plaits unmodified → exactly in tune.
    float bendSemis_ = 0.0f;

    // Voice-count gating (Phase 3d).  index_ is this voice's position in the
    // pool (0..kMaxVoices-1); activeCount_ points at the processor-owned
    // shared atomic holding the current user-selected voice count.  Both are
    // written once on the message thread and read on the audio thread in
    // canPlaySound() — the atomic load is the only synchronisation needed.
    int              index_ = 0;
    std::atomic<int>* activeCount_ = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PlaitsSynthVoice)
};