#include "PlaitsVoice.h"

#include <algorithm>
#include <cstring>

#include "plaits/dsp/voice.h"
#include "plaits/dsp/dsp.h"
#include "stmlib/utils/buffer_allocator.h"

//==============================================================================

PlaitsVoice::PlaitsVoice()
    : allocator_ (engineBuffer_, sizeof (engineBuffer_))
{
    // allocator_ is constructed in the member-init list.
    // voice_ is created in prepare().
}

PlaitsVoice::~PlaitsVoice()
{
    if (voice_ != nullptr)
    {
        delete voice_;
        voice_ = nullptr;
    }
    // allocator_ owns no heap memory; engineBuffer_ is a fixed array.
}

void PlaitsVoice::prepare (double hostSampleRate)
{
    hostRate_ = hostSampleRate;
    ratio_    = 48000.0 / hostSampleRate;

    // Create or re-initialise the Voice.
    // Voice::Init calls BufferAllocator::Allocate many times – must NOT
    // be called from the audio thread.
    if (voice_ == nullptr)
        voice_ = new plaits::Voice();

    voice_->Init (&allocator_);

    // Reset resamplers and note / scratch state.
    resamplerOut_.reset (hostSampleRate);
    resamplerAux_.reset (hostSampleRate);
    noteOn_          = false;
    retrigger_       = false;
    lastTriggerHigh_ = false;
    scratchCount_    = 0;
}

void PlaitsVoice::setEngine (int engineIndex)
{
    if (engineIndex < 0)  engineIndex = 0;
    if (engineIndex > 23) engineIndex = 23;
    engineIndex_ = engineIndex;
}

void PlaitsVoice::setParams (float harmonics, float timbre, float morph)
{
    harmonics_ = harmonics < 0.0f ? 0.0f : (harmonics > 1.0f ? 1.0f : harmonics);
    timbre_    = timbre    < 0.0f ? 0.0f : (timbre    > 1.0f ? 1.0f : timbre);
    morph_     = morph     < 0.0f ? 0.0f : (morph     > 1.0f ? 1.0f : morph);
}

void PlaitsVoice::noteOn (int midiNote, float velocity)
{
    midiNote_ = midiNote;
    velocity_ = velocity < 0.0f ? 0.0f : (velocity > 1.0f ? 1.0f : velocity);

    // Snap the ramp "previous" values to the current targets so this new note
    // does NOT ramp from the previous note's harmonics / timbre / morph /
    // pitch offset (that would cause an audible glitch on note start).  This
    // is the "snap on note start" requirement of the mod matrix.
    snapToParams();

    if (lastTriggerHigh_)
    {
        // Plaits' internal trigger_state_ is still latched high from a
        // previous render (either noteOn_ was true, or noteOff() was called
        // without a render in between — the voice-stealing case).  Set the
        // retrigger flag so render() flushes the 5-slot trigger delay line
        // before re-asserting trigger=1.0f, producing a clean rising edge.
        retrigger_ = true;
    }

    noteOn_ = true;
}

void PlaitsVoice::noteOff ()
{
    noteOn_ = false;
}

void PlaitsVoice::setLevel (float level)
{
    level_ = level < 0.0f ? 0.0f : (level > 1.0f ? 1.0f : level);
}

void PlaitsVoice::snapToParams()
{
    prevHarmonics_ = harmonics_;
    prevTimbre_    = timbre_;
    prevMorph_     = morph_;
    prevPitch_     = pitchOffset_;
}

//==============================================================================
// render
//==============================================================================

void PlaitsVoice::render (float* out, float* aux, int numSamples)
{
    if (voice_ == nullptr || numSamples <= 0)
    {
        juce::FloatVectorOperations::clear (out, numSamples);
        if (aux != nullptr)
            juce::FloatVectorOperations::clear (aux, numSamples);
        return;
    }

    // ---- Step 1: Generate all needed 48 kHz samples into scratch buffers --
    //
    // We need at least (numSamples * ratio_) + 2 input samples to produce
    // numSamples output samples.  We generate in chunks of kMaxPlaitsBlockSize
    // (24) to stay within Plaits' block limit.
    //
    // If scratchCount_ already has leftover samples from a previous render()
    // call (because the resampler consumed fewer than we generated), we
    // append after them.
    const double neededIn = static_cast<double> (numSamples) * ratio_ + 2.0;
    int          totalIn  = scratchCount_;

    // Build the Patch and Modulations once per render() call (the note /
    // engine / trigger fields are constant within a block; harmonics /
    // timbre / morph / pitch are ramped per-chunk — see below).
    plaits::Patch patch;
      patch.harmonics = prevHarmonics_;
    patch.timbre    = prevTimbre_;
    patch.morph     = prevMorph_;
    patch.note      = static_cast<float> (midiNote_) + prevPitch_;
    patch.frequency_modulation_amount = 0.0f;
    patch.timbre_modulation_amount    = 0.0f;
    patch.morph_modulation_amount     = 0.0f;
    patch.engine                    = engineIndex_;
    patch.decay                     = lpgDecay_;   // gate release length (0..1)
    patch.lpg_colour                = lpgColour_;  // HF bleed (0..1)

    plaits::Modulations mod;
    mod.engine            = 0.5f;   // mid CV → no engine drift
    mod.note              = 0.0f;   // no V/OCT offset
    mod.frequency         = 0.0f;
    mod.harmonics         = 0.0f;
    mod.timbre            = 0.0f;
    mod.morph             = 0.0f;
    mod.trigger           = noteOn_ ? 1.0f : 0.0f;
    mod.frequency_patched = false;
    mod.timbre_patched    = false;
    mod.morph_patched     = false;
    mod.trigger_patched   = true;   // RISING_EDGE / HIGH / LOW per note state

    if (useExternalLevel_)
    {
        // External level: pass our value as the accent, LPG uses Process
        // (no built-in decay).  Our ADSR (Phase 3) will handle release.
        mod.level         = level_;
        mod.level_patched = true;
    }
    else
    {
        // Default: velocity drives accent, LPG uses ProcessPing
        // (ramp-up + decay), giving the classic "play, hold, decay" envelope.
        mod.level         = velocity_;
        mod.level_patched = false;
    }

    const float int16Scale = 1.0f / 32768.0f;

    // ---- Parameter ramping (zipper-noise avoidance, Task 4c-1) -------------
    //
    // harmonics, timbre, morph and the pitch offset are ramped linearly from
    // their previous values (the values used in the PREVIOUS render() call,
    // held in prevHarmonics_ / prevTimbre_ / prevMorph_ / prevPitch_) to their
    // current targets (harmonics_ / timbre_ / morph_ / pitchOffset_) across
    // the 24-sample chunks of this host block.  This removes zipper noise when
    // a mod-matrix amount (or a base parameter) changes between blocks.
    //
    // The "previous" value is captured once, here, and is the value at the END
    // of the previous render() call (== the previous target).  Each chunk then
    // advances the value a fixed fraction (toGen / neededIn) of the total
    // delta.  At the end of this call the value has advanced by
    // (totalIn - initialTotalIn) / neededIn, which is exactly the fraction of
    // the host block generated this call — so consecutive calls ramp
    // continuously.  After a note starts, snapToParams() (in noteOn()) makes
    // prev == target so the delta is 0 and there is no ramp from a stale value.
    const float rampHarmStart = prevHarmonics_;
    const float rampTimbStart = prevTimbre_;
    const float rampMorphStart = prevMorph_;
    const float rampPitchStart = prevPitch_;
    const float rampHarmDelta = harmonics_ - rampHarmStart;
    const float rampTimbDelta = timbre_    - rampTimbStart;
    const float rampMorphDelta = morph_    - rampMorphStart;
    const float rampPitchDelta = pitchOffset_ - rampPitchStart;

    // The retrigger flush renders 5 samples before the main loop, so the ramp
    // start for those samples is captured here (they use the "previous" values
    // — correct, as they precede the ramped region).
    int rampGenerated = 0;   // samples generated since the ramp start

    // ---- Retrigger handling -----------------------------------------------
    //
    // If retrigger_ is set (noteOn while already triggered), Plaits' internal
    // trigger_state_ is latched high.  We must flush the 5-slot trigger delay
    // line by writing trigger=0.0f for 5 consecutive samples, which causes
    // the edge detector to see trigger_value < 0.1f and clear trigger_state_.
    // Then the normal generation loop continues with trigger=1.0f, producing
    // a clean false→true rising edge → TRIGGER_RISING_EDGE.
    //
    // 5 samples is enough because the delay line is 8 slots (kMaxTriggerDelay)
    // but the read offset is 5 (kTriggerDelay), so 5 writes of 0.0f guarantee
    // that the slot being read is 0.0f.
    if (retrigger_)
    {
        retrigger_ = false;
        noteOn_ = true;  // ensure noteOn_ is true for the rest of this call

        plaits::Modulations modOff = mod;
        modOff.trigger = 0.0f;

        for (int s = 0; s < 5; ++s)
        {
            const int spaceLeft = static_cast<int> (kScratchSize) - totalIn;
            if (spaceLeft <= 0)
                break;

            plaits::Voice::Frame frames[1];
            voice_->Render (patch, modOff, frames, 1);
            scratchOut_[totalIn] = static_cast<float> (frames[0].out) * int16Scale;
            scratchAux_[totalIn] = static_cast<float> (frames[0].aux) * int16Scale;
            totalIn++;
        }
    }

    while (static_cast<double> (totalIn) < neededIn)
    {
        // Room left in the scratch buffers?
        const int spaceLeft = static_cast<int> (kScratchSize) - totalIn;
        if (spaceLeft <= 0)
            break;   // shouldn't happen with kScratchSize=8192

        const int toGen = static_cast<int> (
            std::min (static_cast<size_t> (spaceLeft),
                      static_cast<size_t> (plaits::kMaxBlockSize)));

        // ---- Ramped parameter values for this chunk ------------------------
        // Each chunk advances the value by (toGen / neededIn) of the total
        // delta, starting from the value at the beginning of the ramp (the
        // previous render() call's value).  rampGenerated tracks how many
        // samples have been generated this call, so consecutive chunks (and
        // consecutive render() calls) ramp continuously across the host block.
        {
            const float frac =
                std::min (1.0f, (rampGenerated + toGen) / static_cast<float> (neededIn));
            const float h = rampHarmStart  + rampHarmDelta  * frac;
            const float t = rampTimbStart  + rampTimbDelta  * frac;
            const float m = rampMorphStart + rampMorphDelta * frac;
            const float p = rampPitchStart + rampPitchDelta * frac;

            patch.harmonics = h < 0.0f ? 0.0f : (h > 1.0f ? 1.0f : h);
            patch.timbre    = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
            patch.morph     = m < 0.0f ? 0.0f : (m > 1.0f ? 1.0f : m);
            patch.note      = static_cast<float> (midiNote_) + p;
        }
        rampGenerated += toGen;

        // Call Plaits.  Voice::Render writes toFrame (short out, short aux).
        plaits::Voice::Frame frames[plaits::kMaxBlockSize];
        voice_->Render (patch, mod, frames, static_cast<size_t> (toGen));

        // Convert int16 → float and append to scratch buffers.
        for (int i = 0; i < toGen; ++i)
        {
            const int idx = totalIn + i;
            scratchOut_[idx] = static_cast<float> (frames[i].out) * int16Scale;
            scratchAux_[idx] = static_cast<float> (frames[i].aux) * int16Scale;
        }
        totalIn += toGen;
    }

    // Record the trigger value actually sent to Plaits on this render call.
    // Used by noteOn() to detect the voice-stealing case (noteOff → noteOn
    // with no render between them), where lastTriggerHigh_ is still true
    // from the previous render even though noteOn_ is now false.
    lastTriggerHigh_ = (mod.trigger > 0.1f);

    // ---- Update the ramp "previous" values --------------------------------
    // The value used for the LAST chunk of this call is the ramp start
    // advanced by (totalIn - initialTotalIn) / neededIn of the delta.  Store
    // that as the new prev* so the NEXT call ramps from exactly where this
    // one ended (continuous ramp across host blocks).  If no samples were
    // generated (e.g. scratch buffer already full) the values are unchanged.
    //
    // rampGenerated holds the number of samples generated this call, so:
    const float fracEnd = std::min (1.0f, rampGenerated / static_cast<float> (neededIn));
    prevHarmonics_ = rampHarmStart  + rampHarmDelta  * fracEnd;
    prevTimbre_    = rampTimbStart  + rampTimbDelta  * fracEnd;
    prevMorph_     = rampMorphStart + rampMorphDelta * fracEnd;
    prevPitch_     = rampPitchStart + rampPitchDelta * fracEnd;
    // Clamp the stored prev values to the same range as the patch fields
    // (harmonics / timbre / morph 0..1; pitch is unbounded but we clamp the
    // stored value to the target to avoid drift).
    if (prevHarmonics_ < 0.0f) prevHarmonics_ = 0.0f; else if (prevHarmonics_ > 1.0f) prevHarmonics_ = 1.0f;
    if (prevTimbre_    < 0.0f) prevTimbre_    = 0.0f; else if (prevTimbre_    > 1.0f) prevTimbre_    = 1.0f;
    if (prevMorph_     < 0.0f) prevMorph_     = 0.0f; else if (prevMorph_     > 1.0f) prevMorph_     = 1.0f;

    // ---- Step 2: Resample to host rate ------------------------------------
    //
    // The LinearResampler wraps juce::Interpolators::Linear, which is
    // STATEFUL: it tracks subSamplePos, indexBuffer, and lastInputSamples[2]
    // across process() calls.  We must NOT call reset() here; the fractional
    // position and the last two input samples are carried forward
    // automatically, giving a continuous output stream.
    //
    // process() returns the number of input samples actually consumed.
    // We feed it totalIn samples and request exactly numSamples output
    // samples.  If totalIn is slightly more than needed (by at most 2
    // samples), the extras are simply not consumed and will be available
    // in the scratch buffer for the next call's resample step.
    //
    // With wrapAround = 0 the interpolator feeds zeroes if it ever needs
    // more input than totalIn (shouldn't happen given our +2 margin).

    const int outUsed = resamplerOut_.process (scratchOut_, totalIn, out, numSamples);
    const int auxUsed = (aux != nullptr)
                        ? resamplerAux_.process (scratchAux_, totalIn, aux, numSamples)
                        : 0;

#if JUCE_DEBUG
    // Both resamplers run the same ratio over the same input samples, so the
    // consumed counts must always be equal.  If this ever fires, the two
    // channels have drifted and the single scratchCount_ can no longer track
    // both of them — the invariant this class depends on is broken.
    jassert (aux == nullptr || auxUsed == outUsed);
#endif

    // Move unconsumed input samples to the front of the scratch buffers for the
    // next render() call.  The interpolator's internal state (subSamplePos,
    // lastInputSamples) is already correct; we just keep the raw input aligned
    // so the next call's resampler can find the samples it still needs.
    //
    // outUsed and auxUsed are always equal (debug assert above), so there is a
    // single carry-over count that is correct for BOTH channels.  In practice,
    // with Linear (memorySize=2) the interpolator only ever needs at most 1
    // extra input sample beyond (numSamples * ratio_); our +2 margin in neededIn
    // guarantees totalIn is always sufficient, so outUsed == totalIn in almost
    // all cases.  If not, we shift.
    if (outUsed < totalIn)
    {
        const int remaining = totalIn - outUsed;
        std::memmove (scratchOut_, scratchOut_ + outUsed, remaining * sizeof (float));
        if (aux != nullptr)
            std::memmove (scratchAux_, scratchAux_ + outUsed, remaining * sizeof (float));
    }

    scratchCount_ = (outUsed < totalIn) ? (totalIn - outUsed) : 0;
}

