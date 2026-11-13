# STATUS

Last updated: 2026-11-12

## Current state

Phases 1-3 complete (tags v0.1-v0.3). Phase 4 in progress: 4a, 4b, 4c-1 done.
Next task: 4c-2 (LFO as a fourth modulation source).

## Build

```
cmake --build build --config Release --target Mutable_VST3
```

Output: `build\Mutable_artefacts\Release\VST3\Mutable.vst3`
(Ableton locks the file while open; close Live or remove the plugin before copying.)

Optional CPU benchmark:
```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DMUTABLE_BUILD_TESTS=ON
cmake --build build --config Release --target MutableBench
```

## Phase 1 - CMake + JUCE scaffold (COMPLETE, v0.1)

- CMake project, JUCE 8 via FetchContent. VST3 (all platforms), AU (macOS only).
- Output-only stereo bus. `NEEDS_MIDI_INPUT TRUE` in juce_add_plugin (fixes the
  Ableton Live "no valid event input bus" load failure).
- Passes pluginval at strictness 5.

## Phase 2 - Plaits integration, single voice (COMPLETE)

- CMake: `stmlib` and `plaits_dsp` static targets; plugin links `plaits_dsp`.
  Include root is `third_party/`. `plaits_dsp` = 37 .cc files from
  `third_party/plaits/dsp/**` + `resources.cc`. Compiled with `-DTEST` so
  `plaits/user_data.h` uses the mock flash API. Firmware-only files excluded.
  stmlib: 3 .cc files (dsp/atan.cc, dsp/units.cc, utils/random.cc); system/ excluded.
- MSVC build fixes are documented in `third_party/PATCHES.md`:
  - `stmlib` target needs `TEST` defined (ARM inline-asm in dsp.h is in the #else of #ifdef TEST).
  - stmlib/utils/dsp.h:38-84 `__attribute__((always_inline))` -> STMLIB_ALWAYS_INLINE macro.
  - plaits/resources.cc:4032 zero-length array -> `{ nullptr }`.
- PlaitsVoice wrapper (`Source/PlaitsVoice.{h,cpp}`):
  - Heap-allocated `plaits::Voice` (large, DISALLOW_COPY_AND_ASSIGN) + 16 KB char
    buffer + BufferAllocator, created in prepare() (never on the audio thread).
  - Public API: prepare(double), setEngine(int 0..23), setParams(harmonics, timbre,
    morph), noteOn(midiNote, velocity), noteOff(), setLevel(float),
    setUseExternalLevel(bool), setPitchOffset(float), setLpg(lpgColour, decay),
    snapToParams(), render(out, aux, numSamples).
  - render() runs Plaits in chunks of kMaxBlockSize (24) at 48 kHz, converts int16
    Frame out/aux to float, then resamples to the host rate.
  - Resampler: small LinearResampler class using juce::Interpolators::Linear, one
    per channel. The JUCE interpolator is stateful (subSamplePos and
    lastInputSamples[2] carry across calls); a per-call reset() that caused a click
    at every block boundary was removed. process() returns the number of input
    samples consumed; render() memmoves unconsumed samples to the front of the
    scratch buffer. Margin: neededIn = numSamples * ratio + 2, which is sufficient
    (the interpolator reads at most 1 sample beyond the last output sample).
  - Trigger/level: trigger_patched=true (RISING_EDGE/HIGH/LOW); level_patched=false
    by default (accent = velocity, LPG uses ProcessPing). With external level
    enabled: level_patched=true and the LPG uses Process (no built-in decay), so
    the ADSR drives the release.
  - No allocation, locks, or file I/O in render(). Before prepare() (voice_ ==
    nullptr) render() outputs silence.
  - Retrigger fix: Plaits' Voice::Render() latches trigger_state_ high after the
    first note, so a second noteOn with the trigger still high never produces a
    rising edge. noteOn() while already triggered sets retrigger_; render() then
    renders 5 samples with trigger=0.0f to flush the 5-slot trigger delay line
    before the normal loop continues with trigger=1.0f (clean rising edge).
    NOTE: this does not yet cover noteOff -> noteOn (release then retrigger); the
    delay line still holds earlier trigger=1.0f values. A full fix would have
    noteOff() set a flag so render() writes trigger=0.0f for 5 samples before the
    next noteOn.
- Tuning check: patch.note is passed the raw MIDI number. Plaits NoteToFrequency(n) =
  a0 * 0.25 * SemitonesToRatio(n - 9), with a0 = (440/8) / 47872.34. MIDI 69 ->
  60 semitones -> ratio 32 -> 55 * 0.25 * 32 = 440 Hz. Tuning is correct.
- PluginProcessor: AudioProcessorValueTreeState with stable string IDs. State
  save/restore via copyState() -> createXml() -> toString() and XmlDocument::parse
  -> ValueTree::fromXml -> replaceState(). Parameters are read once per block via
  cached RangedAudioParameter* pointers (no string lookups on the audio thread).
  createEditor returns juce::GenericAudioProcessorEditor (TEMPORARY; custom UI in
  Phase 6).

## Phase 3 - Polyphony and envelopes (COMPLETE, v0.3)

- PlaitsSynthVoice (`Source/PlaitsSynthVoice.{h,cpp}`): juce::SynthesiserVoice owning
  one PlaitsVoice + one juce::ADSR (external-level mode: ADSR value multiplies the
  output and feeds PlaitsVoice::setLevel() each sample). startNote -> ADSR.trigger +
  noteOn. stopNote(allowTailOff) -> ADSR release; voice cleared via
  clearCurrentNote() once the ADSR is idle.
- Polyphony: each voice renders into a private scratch_ (8192 samples, pre-sized in
  prepare()), then ACCUMULATES into every output channel with
  FloatVectorOperations::add() (JUCE contract: voices add). kVoiceGain = 0.35f is
  applied before accumulation (headroom for 8-note chords).
- MutableAudioProcessor: juce::Synthesiser with kMaxVoices (8) PlaitsSynthVoice
  instances + one "AnySound" matching all notes/channels. All voices are allocated
  once in the constructor; no runtime add/remove. setIndex(i) + shared
  std::atomic<int>* (activeVoiceCount_) gives a selectable voice count 1-8.
  canPlaySound() returns index_ < active count. When the count decreases,
  stopNote(1.0f, true) is sent to voices with index >= new count (smooth release).
- Pitch bend: bendRange param (0-24 semitones). wheelValueToSemitones(wheel, range) =
  (wheel - 8192) / 8192 * range; centred (8192) -> 0.0 (exact tuning).
- Sustain pedal (CC64) is handled by the JUCE Synthesiser (not reimplemented).
- No allocation, locks, or file I/O on the audio path.

## Phase 4 - Full parameters and placeholder UI (IN PROGRESS)

Done: 4a, 4b, 4c-1. Remaining: 4c-2 LFO, 4d unison/detune, 4e functional UI,
4f presets. All 24 engines are selectable via the `engine` param (0..23, default 8).

### Task 4a - Aux output, Mix and Aux Level (DONE)
- Plaits aux output resampled and ADSR-applied like the main output, blended per
  voice by `mix` (default 0) and `auxLevel`.

### Task 4b - Tuning + LPG colour/decay (DONE)
- Params: coarse (AudioParameterInt, -24..+24 semitones, default 0), fine
  (AudioParameterFloat, -100..+100 cents, default 0), lpgColour (0..1, default 0.5),
  lpgDecay (0..1, default 0.5). Added to the per-block PlaitsSynthVoice::Parameters
  snapshot, written by processBlock() before renderNextBlock.
- Single source of truth for pitch: PlaitsSynthVoice stores the bend in bendSemis_
  (written only by startNote and pitchWheelMoved). renderNextBlock() is the ONLY
  call site of plaitsVoice_.setPitchOffset(), computing
  total = bendSemis_ + coarse + fine / 100. Centred wheel + coarse 0 + fine 0 =
  exactly in tune. Changes affect sounding notes (including release) immediately.
- PlaitsVoice::setLpg(lpgColour, decay) is called each block before render() and maps
  to patch.lpg_colour and patch.decay. In our mode (level_patched=true,
  trigger_patched=true): lpgColour raises the gate cutoff and HF bleed (brighter at
  low levels); decay shapes the release tail only (attack coefficient is fixed at
  0.6, independent of decay).

### Task 4c-1 - Modulation matrix (DONE, 2026-11-12)
- 12 bipolar APVTS params (range -1..+1): modEnv / modVel / modWheel x Harmonics /
  Timbre / Morph / Pitch. modWheelTimbre defaults to 0.5; all other 11 default to 0.0.
- PlaitsVoice::render ramps harmonics, timbre, morph and the pitch offset linearly
  per 24-sample chunk from the previous call's values (prevHarmonics_ / prevTimbre_ /
  prevMorph_ / prevPitch_) to the current targets, to avoid zipper noise. The ramp
  fraction is capped at 1.0. patch.harmonics / timbre / morph / note are initialised
  from the prev* values right after the Patch is constructed, so the retrigger flush
  never sees uninitialised fields.
- PlaitsVoice::snapToParams() is called from noteOn() so a new note does not ramp
  from the previous note's values.
- Garbled `\"` string literals in PluginProcessor.cpp fixed; duplicate
  paramLpgDecay definition removed.
- Verified in Ableton: modEnvTimbre pluck sweep, velocity modulation, mod wheel
  default unchanged, no zipper at large buffers, fast retrigger OK.
- Open items:
  - Order of snapToParams() (at noteOn) vs setParams() (in renderNextBlock) for a
    newly started note is not verified; the snap may use the previous note's
    targets. No audible problem found in testing.
  - Temporary mod-wheel routing removed (no `modWheel_` left in Source).
  - Cosmetic compiler warning: unused variable `auxUsed` (PlaitsVoice.cpp ~line 333).

### Remaining Phase 4 tasks
- 4c-2: LFO as a fourth modulation source.
- 4d: unison and detune (optional).
- 4e: plain functional JUCE UI bound to all params (engine as selector, 24 engines;
  per-engine labels for harmonics/timbre/morph).
- 4f: basic preset save and load.

## Phase 5 - Visualizers
- Oscilloscope: audio thread pushes summed output into a lock-free ring buffer; UI
  triggers on a rising zero crossing; zoom control.
- Envelope display: draws the ADSR curve from parameters even when idle, with a
  moving dot per active voice.
- Later: spectrum analyser, per-voice meters.

## Phase 6 - Custom graphics
- Film strip or SVG knobs, custom look-and-feel classes, faux display skin around
  the visualizers, resizable and high-DPI friendly, assets embedded as binary data.

## Phase 7 - Release
- Presets, auval validation for AU, testing in multiple DAWs, universal binaries on
  Mac, code signing and notarization, installers, MIT license notice for Plaits.
- JUCE 8 license check required before release.

## Known issues / not yet verified in a DAW
(Delete each line once it has been tested.)
- Retrigger noteOff -> noteOn (release then retrigger) not verified.
- 8 simultaneous notes on heavy engines (Particle, Swarm, SixOp) not tested.
- Polyphony summing: "press 3-4 keys, all should sound" check not recorded.
- Selectable voice count: play more notes than "voices" (only that many should
  sound); decrease "voices" while holding a chord (smooth release).
- Voice-stealing edge case (noteOff -> noteOn with no render between) not
  exercised in a DAW.

## How to test
1. Load Mutable.vst3 in Ableton Live.
2. Play a single note, then 3-4 notes together (all should sound).
3. Change engine, harmonics, timbre, morph, and the ADSR; the sound should change.
4. Pitch bend should follow bendRange.
5. Mod wheel (CC1) should act through modWheel* params (default: Timbre at 0.5).
6. modEnv* and modVel* amounts should audibly shape the sound.
7. Sustain pedal (CC64) should hold notes.
8. "voices" should limit polyphony.
9. Run pluginval at strictness 5 after parameter changes.

## Benchmarks (CPU)
Task 3e (11 Nov 2026): bench harness benchmarks/bench_main.cpp (standalone exe,
excluded from the default build via -DMUTABLE_BUILD_TESTS=ON). 8 PlaitsVoice
instances, sustained notes MIDI 60-67, external level 1.0, 10 s in 512-sample blocks
at 48 kHz, wall-clock via steady_clock, NaN/inf check per block. Release, MSVC 14.51,
Windows x64. Percent of one core = ms/s / 10.

- Range across the 24 engines: ~0.7 % (engine 4, 7.2 ms/s) to ~5.1 % (engine 23,
  51.3 ms/s). No engine above 25 % of one core. All outputs finite.
- 4a re-run with the aux path active: heaviest engine is 21 at 58.6 ms/s = 5.9 % of
  one core; all outputs finite. Compare engines within a run only.
- Conclusion: 8 voices of any single engine are well under 25 % of one core.

## Plaits API notes (reference)
Source: `third_party/plaits/dsp/voice.{h,cc}`, `engine/engine.h`, `dsp/dsp.h`,
`stmlib/utils/buffer_allocator.h`.

- Voice: Init(BufferAllocator*), ReloadUserData(), Render(Patch, Modulations,
  Frame*, size), active_engine(). Frame = { short out; short aux; }.
- Patch: note (MIDI), harmonics/timbre/morph 0..1, frequency/timbre/morph
  modulation amounts -1..+1, engine 0..23, decay 0..1, lpg_colour 0..1.
- Modulations: engine 0..1, note (semitone offset), frequency/harmonics/timbre/morph
  -1..+1, trigger 0..1 (>0.3 on, <0.1 off), level 0..1, *_patched flags.
- kSampleRate 48000, kCorrectedSampleRate 47872.34, kMaxBlockSize 24, kBlockSize 12.
- Engine selection: kMaxEngines = 24; HysteresisQuantizer2 on patch.engine.
  All engines share the same 16 KB BufferAllocator space.
- Trigger is delayed by kTriggerDelay = 5 samples. Level -> accent:
  1.3 * level / (0.3 + |level|), clamped 0..1; default accent 0.8 if level unpatched.
- Engine index order: 0 VirtualAnalogVCF, 1 PhaseDistortion, 2/3/4 SixOp (x3),
  5 WaveTerrain, 6 StringMachine, 7 Chiptune, 8 VirtualAnalog, 9 Waveshaping, 10 FM,
  11 Grain, 12 Additive, 13 Wavetable, 14 Chord, 15 Speech, 16 Swarm, 17 Noise,
  18 Particle, 19 String, 20 Modal, 21 BassDrum, 22 SnareDrum, 23 HiHat.