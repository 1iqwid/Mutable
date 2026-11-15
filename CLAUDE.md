# Mutable: polyphonic Plaits synth plugin

JUCE 8 + CMake VST3 (Win/Mac) and AU (Mac) built on the open-source Mutable
Instruments Plaits DSP (MIT, in third_party/). Owner: Manny. He tests in Ableton Live
and often reads replies on his phone: keep replies short and lead with the answer.

## Sources of truth
- STATUS.md: current state, per-phase notes, known issues, benchmarks.
- .clinerules/product_brief.md: original brief for a previous assistant. Its
  still-valid content is folded into this file (2026-11-14); treat it as history.
- Never put credentials in any doc or the repo.

## Build (the only correct command)
`cmake --build build --config Release --target Mutable_VST3`
Output: `build\Mutable_artefacts\Release\VST3\Mutable.vst3`. Windows PowerShell
(no `&&`). Ableton locks the .vst3 while open; Manny must close Live or remove the
plugin before copying. Do not invent other targets or build Debug. Do not poll with
sleep loops. Compile warnings from third_party/ are noise; do not edit third_party/
except as documented in third_party/PATCHES.md.
Optional CPU benchmark: add `-DMUTABLE_BUILD_TESTS=ON`, target MutableBench.
Percent of one core = ms/s divided by 10 (past benchmark notes got this wrong by 100x).

## Workflow rules
- Git: private GitHub repo, branch per phase (currently working on main), a tag at
  each phase end (v0.1-v0.3 exist), commit after each task with message
  "Phase X: ...". Ask before pushing, tagging, or any destructive git command; never
  rewrite history, force push, or delete branches. main must always build. Don't
  commit build output, IDE files, or large binaries (.gitignore covers them).
- Never invent JUCE or Plaits API names. Check the header first (JUCE is in
  build/_deps, Plaits in third_party/). If something is ambiguous, ask.
- Small tasks. After code changes: build, then report real output. A claim like
  "verified" needs proof (build result, grep output, diff). Be critical of your own
  work. Earlier assistants claimed "verified" for things that were wrong.
- Audio thread rules: no allocation, locks, file I/O, or std::rand in processBlock,
  renderNextBlock, or PlaitsVoice::render. Parameters are read once per block via
  cached RangedAudioParameter* pointers (no string lookups on the audio thread).
- Only Manny can test in Ableton or run pluginval (strictness 5). When a change needs
  an audible check, give him a short numbered test list and wait for his result.
- Keep STATUS.md accurate and concise after each task. Don't leave stale text.
- Release needs a JUCE 8 license check, MIT notice for Plaits, signing/notarization.
  Keep the MIT license files (third_party/LICENSE.md, third_party/stmlib/LICENSE)
  and THIRD_PARTY_NOTICES.md in the repo.

## Design rules
- C++20 (CMakeLists.txt). UI and DSP stay in separate files; the editor binds only
  to APVTS parameters and never touches DSP objects.
- Visualizer data goes audio -> UI through lock-free buffers only; the UI reads on a
  timer at about 60 fps.
- One Plaits instance and one ADSR per voice.
- The Plaits hardware panel is a functional reference only: never copy its artwork
  or Mutable Instruments branding.
- Out/Aux: only Mix (crossfade) + Aux Level, no stereo mode. The oscilloscope should
  be able to show both signals.

## Architecture
- Source/PlaitsVoice.{h,cpp}: wraps plaits::Voice. Runs Plaits at 48 kHz in 24-sample
  chunks, int16 to float, linear resampler to the host rate. Latched-trigger retrigger
  fix (noteOn while triggered flushes 5 samples with trigger=0).
  harmonics/timbre/morph/pitch offset are ramped per 24-sample chunk from the previous
  call's values (prev*_) to current targets; ramp fraction capped at 1.0; snapToParams()
  on noteOn. Pitch has a single source of truth: renderNextBlock() in PlaitsSynthVoice.
- Source/PlaitsSynthVoice.{h,cpp}: juce::SynthesiserVoice with ADSR (external level
  mode) and a per-block Parameters snapshot. Voices accumulate into the output.
- Source/PluginProcessor.{h,cpp}: APVTS with stable string IDs, 8 voices, selectable
  count, MIDI (pitch bend with bendRange, mod wheel, sustain via JUCE Synthesiser).
  Params so far: engine (0..23), harmonics, timbre, morph, outGain, attack/decay/
  sustain/release, bendRange, voices, mix, auxLevel, coarse, fine, lpgColour, lpgDecay,
  12 mod params (modEnv/modVel/modWheel x Harmonics/Timbre/Morph/Pitch; modWheelTimbre
  defaults 0.5), and lfoShape, lfoRate, modLfoHarmonics/Timbre/Morph/Pitch.
  Append new params; don't reorder existing ones.
- Source/Lfo.h: header-only LFO (6 shapes, xorshift32 S&H, bipolar -1..+1).

## Roadmap
Done: Phases 1-3 (tags v0.1-v0.3); 4a aux output, 4b tuning + LPG, 4c-1 mod matrix,
4c-2 LFO.
Next: 4d unison/detune, 4e plain functional JUCE UI bound to all
params (engine selector with all 24 engines, per-engine parameter labels), 4f preset
save/load. Then Phase 5 visualizers (oscilloscope, envelope display), Phase 6 custom
graphics (film-strip/SVG knobs, custom look-and-feel), Phase 7 polish, signing,
installers. Planned but not set up: GitHub Actions CI building on macOS and Windows.