#pragma once

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
    }

    //==========================================================================
    PlaitsVoice         plaitsVoice_;
    juce::ADSR          adsr_;
    Parameters          params_;
    juce::AudioBuffer<float> scratch_;   // mono, voice-private render scratch

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PlaitsSynthVoice)
};