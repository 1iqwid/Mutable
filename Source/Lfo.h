// Lfo.h — Task 4c-2a
//
// A tiny, allocation-free, lock-free LFO for the audio thread.
//
//   - No heap allocation, no locks, no std::rand / std::mt19937 / <random>.
//     The S&H generator is a plain xorshift32 with a deterministic seed.
//   - Designed to be called once per audio block (advance(numSamples)),
//     NOT once per sample.
//
// Shapes (setShape):
//   0  Sine          sin(2π·phase)
//   1  Triangle      4·|phase - 0.5| - 1
//   2  Saw Up        2·phase - 1
//   3  Saw Down      1 - 2·phase
//   4  Square        -1 for phase<0.5, +1 otherwise
//   5  S&H           holds a random value in [-1, +1]; redraws only when
//                    the phase wraps past 1.0
//
// All values are bipolar in [-1.0, +1.0].

#pragma once

#include <cmath>
#include <cstdint>

namespace mutable_plaits
{

class Lfo
{
public:
    Lfo() = default;

    //==========================================================================
    // prepare(sampleRate) — call on the audio thread before the first block.
    //
    //   - Stores the sample rate used for phase accumulation.
    //   - Resets phase to 0.
    //   - Seeds the S&H xorshift32 generator (non-zero golden-ratio seed).
    //   - Picks the first S&H value (so shape 5 returns a defined value
    //     even before the first phase wrap).
    void prepare (double sampleRate)
    {
        sampleRate_ = (sampleRate > 0.0) ? sampleRate : 48000.0;
        phase_      = 0.0;
        rate_       = 1.0;
        shape_      = 0;
        shSeed_     = 0x9E3779B9u;          // non-zero xorshift32 state
        shValue_    = toBipolar (nextShState());
    }

    //==========================================================================
    // reset(seed) — deterministic re-seed of the S&H generator.
    //
    //   - phase = 0.
    //   - If seed == 0, falls back to 0x9E3779B9u (xorshift32 requires
    //     a non-zero state; 0 is the absorbing state and would produce
    //     all-zeros forever).
    //   - Picks the first S&H value immediately.
    void reset (uint32_t seed)
    {
        phase_   = 0.0;
        shSeed_  = (seed != 0u) ? seed : 0x9E3779B9u;
        shValue_ = toBipolar (nextShState());
    }

    //==========================================================================
    // setRate(hz) — set the LFO frequency in Hz.  Takes effect on the next
    // advance() call.
    void setRate (float hz)
    {
        rate_ = (hz > 0.0f) ? hz : 0.0f;
    }

    //==========================================================================
    // setShape(shape) — select waveform shape.
    //   0=Sine, 1=Triangle, 2=Saw Up, 3=Saw Down, 4=Square, 5=S&H.
    //   Values outside 0..5 are clamped.
    void setShape (int shape)
    {
        shape_ = (shape < 0) ? 0 : (shape > 5) ? 5 : shape;
    }

    //==========================================================================
    // advance(numSamples) — advance the LFO by numSamples and return the
    // bipolar value at the new phase.
    //
    //   phase += rate * numSamples / sampleRate
    //   phase wraps into [0, 1).
    //   If the phase wrapped (crossed an integer boundary), a new S&H value
    //   is drawn (used only when shape_ == 5).
    float advance (int numSamples)
    {
        if (numSamples <= 0)
            return sampleValue (phase_);

        // Accumulate phase in double for precision, then wrap to [0, 1).
        const double delta =
            static_cast<double> (rate_)
            * static_cast<double> (numSamples)
            / sampleRate_;

        double newPhase = phase_ + delta;

        // Wrap into [0.0, 1.0) via std::floor (keeps the math total even
        // for the impossible negative-delta case).
        const double wrap = std::floor (newPhase);
        newPhase -= wrap;

        // Draw a new S&H value if the phase wrapped (>= 1 full cycle elapsed).
        if (wrap >= 1.0)
            shValue_ = toBipolar (nextShState());

        phase_ = newPhase;
        return sampleValue (newPhase);
    }

private:
    //==========================================================================
    // xorshift32 — deterministic, no external dependencies.  Advances the
    // state and returns the new state (non-zero if seeded non-zero).
    uint32_t nextShState()
    {
        uint32_t x = shSeed_;
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        shSeed_ = x;
        return x;
    }

    // Convert a raw xorshift32 word to bipolar [-1.0, +1.0).
    static float toBipolar (uint32_t raw)
    {
        // Upper 31 bits → uniform [0, 2^31); scale to [-1, +1).
        const uint32_t u = raw & 0x7FFFFFFFu;
        return static_cast<float> (static_cast<double> (u) * (2.0 / 2147483648.0) - 1.0);
    }

    //==========================================================================
    // Sample the current shape at the given phase in [0, 1).
    float sampleValue (double phase) const
    {
        switch (shape_)
        {
            case 0: // Sine
                return static_cast<float> (std::sin (6.283185307179586 * phase));

            case 1: // Triangle
                return static_cast<float> (4.0 * std::abs (phase - 0.5) - 1.0);

            case 2: // Saw Up
                return static_cast<float> (2.0 * phase - 1.0);

            case 3: // Saw Down
                return static_cast<float> (1.0 - 2.0 * phase);

            case 4: // Square
                return (phase >= 0.5) ? 1.0f : -1.0f;

            case 5: // S&H
            default:
                return shValue_;
        }
    }

    //==========================================================================
    double sampleRate_ = 48000.0;   // set in prepare()
    double phase_      = 0.0;       // current phase in [0, 1)
    double rate_       = 1.0;       // LFO frequency in Hz
    int    shape_      = 0;         // 0..5, set in setShape()

    // S&H state (xorshift32 state + current value)
    uint32_t shSeed_   = 0x9E3779B9u;
    float    shValue_  = 0.0f;
};

} // namespace mutable_plaits
