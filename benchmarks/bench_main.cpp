// ==============================================================================
//  Mutable — CPU benchmark: 8 voices × 24 engines
//
//  Build (Windows, from the project root):
//      cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DMUTABLE_BUILD_TESTS=ON
//      cmake --build build --config Release --target MutableBench
//
//  Run:  build\benchmarks\Release\MutableBench.exe
//
//  8 PlaitsVoice instances, each on a sustained note (MIDI 60-67),
//  level 1.0, external-level mode.  10 s rendered in 512-sample blocks
//  at 48 kHz.  Wall-clock via std::chrono::steady_clock.  NaN/inf check.
// ==============================================================================

#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>

#include "PlaitsVoice.h"

static constexpr int        kNumVoices        = 8;
static constexpr int        kNumEngines       = 24;
static constexpr int        kBlockSize        = 512;
static constexpr double     kHostRate         = 48000.0;
static constexpr double     kRenderSeconds    = 10.0;
static constexpr int        kWarmupBlocks     = 4;
static constexpr float      kLevel            = 1.0f;
static constexpr float      kHarmonics        = 0.5f;
static constexpr float      kTimbre           = 0.5f;
static constexpr float      kMorph            = 0.5f;
static constexpr int        kFirstMidiNote    = 60;
static constexpr float      kHeavyThreshold   = 0.25f;

static bool containsNonFinite (const float* buf, int n)
{
    for (int i = 0; i < n; ++i)
        if (!std::isfinite (buf[i]))
            return true;
    return false;
}

static double benchEngine (int engineIndex, std::string& errMsg)
{
    PlaitsVoice voices[kNumVoices];
    for (auto& v : voices)
        v.prepare (kHostRate);

    for (int i = 0; i < kNumVoices; ++i)
    {
        voices[i].setEngine (engineIndex);
        voices[i].setParams (kHarmonics, kTimbre, kMorph);
        voices[i].setUseExternalLevel (true);
        voices[i].setLevel (kLevel);
        voices[i].noteOn (kFirstMidiNote + i, 1.0f);
    }

    const int totalSamples = static_cast<int> (kRenderSeconds * kHostRate);
    const int totalBlocks  = (totalSamples + kBlockSize - 1) / kBlockSize;
    float out[kBlockSize];
    float aux[kBlockSize];

    for (int w = 0; w < kWarmupBlocks; ++w)
        for (auto& v : voices)
            v.render (out, aux, kBlockSize);

    for (int i = 0; i < kNumVoices; ++i)
    {
        voices[i].noteOff();
        voices[i].noteOn (kFirstMidiNote + i, 1.0f);
    }

    const auto t0 = std::chrono::steady_clock::now();
    bool bad = false;
    for (int b = 0; b < totalBlocks; ++b)
    {
        for (auto& v : voices)
        {
            v.render (out, aux, kBlockSize);
            if (!bad)
            {
                if (containsNonFinite (out, kBlockSize)) { bad = true; errMsg = "NaN/inf in out[]"; }
                if (containsNonFinite (aux, kBlockSize)) { bad = true; errMsg = "NaN/inf in aux[]"; }
            }
        }
    }
    const auto t1 = std::chrono::steady_clock::now();

    if (bad)
        return -1.0;

    const double elapsedSec = std::chrono::duration<double> (t1 - t0).count();
    return elapsedSec * 1000.0 / kRenderSeconds;
}

int main (int, char**)
{
    std::printf ("================================================================\n");
    std::printf (" Mutable CPU Benchmark — 8 voices, %g kHz, %g s render\n",
                 kHostRate / 1000.0, kRenderSeconds);
    std::printf (" Block size: %d samples\n", kBlockSize);
    std::printf ("================================================================\n\n");

    struct Row { int engine; double ms; bool ok; } rows[kNumEngines];
    int failures = 0, heaviest = -1, heavyCount = 0;
    double heaviestVal = 0.0;

    for (int e = 0; e < kNumEngines; ++e)
    {
        std::string errMsg;
        double v = benchEngine (e, errMsg);
        rows[e] = { e, v, v >= 0.0 };
        if (!rows[e].ok)
        { ++failures; std::printf ("  Engine %2d : ERROR — %s\n", e, errMsg.c_str()); }
        if (v >= 0.0 && v > heaviestVal) { heaviestVal = v; heaviest = e; }
    }

    std::printf ("\n%-10s %14s %14s %8s\n", "Engine", "ms/s (1 core)", "% of 1 core", "Flag");
    std::printf ("%-10s %14s %14s %8s\n", "------", "------------", "-----------", "----");

    for (int e = 0; e < kNumEngines; ++e)
    {
        if (!rows[e].ok) continue;
        const double pct = rows[e].ms;
        const bool heavy = (pct / 1000.0) >= kHeavyThreshold;
        if (heavy) ++heavyCount;
        std::printf ("%10d %14.3f %13.2f%% %8s\n", e, pct, pct / 1000.0, heavy ? "HEAVY" : "");
    }

    std::printf ("\n");
    if (heaviest >= 0)
        std::printf ("Heaviest engine: %d  (%.3f ms/s  =  %.2f%% of one core)\n",
                     heaviest, heaviestVal, heaviestVal / 1000.0);

    std::printf ("Engines above %.0f%% of one core: ", kHeavyThreshold * 100.0);
    bool first = true;
    for (int e = 0; e < kNumEngines; ++e)
        if (rows[e].ok && (rows[e].ms / 1000.0) >= kHeavyThreshold)
        { if (!first) std::printf (", "); std::printf ("%d", e); first = false; }
    if (first) std::printf ("none");
    std::printf ("\n");

    if (failures > 0)
    { std::printf ("\n%d engine(s) failed (NaN/inf detected).\n", failures); return 1; }

    std::printf ("\nAll %d engines produced finite output.\n", kNumEngines);
    return 0;
}
