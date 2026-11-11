# STATUS

## Phase 1: CMake + JUCE scaffold (COMPLETE, tagged v0.1)

Done:
- CMake project with JUCE 8 via FetchContent; VST3 target, AU only on macOS.
- Builds on Windows with Visual Studio 2026 (MSVC), Release.
- Output-only stereo bus (no input bus).
- processBlock clears the buffer and uses ScopedNoDenormals.
- Passes pluginval at strictness level 5 (SUCCESS).
- Fixed Ableton Live load failure ("plugin has instrument category, but no valid
  event input bus") by adding NEEDS_MIDI_INPUT TRUE to juce_add_plugin.
- Confirmed Mutable loads in Ableton Live and opens an empty plugin window.

Known issues:
- No sound yet (no DSP; that's Phase 2).
- Windows Smart App Control blocks unsigned plugins; it had to be turned off for
  local testing. Code signing is a Phase 7 item.

## Phase 2: Plaits integration (IN PROGRESS, branch phase-2-plaits-voice)

Task 1: CMake targets for Plaits + stmlib
- Added `stmlib` and `plaits_dsp` static library targets in the root
  CMakeLists.txt; `plaits_dsp` links `stmlib`, plugin links `plaits_dsp`.
  Include root is third_party/ (code includes "plaits/..." and "stmlib/...").
- `plaits_dsp` = 37 .cc files: everything in third_party/plaits/dsp/** plus
  resources.cc. Explicit list cross-checked against the Plaits project's own
  host build (third_party/plaits/test/makefile) and the on-disk file inventory.
  Compiled with -DTEST, same as that makefile, so plaits/user_data.h uses its
  mock flash API instead of STM32 headers. Firmware-only files (plaits.cc,
  settings.cc, ui.cc, user_data_receiver.cc, drivers/, bootloader/, test/)
  excluded.
- BLOCKED at CMake configure: third_party/stmlib/ is an EMPTY folder - no
  sources, nothing tracked in git, and no stmlib.h anywhere in the repo.
  Exact error: CMakeLists.txt:56 add_library: "No SOURCES given to target:
  stmlib". Every plaits DSP file includes stmlib headers (stmlib/stmlib.h,
  stmlib/dsp/filter.h, stmlib/utils/buffer_allocator.h, ...), so nothing
  compiles until the stmlib sources exist.
- stmlib sources are now in third_party/stmlib/ (936 files, MIT LICENSE present).
  stmlib is mostly header-only; the target compiles exactly 3 .cc files
  (dsp/atan.cc, dsp/units.cc, utils/random.cc), matching the Plaits host build
  packages (stmlib/utils, stmlib/dsp). stmlib/system/ is STM32 firmware
  (flash programming, system clock) and is excluded.
- First MSVC build failed in stmlib: stmlib/dsp/dsp.h:137/142/154 -
  C4235/C2059 on ARM inline asm (ssat/usat/vsqrt.f32). Root cause: those asm
  blocks sit in the #else branch of #ifdef TEST, and TEST was defined only on
  plaits_dsp, not on stmlib (CMake PUBLIC definitions flow downstream to
  consumers, NOT upstream to dependencies).
- RESOLVED: target_compile_definitions(stmlib PUBLIC TEST) applied (see
  CMakeLists.txt). stmlib now builds cleanly on MSVC (stmlib.lib).
- Build attempt 2: plaits_dsp - 35 of 37 .cc files compile. 2 files fail,
  both on MSVC-incompatible constructs in third_party:
  1. stmlib/utils/dsp.h:38-84 - `__attribute__((always_inline))` (GCC/Clang
     only; MSVC C3646/C2059 with cascading C2084/C2440).
  2. plaits/resources.cc:4032 - zero-length array (MSVC C2466). Nothing
     indexes this table (only the extern in resources.h).
- RESOLVED with minimal patches (documented in third_party/PATCHES.md; all
  license headers untouched):
  1. dsp.h - new STMLIB_ALWAYS_INLINE macro: expands to nothing on MSVC
     (comment notes __forceinline cannot follow the declarator), keeps
     __attribute__((always_inline)) on GCC/Clang.
  2. resources.cc - `lookup_table_i32_table[] = {}` -> `{ nullptr }`.
- Build attempt 3: SUCCESS, no new errors (build/patches_build.log,
  EXITCODE=0). plaits_dsp.lib grew 96,560 -> 968,976 bytes (resources.cc now
  compiles in). VST3 bundle: build/Mutable_artefacts/Release/VST3/Mutable.vst3
  (5.5 MB) + Contents/Resources/moduleinfo.json.

Known issues:
- plaits copy has no LICENSE/README file (per-file headers say MIT); need MIT
  license text + THIRD_PARTY_NOTICES entry (Phase 7 release item).
- Unverified: plugin not yet re-tested in a DAW now that Plaits code is
  linked into the binary; macOS build not yet checked (only MSVC/Windows so
  far).

Task 2b: PlaitsVoice wrapper (COMPLETE)
- Source/PlaitsVoice.h + Source/PlaitsVoice.cpp added to the plugin target.
- Heap-allocated plaits::Voice (large, DISALLOW_COPY_AND_ASSIGN) + 16 KB
  char buffer + BufferAllocator, created in prepare() (never on the audio
  thread).
- Public API: prepare(double), setEngine(int 0..23), setParams(harmonics,
  timbre, morph), noteOn(midiNote, velocity), noteOff(), setLevel(float),
  setUseExternalLevel(bool), render(out, aux, numSamples).
- render() runs Plaits in chunks of kMaxBlockSize (24) at 48 kHz, converts
  int16 Frame out/aux to float, then resamples to the host rate.
- Resampler in its own small class (LinearResampler) using
  juce::Interpolators::Linear, one per channel, easy to swap later.
  The JUCE GenericInterpolator IS stateful (tracks subSamplePos and
  lastInputSamples[2] across calls).  Fixed: removed the per-call reset()
  that was destroying the fractional position and causing a click at every
  block boundary.  The resampler now carries its state forward automatically,
  producing a continuous output stream.
  LinearResampler::process() now returns the number of input samples
  consumed (per JUCE API), and render() memmoves any unconsumed samples
  to the front of the scratch buffer for the next call.
- Trigger/level: trigger_patched=true (RISING_EDGE/HIGH/LOW),
  level_patched=false by default (accent=velocity, LPG uses ProcessPing →
  play, hold, decay).  New setLevel(float) + setUseExternalLevel(bool):
  when external level is enabled, level_patched=true and the LPG uses
  Process (no built-in decay), so our ADSR (Phase 3) can drive release.
- No allocation, locks, or file I/O in render().
- Builds clean on MSVC (Release).  VST3: build/Mutable_artefacts/Release/VST3/
  Mutable.vst3.

Known issues / unverified:
- Resampler discontinuity fix is verified by code inspection (JUCE source
  confirms statefulness), but NOT yet audited in a DAW.  Listen for clicks
  at block boundaries when testing.
- External level path (useExternalLevel_=true) not yet exercised in a DAW.
- macOS build not yet checked (only MSVC/Windows so far).

Task 2c: wire PlaitsVoice into PluginProcessor (COMPLETE)
- Single voice, last-note priority (polyphony arrives in Phase 3).
- AudioProcessorValueTreeState with stable string IDs:
  engine (AudioParameterInt 0..23, default 8), harmonics/timbre/morph
  (AudioParameterFloat 0..1, default 0.5), outGain (AudioParameterFloat
  0..1, default 0.7). Constructor uses the JUCE 8
  AudioProcessorValueTreeState(AudioProcessor&, UndoManager*) form.
- State save/restore via APVTS: getStateInformation uses copyState()
  (thread-safe) -> createXml() -> toString(); setStateInformation uses
  XmlDocument::parse -> ValueTree::fromXml -> replaceState().
- prepareToPlay calls voice_.prepare(sampleRate). releaseResources/destructor
  leave nothing hanging (voice_ is a member; its destructor deletes the
  plaits::Voice).
- processBlock: ScopedNoDenormals, buffer.clear(), reads the 5 parameters
  ONCE per block via cached RangedAudioParameter* raw pointers (no string
  lookups on the audio thread). Skips redundant setEngine() via
  lastEngineSent_. Iterates the MIDI buffer and splits rendering at each
  event's samplePosition so note on/off is sample-accurate; a NoteOn with
  velocity 0 counts as noteOff. Renders mono into channel 0, scales by
  outGain, copies to the other channel(s).
- Before the voice is prepared (voice_==nullptr), render() outputs silence
  instead of crashing (guarded in PlaitsVoice::render).
- createEditor returns juce::GenericAudioProcessorEditor, marked TEMPORARY
  in a comment (custom UI replaces it in Phase 6).
- Resampler margin check (task ask): render() generates
  neededIn = numSamples*ratio_ + 2 input samples (PlaitsVoice.cpp:103) and
  feeds the JUCE Interpolators::Linear with wrapAround=0. The Linear
  interpolator reads at most 1 input sample BEYOND the final output sample
  (subSamplePos starts < 1, memorySize=2), so the +2 margin is sufficient
  and the resampler is never asked for more input than it has. No change
  required.
- Tuning check (task ask): patch.note is passed the raw MIDI number
  (PlaitsVoice.cpp:109). Plaits engine.h NoteToFrequency(n) =
  a0*0.25f*SemitonesToRatio(n-9.0f), with a0=(440/8)/47872.34.
  For MIDI 69: (69-9)=60 semitones -> ratio 2^5=32 -> 55*0.25*32 = 440 Hz.
  So MIDI 69 = A4 = 440 Hz. Tuning is correct. (The a0*0.25f*2^-9
  factors exactly cancel the -9 offset; the 47872.34 "corrected" rate is
  the hardware I2S compensation and does not affect the audible pitch,
  which the resampler maps back to the host rate.)
- Build: SUCCESS on MSVC Release. VST3 at
  build/Mutable_artefacts/Release/VST3/Mutable.vst3. One benign warning:
  C4100 unreferenced 'layouts' in isBusesLayoutSupported.

Known issues / unverified (2c):
- Not yet auditioned in a DAW (single note, MIDI 69 -> 440 Hz unconfirmed
  by ear). Listen for correct pitch and clean block boundaries.
- macOS build not yet checked (only MSVC/Windows so far).
- outGain currently scales only the Out channel; Aux is not used in
  processBlock (aux=nullptr) until the Mix/Aux knobs land.
- BUG FIXED: second and subsequent notes were silent. Root cause: Plaits'
  Voice::Render() latches trigger_state_ high after the first note. A second
  noteOn with trigger held high never produces TRIGGER_RISING_EDGE, so the
  engine is never retriggered. Fix: PlaitsVoice now has a retrigger_ flag
  (PlaitsVoice.h:119-123). noteOn() while already triggered sets
  retrigger_=true instead of noteOn_=true. render() checks retrigger_ and,
  if set, renders 5 samples with trigger=0.0f to flush the 5-slot trigger
  delay line (PlaitsVoice.cpp:161-193), then the normal generation loop
  continues with trigger=1.0f, producing a clean false→true rising edge →
  TRIGGER_RISING_EDGE. Builds clean on MSVC Release.
  NOTE: this does NOT fix noteOff→noteOn (release then retrigger) because
  the trigger delay line still contains the previous trigger=1.0f values.
  A full fix would require noteOff() to also set a flag that causes render()
  to write trigger=0.0f for 5 samples before the next noteOn's trigger=1.0f.
  See the retrigger_ flag logic in PlaitsVoice.cpp for the pattern.

Task 3a: Polyphony + per-voice ADSR (COMPLETE)
- Source/PlaitsSynthVoice.h + .cpp: juce::SynthesiserVoice subclass owning
  one PlaitsVoice and one juce::ADSR (attack 5 ms, decay 200 ms, sustain 0.7,
  release 300 ms).  External-level mode: ADSR value multiplied into the output
  and fed to PlaitsVoice::setLevel() each sample.
  startNote triggers ADSR + PlaitsVoice::noteOn.
  stopNote(allowTailOff) starts ADSR release; voice is cleared via
  clearCurrentNote() once the ADSR goes idle (or immediately if !allowTailOff).
- MutableAudioProcessor now holds a juce::Synthesiser with 8 PlaitsSynthVoice
  instances (kNumVoices = 8) and one "AnySound" that matches every
  note/channel.
- prepareToPlay: calls synth.setCurrentPlaybackSampleRate and iterates all
  voices calling prepare(rate).  Scratch buffers are preallocated inside
  PlaitsVoice (kScratchSize = 8192) — no allocation in processBlock.
- processBlock: snapshots engine/harmonics/timbre/morph/outGain from APVTS
  into a PlaitsSynthVoice::Parameters struct and calls setParameters on every
  voice before synth.renderNextBlock.  Output scaled by outGain; channel 0
  copied to channels 1..N-1.
- Silence-before-prepare and NaN/inf guard preserved from Task 2b.
- NOTE: voice count is a compile-time constant of 8 for now; will become
  selectable in Phase 3 (task 3b).

Polyphony bug fix (COMPLETE)
- BUG: plugin sounded monophonic — pressing several keys at once only played
  one note.  ROOT CAUSE: PlaitsSynthVoice::renderNextBlock() rendered directly
  into the shared output buffer (outputBuffer.getWritePointer(0)) and OVERWROTE
  it.  But JUCE's Synthesiser::renderVoices() clears the buffer once, then calls
  renderNextBlock() on every voice with that same buffer, and the
  SynthesiserVoice::renderNextBlock contract (juce_Synthesiser.h:183-189) is
  that each voice must ADD its samples.  So the last active voice clobbered
  all the others.
- FIX: PlaitsSynthVoice now renders into a PRIVATE mono scratch buffer
  (scratch_, pre-sized in prepare() to 8192 samples on the message thread so
  the audio path never allocates), applies the ADSR there, then ACCUMULATES
  into every output channel with juce::FloatVectorOperations::add().  All 8
  voices now sum together.
  - PlaitsSynthVoice.h: added scratch_ member, ensureScratch() helper,
    kMaxBlockSamples = 8192.
  - PlaitsSynthVoice.cpp: prepare() pre-sizes scratch_; renderNextBlock()
    renders into scratch_ and adds into output channels 0..N-1.
  - No changes to PlaitsVoice, the processor, or third_party/.
- Rebuilt Mutable_VST3 (Release) — 0 errors (only pre-existing C4100 'layouts'
  warning in PluginProcessor.cpp).
- NOTE: voice count is a compile-time constant of 8 for now; will become
  selectable in Phase 3 (task 3b).

Unverified (3a):
- 8 simultaneous notes not yet tested in a DAW (CPU headroom on heavy
  engines such as Particle / Swarm / SixOp).
- Polyphony fix (voices now summing) not yet heard in a DAW — needs the
  "press 3-4 keys, all should sound" check.
- Voice-stealing edge case (noteOff → noteOn with no render between them)
  is handled by PlaitsVoice::lastTriggerHigh_ / retrigger_ but has not been
  exercised in a DAW yet.
- (ADSR values are now APVTS parameters — see Task 3b below.)

Task 3b: Adjustable envelope (ADSR) (COMPLETE)
- Four new APVTS parameters (stable string IDs), registered in
  PluginProcessor.cpp and cached as raw pointers (no string lookups in
  processBlock), same pattern as the other parameters:
    attack    0.001–5 s   default 0.005   skew 0.3
    decay     0.001–5 s   default 0.2     skew 0.3
    sustain   0–1         default 0.7     (linear)
    release   0.001–10 s  default 0.3     skew 0.3
  The three time parameters use a skewed NormalisableRange (skew ≈ 0.3) so
  the short end has fine control (helper makeSkewedRange() in PluginProcessor.h).
- The four values were added to PlaitsSynthVoice::Parameters and are snapshotted
  in processBlock() alongside engine/harmonics/timbre/morph.
- PlaitsSynthVoice::setParameters() (audio thread, called before each
  renderNextBlock) now applies the snapshot to adsr_ via
  juce::ADSR::setParameters(), but ONLY when one of the four values actually
  changed — so it is not called every block.  A zero/negative time guard
  clamps to 0.001 s (sustain clamped to 0..1).
- Signature confirmed in juce_ADSR.h: juce::ADSR::Parameters(attack, decay,
  sustain, release) and setParameters() only recalculates rates — it does NOT
  reset the envelope state — so changing values on already-playing voices is
  click-free (in-progress attack/decay/sustain/release continues with the new
  values).
- No third_party/ changes. Temporary GenericAudioProcessorEditor shows the new
  sliders automatically, so no UI change.
- Rebuilt Mutable_VST3 (Release) — 0 errors; Mutable.vst3 produced.
- Unverified: audible envelope changes in a DAW (adjust attack/decay/sustain/
  release while a note is held, and while releasing).

Task 3c: Pitch bend, mod wheel, sustain pedal, headroom (COMPLETE)
- bendRange APVTS parameter (stable string id "bendRange"), 0-24 semitones,
  integer steps (AudioParameterInt), default 2. Registered + cached in
  PluginProcessor like the ADSR params.
- PlaitsVoice::setPitchOffset(float semitones): stores a per-voice offset added
  to the note passed to Plaits (note_ + pitchOffset_, clamped 1..127). At zero
  bend the note is passed unmodified -> exact tuning.
- wheelValueToSemitones(wheel, bendRange) (static helper in PlaitsSynthVoice.h):
  maps raw wheel (0..0x3FFF, 0x2000 centred) to a signed semitone offset by
  centreing first: (wheel - 8192) / 8192 * bendRange. Centred (8192) -> 0.0
  semitones, so the note passes through to Plaits UNMODIFIED -> exact tuning at
  zero bend. Full range (0 / 0x3FFF) -> -bendRange / +bendRange. Shared by
  startNote() and pitchWheelMoved() so the two paths can never disagree.
- pitchWheelMoved(): uses wheelValueToSemitones() -> setPitchOffset. (Was
  previously (wheel/8191)*bendRange, which made centred != 0 and broke exact
  tuning; now fixed.)
- startNote() reads currentPitchWheelPosition (JUCE passes the CURRENT wheel
  position to new voices -- verified Synthesiser::startVoice passes
  lastPitchWheelValues[channel-1], initialised to 0x2000) and applies
  wheelValueToSemitones() immediately, so a note starting while the wheel is
  already bent is bent from sample one and still exact at centred.
- controllerMoved() CC1: stores modWheel_ (0..1) per voice ONLY (no setParams
  call). TEMPORARY routing (marked // TEMPORARY in both files) is applied in
  renderNextBlock() as the single source of truth: timbre = clamp(params_.timbre
  + modWheel_ * 0.5, 0..1), until the Phase 4 mod section exists. (Previously
  controllerMoved() called setParams() with the mod'd timbre while
  renderNextBlock() overwrote it with the base timbre every block, so the
  temporary CC1->timbre route effectively did nothing on held notes; now fixed
  so the route works and can't fight the per-block path.) Remove the modWheel_
  term + member in Phase 4.
- Sustain pedal: JUCE's Synthesiser already handles CC64 (holds stopNote until
  release; allowTailOff path -> ADSR release; voice stays active until
  renderNextBlock sees ADSR idle -> clearCurrentNote). Verified in
  juce_Synthesiser.h/.cpp; NOT reimplemented.
- Headroom: kVoiceGain = 0.35f (constant in PlaitsSynthVoice) applied before
  each voice adds into the shared output, so an 8-note chord doesn't clip at
  the default outGain.
- All audio-thread-safe: no allocation, locks, or file access on the audio path.
- Rebuilt Mutable_VST3 (Release) — EXITCODE=0; Mutable.vst3 produced. (First
  attempt failed only with LNK1104 "cannot open ...Mutable.vst3" — a transient
  file lock, zero compile errors; a clean rebuild succeeded.)
- Unverified: pitch bend + CC1 mod-wheel audible in a DAW (code paths verified
  against JUCE headers/cpp).

Next:
1. Verify in DAW: hold a note and sweep the pitch bend — pitch should move up
   and down by the full bendRange; start a note with the wheel already bent —
   it should ring at the bent pitch.
2. Verify mod wheel (temporary): modulate CC1 — timbre should shift (the
   Phase 4 mod section will replace this routing).
3. Verify sustain pedal: hold a note, release the key with pedal up — note
   should release; with pedal held, releasing the key should sustain.
4. Verify headroom: play an 8-note chord at the default outGain — no clipping.
5. Commit (suggested message:
   "Phase 3c: pitch bend, mod wheel (temporary), sustain pedal, headroom").
6. Remaining Phase 3 items: selectable voice count. Then Phase 4: full
   parameters, unison/detune, mod section (replace the temporary CC1→timbre
   routing), plain UI, presets.

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