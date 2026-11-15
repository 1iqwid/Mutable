#pragma once

#include <atomic>

//==============================================================================
/// Phase 5e: per-voice envelope position, audio -> UI.  One per voice, owned by
/// the processor.  The voice writes all three fields (relaxed) at the end of
/// each renderNextBlock and when it stops; the envelope view reads them on its
/// timer.  Fields may be from different blocks for one frame, which is
/// visually harmless.  Lock-free, no allocation.
struct VoiceDisplayState
{
    enum Stage : int { kIdle = 0, kAttack, kDecay, kSustain, kRelease };

    std::atomic<int>   stage    { kIdle };
    std::atomic<float> level    { 0.0f };   // ADSR value 0..1
    std::atomic<float> progress { 0.0f };   // 0..1 through the current stage
    // Release stage only: the level at note-off, and the stage / progress the
    // voice was in at that moment (where the key was let go on the curve).
    std::atomic<float> releaseStart  { 0.0f };
    std::atomic<int>   releaseFromStage    { kIdle };
    std::atomic<float> releaseFromProgress { 0.0f };

    void publish (int s, float lv, float pr) noexcept
    {
        level.store (lv, std::memory_order_relaxed);
        progress.store (pr, std::memory_order_relaxed);
        stage.store (s, std::memory_order_relaxed);
    }

    void publishRelease (float lv, float pr, float relStart, int fromStage, float fromProgress) noexcept
    {
        releaseStart.store (relStart, std::memory_order_relaxed);
        releaseFromStage.store (fromStage, std::memory_order_relaxed);
        releaseFromProgress.store (fromProgress, std::memory_order_relaxed);
        publish (kRelease, lv, pr);
    }
};
