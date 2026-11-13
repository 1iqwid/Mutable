#pragma once

#include <atomic>

#include <juce_audio_basics/juce_audio_basics.h>
#include "PlaitsVoice.h"

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
    };

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
    PlaitsVoice& getPlaitsVoice() noexcept  { return plaitsVoice_; }

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
    }

    //==========================================================================
    // Per-voice output gain applied before this voice is added into the shared
    // output buffer.  With 8 voices summing, an 8-note chord at full level would
    // otherwise clip; 0.35f leaves headroom so the default outGain is safe.
    // Applied in renderNextBlock() (audio thread) — a plain multiply, no
    // allocation or locks.
    static constexpr float kVoiceGain = 0.35f;

    //==========================================================================
    PlaitsVoice         plaitsVoice_;
    juce::ADSR          adsr_;
    Parameters          params_;
    juce::AudioBuffer<float> scratch_;     // mono, voice-private render scratch (Out)
    juce::AudioBuffer<float> scratchAux_;  // mono, voice-private render scratch (Aux)

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
    // renderNextBlock() is the ONLY place that calls plaitsVoice_.setPitchOffset(),
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