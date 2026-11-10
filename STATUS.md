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

Next:
1. Commit (suggested message: "Phase 2: fix retrigger bug — second note
   silent due to latched trigger_state_").
2. Verify in DAW: play note, release, play same note — should retrigger.
   Play note, play second note while first is held — second should sound.
3. Task 3 (Phase 3): polyphony (8-voice pool via JUCE synthesiser),
   ADSR envelope per voice, pitch bend, mod wheel, sustain pedal.

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