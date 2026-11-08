# PROJECT BRIEF: Polyphonic Plaits Synth Plugin

You are my C++ coding assistant. Read this brief fully before doing anything. We will work one small task at a time.

## What We Are Building

A polyphonic software synthesizer plugin based on the open source Mutable Instruments Plaits DSP code (MIT license).

- Formats: VST3 on Mac and Windows, Audio Unit (AU) on Mac only.
- Polyphonic, starting with 8 voices.
- One codebase for all platforms.
- Custom artwork and visualizers will be added later, so UI and DSP must stay separate.

## Tech Stack

- Language: C++17
- Framework: JUCE 8, fetched with CMake FetchContent
- Build system: CMake, used from VS Code
- DSP source: Plaits and its stmlib dependency, copied into the repo under a third\_party folder
- CI later: GitHub Actions on macOS and Windows

## Control Map (from the Plaits hardware panel)

The panel is a functional reference only. Never copy its artwork or Mutable Instruments branding. A photo is saved at docs/reference/plaits-panel.jpg.

- Engine selector: replaces the two buttons and 16 LEDs. Check the Plaits source for the real engine count and do not assume 16.
- Frequency: MIDI replaces V/Oct, so this is a coarse tune control. Pitch bend range is a separate setting.
- Harmonics, Timbre, Morph: the three main parameters.
- Timbre, FM, and Morph attenuverters: bipolar modulation amounts, fed by sources like envelope, LFO, velocity, and mod wheel.
- Model and Harmo inputs: engine and harmonics modulation. Ask me whether Harmonics gets its own mod-amount knob.
- Trig and Level: Level is normalled to Trig on the hardware. Check the Plaits source for how it handles decay and low-pass-gate settings, and ask me before choosing how they coexist with our ADSR.
- Out and Aux: Plaits has two outputs. Decision made: provide a Mix knob that crossfades between Out and Aux, plus an Aux Level knob that sets Aux loudness before the blend. Keep it to just those two knobs for now, with no stereo mode. The oscilloscope should be able to show both signals.

## Architecture Rules (always follow these)

1. The audio thread must never allocate memory, take locks, or draw anything.
2. All parameters live in a JUCE AudioProcessorValueTreeState. The editor only binds to parameters and never touches DSP objects directly.
3. Data for visualizers moves from audio to UI through lock-free buffers only. The UI reads on a timer at about 60 frames per second.
4. Plaits runs natively at 48 kHz. If the host sample rate differs, resample cleanly.
5. One Plaits instance per voice. Each voice has its own envelope.
6. Keep DSP code, plugin glue, and UI code in separate files and folders.

## Rules For How You Work

- Do ONE task at a time, only the one I ask for. Do not jump ahead to later phases.
- NEVER invent JUCE or Plaits function names. If you are not sure an API exists, say so and ask me, or tell me which header to check.
- Before writing code, tell me in two or three sentences what you plan to do and which files you will touch.
- Keep changes small. Show complete files or clear diffs, not fragments.
- After each task, tell me exactly how to build it and how to test it.
- If something is ambiguous, ask a question instead of guessing.
- At the end of every task, before saying you are finished, update STATUS.md: what was completed, any known issues or unverified items, and the next step. Keep it short and accurate, and change no other files.

## Git and GitHub Rules

- Never ask for, read, or write passwords, tokens, SSH keys, or any credentials. If a login is needed, tell me the command and I will run it myself. Never put secrets in any file in the repo.

* The project lives in a Git repo with a private GitHub remote. main must always build.
* Work on one branch per phase, named like phase-2-plaits-voice.
* After each finished task, remind me to commit, and suggest a short commit message such as "Phase 1: CMake scaffold with JUCE".
* Never rewrite history, force push, or delete branches. Tell me the command and let me run it.
* Keep the Plaits and stmlib MIT license files in the repo and list them in THIRD\_PARTY\_NOTICES.
* Keep STATUS.md up to date: what is done, what is in progress, what is next. At the start of a fresh chat, read this brief and STATUS.md first.
* Do not commit build output, IDE files, or large binaries. Check that .gitignore covers them.
* Once the Phase 1 scaffold works, help me add a GitHub Actions workflow that builds on macOS and Windows on every push.

## Phases (we work through these in order)

**Phase 1: Project scaffold.** CMake project with JUCE via FetchContent. Targets: VST3 (Mac and Windows), AU (Mac). Empty plugin that loads in a DAW. Done when it loads and validates on both platforms.

**Phase 2: Plaits integration, single voice.** Copy in Plaits and stmlib. Write a thin wrapper class around the Plaits voice with a clean render function. Add sample rate conversion to and from 48 kHz. Map engine, harmonics, timbre, and morph to plugin parameters. Play one note from MIDI.

**Phase 3: Polyphony and envelopes.** Wrap each Plaits voice in a JUCE synthesiser voice. Eight-voice pool using JUCE's synthesiser class for allocation and stealing. ADSR envelope per voice driving level. Pitch bend, mod wheel, sustain pedal. Selectable voice count. Profile CPU on heavy engines.

**Phase 4: Full parameters and placeholder UI.** Expose all engines and parameters. Optional unison and detune. Plain functional UI with stock JUCE components bound to the parameter tree. Basic preset save and load.

**Phase 5: Visualizers.** Oscilloscope: audio thread pushes the summed output into a lock-free ring buffer, the UI triggers on a rising zero crossing so the waveform holds steady, with a zoom control. Envelope display: draws the ADSR curve from the parameters even when idle, plus a moving dot per active voice. Later: spectrum analyser, per-voice meters.

**Phase 6: Custom graphics.** Film strip or SVG knobs, custom look-and-feel classes, faux display skin around the visualizers, resizable and high-DPI friendly, assets embedded as binary data.

**Phase 7: Release.** Presets, auval validation for AU, testing in multiple DAWs, universal binaries on Mac, code signing and notarization, installers, MIT license notice for Plaits.

## Current Status

We are starting Phase 1. Wait for my first instruction.
