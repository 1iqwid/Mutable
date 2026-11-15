#pragma once

// EngineInfo.h - Task 4e
//
// UI-only data: display name and per-engine meaning of Harmonics / Timbre /
// Morph for each of the 24 Plaits engines, in plaits::Voice registration order
// (third_party/plaits/dsp/voice.cc).  Meanings were checked against how each
// engine's Render() uses parameters.harmonics / timbre / morph
// (third_party/plaits/dsp/engine/*.cc and engine2/*.cc).
//
// Note: our trigger is always patched, so Chiptune runs in arpeggiator mode
// (Timbre selects the arp pattern; one note steps per trigger).

namespace engine_info
{

struct Engine
{
    const char* name;
    const char* harmonics;
    const char* timbre;
    const char* morph;
};

inline constexpr int kNumEngines = 24;

inline constexpr Engine kEngines[kNumEngines] =
{
    { "VA + VCF",           "Resonance / mode", "Cutoff",          "Waveshape"     },  //  0
    { "Phase Distortion",   "Mod frequency",    "Amount",          "Asymmetry"     },  //  1
    { "6-Op FM A",          "Patch",            "Brightness",      "Envelope"      },  //  2
    { "6-Op FM B",          "Patch",            "Brightness",      "Envelope"      },  //  3
    { "6-Op FM C",          "Patch",            "Brightness",      "Envelope"      },  //  4
    { "Wave Terrain",       "Terrain",          "Path radius",     "Path offset"   },  //  5
    { "String Machine",     "Chord",            "Filter / chorus", "Registration"  },  //  6
    { "Chiptune",           "Chord",            "Arp pattern",     "Waveshape"     },  //  7
    { "Virtual Analog",     "Detune",           "Square shape",    "Saw shape"     },  //  8
    { "Waveshaping",        "Shaper wave",      "Fold amount",     "Asymmetry"     },  //  9
    { "2-Op FM",            "Ratio",            "Mod index",       "Feedback"      },  // 10
    { "Granular Formant",   "Formant ratio",    "Formant freq",    "Formant width" },  // 11
    { "Additive",           "Bumps",            "Main harmonic",   "Bump shape"    },  // 12
    { "Wavetable",          "Bank",             "Row",             "Column"        },  // 13
    { "Chords",             "Chord type",       "Inversion",       "Waveform"      },  // 14
    { "Speech",             "Synth / bank",     "Species",         "Phoneme / word"},  // 15
    { "Swarm",              "Pitch spread",     "Density",         "Grain size"    },  // 16
    { "Filtered Noise",     "Filter type",      "Clock freq",      "Resonance"     },  // 17
    { "Particle",           "Freq spread",      "Density",         "Filter type"   },  // 18
    { "Inharmonic String",  "Inharmonicity",    "Brightness",      "Decay"         },  // 19
    { "Modal Resonator",    "Material",         "Brightness",      "Decay"         },  // 20
    { "Bass Drum",          "Attack / drive",   "Brightness",      "Decay"         },  // 21
    { "Snare Drum",         "Noise balance",    "Mode balance",    "Decay"         },  // 22
    { "Hi-Hat",             "Metal / noise",    "HP cutoff",       "Decay"         },  // 23
};

} // namespace engine_info
