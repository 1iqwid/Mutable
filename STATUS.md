# STATUS

Last updated: 2026-11-15

## Current state

Phases 1-4 complete (tags v0.1-v0.4).
Phase 5 (visualizers) complete: 5a-5e done (not yet tagged).
Next: Phase 6 (custom graphics).

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
  (Save/restore did not actually work until the 4e fix; see Task 4e.)

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

## Phase 4 - Full parameters and placeholder UI (COMPLETE, v0.4, 2026-11-14)

Done: 4a, 4b, 4c-1, 4c-2, 4d, 4e, 4f. All 24 engines are selectable via the `engine` param (0..23, default 8).

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

### Task 4c-2 - LFO (DONE, 2026-11-14)
- Source/Lfo.h: header-only, 6 shapes (Sine, Triangle, SawUp, SawDown, Square, S&H),
  xorshift32 S&H, bipolar -1..+1. Params: lfoShape (choice, read via getIndex()),
  lfoRate (0.05..20 Hz, skew centre 1 Hz), modLfoHarmonics/Timbre/Morph/Pitch (-1..+1).
- One Lfo per PlaitsVoice, prepared at 48 kHz, reset per noteOn (phase 0, per-note
  S&H seed). Advanced once per 24-sample chunk in render(); its value is added after
  the ramp and before clamping, and is NOT part of the prev* ramp state.
- Pitch amount scaled by 12 semitones per unit (same as modEnvPitch).
- The 5-sample retrigger flush does not apply/advance the LFO (~0.1 ms, ignored).
- Verified in Ableton: amount 0 = no change, sine timbre, S&H steps, pitch vibrato,
  phase restart per note. Large-buffer smoothness NOT yet tested (see Known issues).

### Task 4d - Unison, detune, stereo spread (DONE, 2026-11-14)
- Params (appended): unison (int 1..4, default 1), detune (0..50 cents for the
  outer layers, default 10), spread (0..1, default 0.5).
- Each PlaitsSynthVoice owns kMaxUnison (4) PlaitsVoice layers, all prepared in
  prepare(). Layer count is latched at startNote; detune and spread are live.
  Layer position pos is evenly spaced in -1..+1: pitch offset += pos * detune,
  pan = pos * spread (balance law: centre = 1/1). Sum scaled by 1/sqrt(layers).
- Output is now stereo (ch0 = L, ch1 = R; mono or extra channels get the average).
  Unison 1 is identical to the pre-4d output.
- ADSR advances once per sample (not per layer). All layers share one LFO seed
  per note (PlaitsVoice::resetLfo), so S&H steps line up.
- Detune 0 with unison > 1: layers are phase-coherent, about +6 dB vs unison 1.
- Verified in Ableton: unison 1 unchanged, detune/spread, live detune changes,
  unison latched per note, bend and LFO across layers, 8-note chord on a heavy
  engine without dropouts.

### Task 4e - Plain functional UI + state save fix (DONE, 2026-11-14)
- Source/PluginEditor.{h,cpp}: stock JUCE components bound via APVTS attachments
  (editor never touches DSP). All 38 params: rotary knobs with formatted text
  (fixed decimals + units), engine and LFO shape as combo boxes. Four rows of
  sections: Engine/Output; Envelope/LPG/Tuning; Voices/LFO; Env/Vel/Wheel mod.
  Window about 1024 x 620. Replaces GenericAudioProcessorEditor.
- Source/EngineInfo.h: 24 engine names + per-engine Harmonics/Timbre/Morph
  captions, checked against each engine's Render() in third_party/plaits. Captions
  follow the engine combo (user and host changes).
- MutableAudioProcessor::getApvts() and public param ID strings for the editor.
- BUG FIX (pre-existing since Phase 2): apvts_.state was never assigned, so
  copyState() was invalid and getStateInformation saved 0 bytes; DAW sets reopened
  at defaults. Now `apvts_.state = ValueTree("State")` after the last param.
  Sets saved before this fix contain no plugin state.
- Verified in Ableton: layout, all knobs, engine captions, host automation of
  engine, double-click reset, save/reopen of a Live set restores all values.

### Task 4f - Basic presets (DONE, 2026-11-14)
- Source/PresetManager.{h,cpp} (message thread only, owned by the processor):
  preset file = the APVTS state XML (tag "State"), extension .mutablepreset, in
  <user documents>/Mutable/Presets (created on first use).
- Editor preset bar: Init, Load..., Save... (async FileChooser, overwrite warning),
  < / > step through the folder alphabetically, current name label (4 Hz timer so
  it follows host state restores).
- Preset name is a property of the state tree ("presetName"), so DAW sessions keep it.
- applyState() (used by loadPreset AND setStateInformation) resets every param to
  its default before replaceState, so params missing from older presets/sessions
  load at defaults instead of keeping current values.
- Verified in Ableton: save/load, overwrite warning, prev/next, Init, name restored
  with the Live set and after reopening the editor.

## Phase 5 - Visualizers
- Oscilloscope: audio thread pushes summed output into a lock-free ring buffer; UI
  triggers on a rising zero crossing; zoom control.
- Envelope display: draws the ADSR curve from parameters even when idle, with a
  moving dot per active voice.
- Later: spectrum analyser, per-voice meters.
- Plan: 5a ring buffer + basic scope, 5b zero-crossing trigger + zoom, 5c Out/Aux
  on the scope, 5d envelope curve, 5e per-voice dots.

### Task 5a - Ring buffer + basic scope (DONE, 2026-11-15)
- Source/ScopeBuffer.h: lock-free SPSC ring (16384 std::atomic<float>, relaxed;
  write index release/acquire). processBlock pushes the mono sum (L+R)/2 of the
  final output (after outGain and the NaN guard) in 256-sample stack chunks.
- Source/ScopeView.{h,cpp}: 60 Hz timer copies the newest 20 ms and draws it.
  Free-running (no trigger yet).
- Editor: window taller by a 200 px full-width visualizer row at the bottom.
- Verified in Ableton by Manny.

### Task 5b - Scope trigger + zoom (DONE, 2026-11-15)
- ScopeView: rising zero-crossing trigger (arms below -0.01, newest crossing that
  still leaves a full window, sub-sample interpolated start); searches at least
  30 ms back; free-runs if no crossing (silence, DC).
- Zoom slider 1-100 ms (default 20) and Trigger toggle in the scope title strip.
  View settings only (not APVTS params, not saved).
- Engine-CV bug fixed in the same session: PlaitsVoice passed mod.engine = 0.5,
  but Plaits treats it as an offset (patch.engine + cv * 23), so every engine
  played ~12 slots higher and 13-24 all played Hi-Hat. Now 0.0, as in
  plaits_test.cc. Manny verified all 24 engines match their names.
- Verified in Ableton by Manny.

### Task 5c - Out/Aux overlay (DONE, 2026-11-15)
- ScopeBuffer is now 2 channels (Out, Aux) with one shared write index; the 5a
  final-output tap is gone.
- Each voice sums its pre-Mix Out and Aux*auxLevel (after ADSR, voice gain and
  unison norm, no pan) into a processor-owned 2 x 8192 tap (allocated in the
  constructor, cleared per block); processBlock applies outGain and pushes it.
  Blocks > 8192 samples are not shown.
- ScopeView overlays Out (green) and Aux (amber) with a legend; one trigger point
  (Out, else Aux) for both so relative phase is kept.
- Verified in Ableton by Manny.

### Task 5d - Envelope display (DONE, 2026-11-15)
- Source/EnvelopeView.{h,cpp}: draws the ADSR from APVTS raw values (UI thread,
  30 Hz poll, repaints only on change). Linear segments (juce::ADSR is linear).
- Time axis: width shares A 25 %, D 25 %, S hold 20 %, R 30 %, each stage scaled
  by sqrt(t / max). Max times (5 / 5 / 10 s) are duplicated from the param ranges
  in EnvelopeView.cpp; keep them in sync.
- Bottom row: scope (flexible, left) | envelope (360 px, right).
- Verified in Ableton by Manny.

### Task 5e - Per-voice envelope dots (DONE, 2026-11-15)
- Source/VoiceDisplayState.h: per-voice relaxed atomics (stage, level, progress,
  release start level, stage/progress at note-off). Processor owns 8; each voice
  publishes at the end of renderNextBlock and publishes idle when it stops.
- juce::ADSR does not expose its stage, so PlaitsSynthVoice infers it: attack until
  level reaches 1, decay until sustain, then sustain; release after a tail-off
  stopNote (first note-off point kept).
- EnvelopeView (now 60 Hz; repaints only while voices sound or params change):
  dot x = stage start + progress, y = actual level; sustain dots sit mid-hold.
  Early release (from attack/decay): faint lead-in at the release level from the
  let-go point to the release start, plus a faint ghost release line to 0.
- Verified in Ableton by Manny.

## Phase 6 - Custom graphics
- Film strip or SVG knobs, custom look-and-feel classes, faux display skin around
  the visualizers, resizable and high-DPI friendly, assets embedded as binary data.

## Phase 7 - Release
- Presets, auval validation for AU, testing in multiple DAWs, universal binaries on
  Mac, code signing and notarization, installers, MIT license notice for Plaits.
- JUCE 8 license check required before release.

## Known issues / not yet verified in a DAW
(Delete each line once it has been tested.)
- CPU benchmark needs a re-run after the engine-CV fix (old per-engine numbers invalid).
- LFO at 20 Hz with a large buffer (2048) not tested for stepping (no audio
  interface; buffer size unknown).
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
- INVALID per engine (found 2026-11-15): both runs had the engine-CV bug (see 5b), so
  engine N actually measured engine min(N+12, 23). Indices 11-23 all measured Hi-Hat
  and indices 0-10 never measured the engines labelled. Needs a re-run.

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