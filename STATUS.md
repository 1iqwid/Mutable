# STATUS

## Current state

**Phase 1 — CMake + JUCE scaffold (COMPLETE, v0.1).**
CMake project, JUCE 8 via FetchContent. VST3 (all platforms), AU (macOS only).
Output-only stereo bus. `NEEDS_MIDI_INPUT TRUE` in juce_add_plugin (fixes Ableton
Live "no valid event input bus" load failure). Passes pluginval at strictness 5.

**Phase 2 — Plaits integration, single voice (COMPLETE).**
- CMake: `stmlib` and `plaits_dsp` static targets; plugin links `plaits_dsp`.
  Include root is `third_party/`. `plaits_dsp` = 37 .cc files from
  `third_party/plaits/dsp/**` + `resources.cc`. Compiled with `-DTEST` so
  `plaits/user_data.h` uses mock flash API. Firmware-only files excluded.
  stmlib: 3 .cc files (dsp/atan.cc, dsp/units.cc, utils/random.cc); system/ excluded.
- MSVC build fixes — see `third_party/PATCHES.md`:
  - `stmlib` target needs `TEST` defined (ARM inline-asm in dsp.h is in #else of #ifdef TEST).
  - stmlib/utils/dsp.h:38-84 `__attribute__((always_inline))` → STMLIB_ALWAYS_INLINE macro.
  - plaits/resources.cc:4032 zero-length array → `{ nullptr }`.
- PlaitsVoice wrapper (`Source/PlaitsVoice.{h,cpp}`):
  - Heap-allocated `plaits::Voice` + 16 KB char buffer + BufferAllocator, created in prepare().
  - Public API: `prepare(double)`, `setEngine(int 0..23)`, `setParams(harmonics, timbre, morph)`,
    `noteOn(midiNote, velocity)`, `noteOff()`, `setLevel(float)`,
    `setUseExternalLevel(bool)`, `render(out, aux, numSamples)`.
  - render(): chunks of kMaxBlockSize (24) at 48 kHz → float → LinearResampler to host rate.
  - LinearResampler uses `juce::Interpolators::Linear` (stateful — carries subSamplePos +
    lastInputSamples[2] across calls). Per-call reset() removed (was causing click at block
    boundaries). `process()` returns samples consumed; `render()` memmoves unconsumed to front.
  - Trigger: `trigger_patched=true` (RISING_EDGE/HIGH/LOW).
  - Level: `level_patched=false` by default (accent=velocity, LPG uses ProcessPing).
    When external level enabled: `level_patched=true`, LPG u

- PlaitsVoice wrapper (Source/PlaitsVoice.{h,cpp}):
  - Heap-allocated plaits::Voice (large, DISALLOW_COPY_AND_ASSIGN) + 16 KB char
    buffer + BufferAllocator, created in prepare() (never on the audio thread).
  - Public API: prepare(double), setEngine(int 0..23), setParams(harmonics,
    timbre, morph), noteOn(midiNote, velocity), noteOff(), setLevel(float),
    setUseExternalLevel(bool), render(out, aux, numSamples).
  - render() runs Plaits in chunks of kMaxBlockSize (24) at 48 kHz, converts
    int16 Frame out/aux to float, then resamples to the host rate.
  - Resampler: small LinearResampler class using juce::Interpolators::Linear,
    one per channel. The JUCE GenericInterpolator IS stateful (tracks
    subSamplePos and lastInputSamples[2] across calls); the per-call reset()
    that destroyed the fractional position (causing a click at every block
    boundary) was removed — state now carries forward automatically.
    LinearResampler::process() returns the number of input samples consumed
    (per JUCE API); render() memmoves unconsumed samples to the front of the
    scratch buffer for the next call.
  - Trigger/level: trigger_patched=true (RISING_EDGE/HIGH/LOW);
    level_patched=false by default (accent=velocity, LPG uses ProcessPing →
    play, hold, decay). setLevel(float) + setUseExternalLevel(bool): when
    external level is enabled, level_patched=true and the LPG uses Process (no
    built-in decay), so the ADSR can drive release.
  - No allocation, locks, or file I/O in render().
  - Retrig bug fix: Plaits' Voice::Render() latches trigger_state_ high after
    the first note. A second noteOn with trigger held high never produces
    TRIGGER_RISING_EDGE, so the engine is never retriggered. PlaitsVoice now has
    a retrigger_ flag (PlaitsVoice.h:119-123): noteOn() while already triggered
    sets retrigger_=true. render() checks retrigger_ and, if set, renders
    5 samples with trigger=0.0f to flush the 5-slot trigger delay line, then the
    normal loop continues with trigger=1.0f → clean false→true rising edge →
    TRIGGER_RISING_EDGE (PlaitsVoice.cpp:161-193). NOTE: does NOT yet fix
    noteOff→noteOn (release then retrigger) because the trigger delay line still
    holds the previous trigger=1.0f values. A full fix would require noteOff() to
    set a flag causing render() to write trigger=0.0f for 5 samples before the
    next noteOn's trigger=1.0f. See the retrigger_ flag logic for the pattern.

- Wired into PluginProcessor: single voice, last-note priority (polyphony in
  Phase 3). AudioProcessorValueTreeState with stable string IDs:
  engine (AudioParameterInt 0..23, default 8), harmonics/timbre/morph
  (AudioParameterFloat 0..1, default 0.5), outGain (AudioParameterFloat 0..1,
  default 0.7). Constructor uses the JUCE 8 AudioProcessorValueTreeState
  (AudioProcessor&, UndoManager*) form. State save/restore via APVTS:
  getStateInformation uses copyState() (thread-safe) → createXml() → toString();
  setStateInformation uses XmlDocument::parse → ValueTree::fromXml →
  replaceState().
  - processBlock: ScopedNoDenormals, buffer.clear(), reads the 5 parameters ONCE
    per block via cached RangedAudioParameter* raw pointers (no string lookups
    on the audio thread). Skips redundant setEngine() via lastEngineSent_.
    Iterates the MIDI buffer and splits rendering at each event's samplePosition
    so note on/off is sample-accurate; a NoteOn with velocity 0 counts as
    noteOff. Renders mono into channel 0, scales by outGain, copies to the other
    channel(s).
  - Before the voice is prepared (voice_==nullptr), render() outputs silence
    instead of crashing (guarded in PlaitsVoice::render).
  - createEditor returns juce::GenericAudioProcessorEditor, marked TEMPORARY
    in a comment (custom UI replaces it in Phase 6).
  - Resampler margin check: render() generates neededIn =
    numSamples*ratio_ + 2 input samples (PlaitsVoice.cpp:103) and feeds the
    JUCE Interpolators::Linear with wrapAround=0. The Linear interpolator reads
    at most 1 input sample BEYOND the final output sample (subSamplePos starts
    < 1, memorySize=2), so the +2 margin is sufficient.
  - Tuning check: patch.note is passed the raw MIDI number (PlaitsVoice.cpp:109).
    Plaits engine.h NoteToFrequency(n) = a0*0.25f*SemitonesToRatio(n-9.0f), with
    a0=(440/8)/47872.34. For MIDI 69: (69-9)=60 semitones → ratio 2^5=32 →
    55*0.25*32 = 440 Hz. So MIDI 69 = A4 = 440 Hz. Tuning is correct.

**Phase 3 — Polyphony and envelopes (COMPLETE).**
- PlaitsSynthVoice (`Source/PlaitsSynthVoice.{h,cpp}`):
  juce::SynthesiserVoice owning one PlaitsVoice + one juce::ADSR (external-level mode:
  ADSR value multiplied into output and fed to PlaitsVoice::setLevel() each sample).
  startNote → ADSR.trigger + noteOn. stopNote(allowTailOff) → ADSR release;
  voice cleared via clearCurrentNote() once ADSR idle.
  Polyphony fix: renders into private scratch_ (8192 samples, pre-sized in prepare()),
  then ACCUMULATES into every output channel via FloatVectorOperations::add() —
  all voices sum (JUCE SynthesiserVoice contract: each voice ADDs).
  kVoiceGain = 0.35f applied before accumulation (8-note chord headroom).
- MutableAudioProcessor: juce::Synthesiser with kMaxVoices (8) PlaitsSynthVoice
  instances + one "AnySound" matching all notes/channels. All voices allocated once
  in constructor; no runtime add/remove. setIndex(i) + shared std::atomic<int>*
  (activeVoiceCount_) for selectable voice count. canPlaySound() returns
  index_ < active count (single lock-free atomic read). On count decrease:
  stopNote(1.0f, true) on voices with index >= new count → smooth ADSR release.
- Pitch bend: bendRange param (0–24 semitones). PlaitsVoice::setPitchOffset(semitones)
  adds to note (clamped 1–127). wheelValueToSemitones(wheel, bendRange) =
  (wheel - 8192) / 8192 * bendRange. Centred (8192) → 0.0 (exact tuning).
  Applied in startNote() and pitchWheelMoved().
- Mod wheel (CC1): stored per voice as modWheel_ (0–1). TEMPORARY routing in
  renderNextBlock(): timbre = clamp(params_.timbre + modWheel_ * 0.5, 0–1).
  Remove modWheel_ term + member in Phase 4 (mod section).
- Sustain pedal (CC64): handled by JUCE Synthesiser (holds stopNote until release;
  allowTailOff → ADSR release). NOT reimplemented.
- prepareToPlay: synth.setCurrentPlaybackSampleRate + iterates all voices calling prepare().
- No allocation, locks, or file I/O on the audio path.

## Phase 4 planning (NOT STARTED)

**Phase 4 — Full parameters and placeholder UI.**
- All 24 engines exposed (currently only 8 default).
- Per-engine parameter sets (harmonics/timbre/morph labels change per engine).
- Mod matrix: timbre, harmonics, morph, FM, model inputs from envelope, LFO, velocity, mod wheel.
- Unison and detune (optional).
- Plain functional UI with stock JUCE components bound to the parameter tree.
- Basic preset save and load.

**Phase 5 — Visualizers.**
- Oscilloscope: audio thread pushes summed output into lock-free ring buffer, UI triggers on rising zero crossing, zoom control.
- Envelope display: draws ADSR curve from parameters even when idle, moving dot per active voice.
- Later: spectrum analyser, per-voice meters.

**Phase 6 — Custom graphics.**
- Film strip or SVG knobs, custom look-and-feel classes, faux display skin around visualizers, resizable and high-DPI friendly, assets embedded as binary data.

**Phase 7 — Release.**
- Presets, auval validation for AU, testing in multiple DAWs, universal binaries on Mac, code signing and notarization, installers, MIT license notice for Plaits.

## Known issues

- **Retrig edge case**: PlaitsVoice handles noteOn→noteOn (retrigger_ flag) but
  noteOff→noteOn (release then retrigger) is not yet verified in a DAW.
- **Mod wheel (CC1) is TEMPORARY**: currently routed to timbre only. Will be
  replaced by the Phase 4 mod matrix.
- **8 simultaneous notes** not yet tested in a DAW (CPU headroom on heavy
  engines such as Particle / Swarm / SixOp).
- **Polyphony fix** (voices now summing) not yet heard in a DAW — needs the
  "press 3-4 keys, all should sound" check.
- **Voice-stealing edge case** (noteOff → noteOn with no render between them)
  is handled by PlaitsVoice::lastTriggerHigh_ / retrigger_ but has not been
  exercised in a DAW yet.
- **Selectable voice count** not yet verified in a DAW (play more notes than
  "voices" → only that many should sound; decrease "voices" while holding a chord
  → smooth release).

## Build

```
cmake --build build --config Release --target Mutable_VST3
```

Output: `build/Plugins/Mutable_VST3/x86_64-win/Release/Mutable.vst3`

To enable the CPU benchmark:
```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DMUTABLE_BUILD_TESTS=ON
cmake --build build --config Release --target MutableBench
```

## How to test

1. Load Mutable.vst3 in a DAW (Ableton Live, Reaper, etc.).
2. Play a single note — should sound.
3. Play 3–4 notes simultaneously — all should sound (polyphony).
4. Adjust engine, harmonics, timbre, morph — should change sound.
5. Adjust attack/decay/sustain/release — should change envelope.
6. Use pitch bend — should bend pitch (range = bendRange param).
7. Use mod wheel (CC1) — should modulate timbre (TEMPORARY).
8. Use sustain pedal (CC64) — should hold notes.
9. Change "voices" param — should limit polyphony.

## Benchmark results

**Task 3e: CPU profiling (DONE, 11 Nov 2026)**
- New benchmark harness: benchmarks/bench_main.cpp (standalone console exe,
  excluded from default build via -DMUTABLE_BUILD_TESTS=ON).
- Method: 8 PlaitsVoice instances, sustained notes (MIDI 60-67),
  setUseExternalLevel(true) + setLevel(1.0f), 10 s rendered in 512-sample
  blocks at 48 kHz, wall-clock via steady_clock, NaN/inf check per block.
- CMakeLists.txt: added option(MUTABLE_BUILD_TESTS OFF) and MutableBench
  target (links plaits_dsp + juce::juce_audio_basics, compiles
  Source/PlaitsVoice.cpp directly).
- Results (Release, MSVC 14.51, Windows x64). % of 1 core = ms/s ÷ 10:
    Engine  ms/s   % of 1 core
    0       20.6   2.1 %
    1       23.1   2.3 %
    2       17.7   1.8 %
    3       17.8   1.8 %
    4        7.2   0.7 %
    5       23.9   2.4 %
    6       14.4   1.4 %
    7       34.2   3.4 %
    8       19.0   1.9 %
    9       22.6   2.3 %
    10      27.0   2.7 %
    11      17.8   1.8 %
    12      22.3   2.2 %
    13      22.3   2.2 %
    14      22.5   2.3 %
    15      22.2   2.2 %
    16      21.5   2.2 %
    17      22.3   2.2 %
    18      22.2   2.2 %
    19      22.9   2.3 %
    20      22.2   2.2 %
    21      22.6   2.3 %
    22      23.9   2.4 %
    23      51.3   5.1 %   <- heaviest (Wave Terrain)
  - Real range across engines: ~0.7 % to ~5.1 % of one core.
  - Heaviest engine: 23 (51.3 ms/s = 5.1 % of one core).
  - Lightest engine: 4 (7.2 ms/s = 0.7 % of one core).
  - Engines above 25 % of one core: none.
  - All 24 engines produced finite (non-NaN) output.
- Interpretation: 8 voices of any single engine is well under 25 % of a core.
  The spread is modest (7x between lightest and heaviest). Multi-core hosts
  have even more headroom. Phase 3 CPU profiling complete.
- No third_party/ changes.

## Plaits API notes (Task 2a, read-only investigation)

Source: `third_party/plaits/dsp/voice.{h,cc}`, `engine/engine.h`,
`dsp/dsp.h`, `stmlib/utils/buffer_allocator.h`, `stmlib/dsp/hysteresis_quantizer.h`.

Voice public API (voice.h:159-254):
  `Voice(); ~Voice();`
  `struct Frame { short out; short aux; };`
  `void Init(stmlib::BufferAllocator* allocator);`
  `void ReloadUserData();`
  `void Render(const Patch& patch, const Modulations& modulations, Frame* frames, size_t size);`
  `inline int active_engine() const;`
  `DISALLOW_COPY_AND_ASSIGN(Voice);`

Patch struct (voice.h:125-137):
  float note;                    // MIDI note number (48=C3, 60=C4)
  float harmonics;               // 0..1
  float timbre;                  // 0..1
  float morph;                   // 0..1
  float frequency_modulation_amount;  // -1..+1
  float timbre_modulation_amount;     // -1..+1
  float morph_modulation_amount;      // -1..+1
  int engine;                    // 0..23
  float decay;                   // 0..1 (LPG decay length)
  float lpg_colour;              // 0..1 (LPG HF bleed)

Modulations struct (voice.h:139-154):
  float engine;                  // 0..1 (quantized to engine index)
  float note;                    // semitone offset (V/OCT)
  float frequency;               // -1..+1 (FM amount)
  float harmonics;               // -1..+1
  float timbre;                  // -1..+1
  float morph;                   // -1..+1
  float trigger;                 // 0..1 gate (>0.3 on, <0.1 off)
  float level;                   // 0..1 (accent/level)
  bool frequency_patched;
  bool timbre_patched;
  bool morph_patched;
  bool trigger_patched;
  bool level_patched;

Sample rate & block size (dsp.h:36-51):
  kSampleRate = 48000.0f
  kCorrectedSampleRate = 47872.34f (hardware I2S error compensation)
  kMaxBlockSize = 24  (max samples per Render call)
  kBlockSize = 12      (firmware DAC block size)
  a0 = (440.0f / 8.0f) / kCorrectedSampleRate

Render output: `Frame` = two `short` (16-bit) values: `out` (main), `aux` (aux).
Render writes `size` frames (max 24) into caller-allocated `Frame* frames`.
Internal float buffers: `out_buffer_[kMaxBlockSize]`, `aux_buffer_[kMaxBlockSize]`.

Engine selection (voice.cc:37-88, engine.h:94-129):
  kMaxEngines = 24 (voice.h:69)
  EngineRegistry<kMaxEngines> engines_ (voice.h:248)
  stmlib::HysteresisQuantizer2 engine_quantizer_ (voice.h:230)
  Init: engines_.Init(); then 24x engines_.RegisterInstance(...) calls.
  All engines share the same BufferAllocator space (allocator->Free() before
  each engine Init, voice.cc:69-71).
  Render: engine_index = engine_quantizer_.Process(patch.engine, engine_cv_);
          Engine* e = engines_.get(engine_index);
  Quantizer: Init(engines_.size(), 0.05f, true) (voice.cc:73)
  Engine base class (engine.h:78-92):
    virtual void Init(stmlib::BufferAllocator* allocator) = 0;
    virtual void Reset() = 0;
    virtual void LoadUserData(const uint8_t* user_data) = 0;
    virtual void Render(const EngineParameters&, float* out, float* aux,
                        size_t size, bool* already_enveloped) = 0;
    PostProcessingSettings post_processing_settings;
  EngineParameters (engine.h:52-59):
    int trigger;   // TriggerState: LOW=0, RISING_EDGE=1, UNPATCHED=2, HIGH=4
    float note; float timbre; float morph; float harmonics; float accent;

Memory (stmlib/utils/buffer_allocator.h:36-83):
  BufferAllocator wraps a raw buffer + size; Allocate<T>() bumps a pointer.
  Firmware uses char shared_buffer[16384] (plaits.cc:56),
  BufferAllocator allocator(shared_buffer, 16384) (plaits.cc:142).
  All 24 engines share this 16 KB space.

Trigger & note (voice.cc:90-270):
  Trigger: modulations.trigger (0..1) delayed by kTriggerDelay=5 samples.
    Rising edge >0.3, falling <0.1. If trigger_patched=true, internal
    envelope is used; else TRIGGER_UNPATCHED passed to engine.
  Note: modulations.note = semitone offset (V/OCT). Combined:
    p.note = ApplyModulations(patch.note + note, ...) where
    note = (modulations.note + previous_note_) * 0.5f (low-passed).
    Final range: -119..+120 semitones (voice.cc:202-203).
  Level: modulations.level (0..1) compressed to accent:
    compressed_level = 1.3f*level / (0.3f+fabsf(level)), clamped 0..1.
    If level_patched, used as accent; else default 0.8f (voice.cc:160-162).
  Engine CV: modulations.engine (0..1) feeds quantizer as engine_cv_.

Engine index order (voice.cc:40-65):
  0=VirtualAnalogVCF, 1=PhaseDistortion, 2/3/4=SixOp (x3),
  5=WaveTerrain, 6=StringMachine, 7=Chiptune,
  8=VirtualAnalog, 9=Waveshaping, 10=FM, 11=Grain,
  12=Additive, 13=Wavetable, 14=Chord, 15=Speech,
  16=Swarm, 17=Noise, 18=Particle, 19=String, 20=Modal,
  21=BassDrum, 22=SnareDrum, 23=HiHat

Note unit: MIDI note number. patch.note = base note. modulations.note =
semitone offset. NoteToFrequency(midi_note) = a0*0.25f*SemitonesToRatio(midi_note-9.0f).