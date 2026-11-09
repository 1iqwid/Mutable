#pragma once

#include <cstddef>

#include <juce_audio_basics/juce_audio_basics.h>
#include "stmlib/utils/buffer_allocator.h"

// Forward declaration – no Plaits headers needed in the public header.
namespace plaits { class Voice; }

//==============================================================================
/// Small stateful linear-interpolation resampler (one per channel).
///
/// Wraps juce::Interpolators::Linear (GenericInterpolator<LinearTraits, 2>)
/// so it can be swapped for a higher-quality interpolator later without
/// touching PlaitsVoice.
///
/// speedRatio = 48000 / hostRate  (input samples per output sample).
/// No allocation, no locks, no file I/O.
class LinearResampler
{
public:
    void reset (double hostSampleRate)
    {
        ratio_ = 48000.0 / hostSampleRate;
        interpolator_.reset();
    }

    /// Resamples to produce exactly numOutput output samples.
    /// in must have at least numInput samples.
    /// Returns the number of input samples actually consumed (≤ numInput).
    int process (const float* in, int numInput, float* out, int numOutput) noexcept
    {
        // 6-arg overload: wrapAround=0 → feed zeroes if input runs out.
        // Returns the number of input samples consumed.
        return interpolator_.process (ratio_, in, out, numOutput, numInput, 0);
    }

private:
    juce::Interpolators::Linear interpolator_;
    double ratio_ = 1.0;
};

//==============================================================================
/// Thin wrapper around a single plaits::Voice.
///
/// The Voice is heap-allocated (it is very large and DISALLOW_COPY_AND_ASSIGN)
/// and must never be put on the stack.  All setup happens in prepare();
/// the audio thread only calls render().
///
/// Plaits runs natively at 48 kHz.  render() converts the int16 Frame
/// output to float, then resamples to the host sample rate using the
/// LinearResampler members (one per channel).
///
/// Trigger / level mapping (see third_party/plaits/dsp/voice.cc):
///   trigger_patched = true  → engine gets RISING_EDGE on noteOn,
///                             HIGH while held, LOW after noteOff.
///   level_patched   = false → accent = 0.8 (Plaits default),
///                             LPG uses ProcessPing (ramp-up then decay),
///                             giving the classic "play, hold, decay"
///                             envelope.  Our own ADSR is added in Phase 3.
///
/// No allocation, locks, or file I/O occurs inside render().
class PlaitsVoice
{
public:
    static constexpr size_t kPlaitsSampleRate   = 48000;
    static constexpr size_t kMaxPlaitsBlockSize = 24;       // plaits::kMaxBlockSize
    static constexpr size_t kEngineBufferSize   = 16 * 1024; // matches firmware

    PlaitsVoice();
    ~PlaitsVoice();

    /// Must be called on a non-audio thread before the first render().
    /// Creates/initialises the plaits::Voice and the 16 KB BufferAllocator,
    /// and configures the resamplers for the given host sample rate.
    /// Calling prepare() again re-initialises everything (safe).
    void prepare (double hostSampleRate);

    // Safe to call from the MIDI / message thread.
    void setEngine (int engineIndex);         // 0..23 (clamped)
    void setParams (float harmonics,          // 0..1 (clamped)
                    float timbre,             // 0..1 (clamped)
                    float morph);             // 0..1 (clamped)
    void noteOn  (int midiNote, float velocity);   // velocity 0..1
    void noteOff ();

    /// External level control (0..1).  When useExternalLevel_ is true,
    /// this value is passed as mod.level with level_patched=true,
    /// overriding the velocity-based accent.  Default is false, in which
    /// case the Plaits default accent (0.8) is used.
    void setLevel (float level);
    void setUseExternalLevel (bool enabled) { useExternalLevel_ = enabled; }

    /// Renders numSamples of float audio at the host sample rate.
    /// out and aux must each point to at least numSamples floats.
    /// aux may be nullptr if you don't need the aux channel.
    /// No allocation, locks, or file I/O occurs inside this call.
    void render (float* out, float* aux, int numSamples);

private:
    // 16 KB shared memory pool for all 24 Plaits engines
    // (matches the firmware: char shared_buffer[16384] in plaits.cc).
    char                    engineBuffer_[kEngineBufferSize];
    stmlib::BufferAllocator allocator_;
    plaits::Voice*          voice_ = nullptr;

    // Parameters (written from message thread, read from audio thread).
    int   engineIndex_ = 8;    // VirtualAnalog (index 8 in the engine list)
    float harmonics_   = 0.5f;
    float timbre_      = 0.5f;
    float morph_       = 0.5f;

    // Note state.
    bool  noteOn_   = false;
    int   midiNote_ = 60;
    float velocity_ = 1.0f;

    // External level control (Phase 2b follow-up).
    bool  useExternalLevel_ = false;
    float level_            = 0.8f;   // default accent when useExternalLevel_ is true

    // Resampler configuration (set once in prepare()).
    double hostRate_ = 44100.0;
    double ratio_    = 48000.0 / 44100.0;   // 48 kHz samples per host sample

    // Stateful resamplers (one per channel), persist across render() calls.
    // Do NOT reset these inside render(); the interpolator tracks its own
    // subSamplePos / lastInputSamples internally and carries the fractional
    // position forward automatically.  Only reset in prepare().
    LinearResampler resamplerOut_;
    LinearResampler resamplerAux_;

    // Scratch buffers for 48 kHz float samples generated by Plaits,
    // fed into the resamplers.
    // 8192 samples ≈ 170 ms at 48 kHz – enough for very large DAW blocks.
    static constexpr size_t kScratchSize = 8192;
    float scratchOut_[kScratchSize];
    float scratchAux_[kScratchSize];
    int   scratchCount_ = 0;   // samples currently in scratch buffers

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PlaitsVoice)
};
