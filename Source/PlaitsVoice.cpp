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

    // Build the Patch and Modulations once per render() call (they don't
    // change within a block of samples for a single voice).
    plaits::Patch patch;
    patch.note                      = static_cast<float> (midiNote_) + pitchOffset_;
    patch.harmonics                 = harmonics_;
    patch.timbre                    = timbre_;
    patch.morph                     = morph_;
    patch.frequency_modulation_amount = 0.0f;
    patch.timbre_modulation_amount    = 0.0f;
    patch.morph_modulation_amount     = 0.0f;
    patch.engine                    = engineIndex_;
    patch.decay                     = 0.5f;   // mid-range LPG decay
    patch.lpg_colour                = 0.0f;   // no HF bleed

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

    // Move unconsumed input samples to the front of the scratch buffers
    // for the next render() call.  The interpolator's internal state
    // (subSamplePos, lastInputSamples) is already correct; we just need
    // to keep the raw input aligned so the next call's resampler can
    // find any samples it still needs.
    //
    // In practice, with Linear (memorySize=2) the interpolator only ever
    // needs at most 1 extra input sample beyond (numSamples * ratio_).
    // Our +2 margin in neededIn guarantees totalIn is always sufficient,
    // so outUsed == totalIn in almost all cases.  If not, we shift.
    if (outUsed < totalIn)
    {
        const int remaining = totalIn - outUsed;
        std::memmove (scratchOut_, scratchOut_ + outUsed,
                      remaining * sizeof (float));
    }
    if (aux != nullptr && auxUsed < totalIn)
    {
        const int remaining = totalIn - auxUsed;
        std::memmove (scratchAux_, scratchAux_ + auxUsed,
                      remaining * sizeof (float));
    }

    scratchCount_ = (outUsed < totalIn) ? (totalIn - outUsed)
                   : (aux != nullptr && auxUsed < totalIn) ? (totalIn - auxUsed)
                   : 0;
}

