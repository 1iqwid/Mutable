#pragma once

#include <array>
#include <atomic>
#include <cstdint>

//==============================================================================
/// Phase 5: lock-free single-producer / single-consumer ring buffer for the
/// oscilloscope.  The audio thread pushes samples; the UI thread copies the most
/// recent ones on a timer.  Every slot is a std::atomic<float> (relaxed), so
/// there is no data race even when the writer laps the reader; the worst case
/// is a visually harmless mix of old and new samples in one frame.
/// No allocation, no locks.
///
/// 5c: two channels written together (shared write index):
///   kOut = the Plaits Out signal summed over voices, kAux = the Aux signal
///   (after Aux Level), both before the Out/Aux Mix and after the envelope,
///   voice gain and Out Gain.
class ScopeBuffer
{
public:
    static constexpr int kSize = 16384;   // per channel, power of two
    static_assert ((kSize & (kSize - 1)) == 0, "kSize must be a power of two");

    enum Channel { kOut = 0, kAux = 1, kNumChannels = 2 };

    /// Audio thread.
    void push (const float* out, const float* aux, int numSamples) noexcept
    {
        auto w = writePos_.load (std::memory_order_relaxed);
        for (int i = 0; i < numSamples; ++i)
        {
            const auto idx = (w + (uint32_t) i) & kMask;
            data_[kOut][idx].store (out[i], std::memory_order_relaxed);
            data_[kAux][idx].store (aux[i], std::memory_order_relaxed);
        }
        writePos_.store (w + (uint32_t) numSamples, std::memory_order_release);
    }

    /// Audio thread: the host sample rate, so the UI can convert ms to samples.
    void setSampleRate (double sr) noexcept { sampleRate_.store (sr, std::memory_order_relaxed); }

    /// UI thread: copies the newest `num` samples (oldest first) of both
    /// channels, from the same write position.  num is clamped to kSize.
    int readLatest (float* outDest, float* auxDest, int num) const noexcept
    {
        if (num > kSize) num = kSize;
        const auto end = writePos_.load (std::memory_order_acquire);
        const auto start = end - (uint32_t) num;
        for (int i = 0; i < num; ++i)
        {
            const auto idx = (start + (uint32_t) i) & kMask;
            outDest[i] = data_[kOut][idx].load (std::memory_order_relaxed);
            auxDest[i] = data_[kAux][idx].load (std::memory_order_relaxed);
        }
        return num;
    }

    double getSampleRate() const noexcept { return sampleRate_.load (std::memory_order_relaxed); }

private:
    static constexpr uint32_t kMask = (uint32_t) kSize - 1;

    std::array<std::array<std::atomic<float>, kSize>, kNumChannels> data_ {};
    std::atomic<uint32_t> writePos_ { 0 };
    std::atomic<double> sampleRate_ { 48000.0 };
};
