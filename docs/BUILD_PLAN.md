# Plaits Polyphonic Synth Plugin: Build Plan

A polyphonic software synthesizer built on the open source Mutable Instruments Plaits DSP code (MIT license), delivered as VST3 on Mac and Windows and as an Audio Unit on Mac. Built in VS Code with a local Qwen model served by LM Studio, with Claude as the planner.

Current progress lives in `STATUS.md`. This file is the plan.

---

## Goals

- Wrap the Plaits macro oscillator engines in a polyphonic plugin.
- Ship VST3 (Mac and Windows) and AU (Mac) from one codebase.
- Keep audio code and UI code fully separate so custom artwork can be dropped in later.
- Go beyond the hardware with rich visuals: an oscilloscope and live envelope displays first, more later.

---

## Tech Stack

- **Language:** C++17
- **Framework:** JUCE 8, pulled in with CMake FetchContent
- **Build system:** CMake, driven from VS Code with the CMake Tools extension
- **Windows compiler:** Visual Studio 2026 (MSVC) build tools
- **DSP source:** Mutable Instruments Plaits plus its stmlib dependency, vendored unmodified into `third_party/`
- **CI:** GitHub Actions building on macOS and Windows runners
- **AI workflow:** Qwen3.8-27B served by LM Studio (OpenAI-compatible API on localhost port 1234, context length 65536), used through Cline in VS Code
- **Validation:** pluginval at strictness level 5, plus a real DAW (Ableton Live)

---

## Control Map (from the Plaits panel)

The hardware panel is a functional reference only. Do not copy its artwork or Mutable Instruments branding.

- **Engine selector:** the two buttons and 16 LEDs become one engine selector, shown as an icon grid. Check the Plaits source for the real engine count before locking it.
- **Frequency:** MIDI replaces V/Oct, so this becomes a coarse tune control, with a separate pitch bend range setting.
- **Harmonics, Timbre, Morph:** the three main parameters, each a knob.
- **Timbre, FM, and Morph attenuverters:** bipolar modulation amounts. Add a small mod section with sources such as envelope, LFO, velocity, and mod wheel routed to these targets.
- **Model and Harmo inputs:** engine and harmonics modulation. Decide whether Harmonics gets its own mod-amount knob, since the panel has none.
- **Trig and Level:** Level is normalled to Trig on the hardware. Verify in the source how Plaits handles its own decay and low-pass-gate settings, then decide whether to offer them alongside our ADSR.
- **Out and Aux:** Plaits has two outputs. Decision made: a Mix knob crossfades between Out and Aux, and an Aux Level knob sets Aux loudness before the blend. No stereo mode for now, and the scope can show both signals.
- **Reference image:** the panel photo lives at `docs/reference/plaits-panel.jpg`.

---

## Architecture Rules

1. Audio and UI never share a thread. The audio thread never allocates, locks, or draws.
2. All parameters live in an AudioProcessorValueTreeState. The editor only binds to parameters, so the UI can be swapped without touching DSP.
3. Visualizer data moves through lock-free buffers. The audio thread writes, and the UI reads on a timer at about 60 frames per second.
4. Plaits runs at its native 48 kHz. Resample cleanly when the host rate differs.
5. One Plaits instance per voice. Each voice owns its own envelope.

---

## Phase 1: Project Scaffold (complete, tagged v0.1)

- [x] Create the CMake project with JUCE via FetchContent.
- [x] Target VST3, with AU only on macOS.
- [x] Get an empty plugin loading in a DAW (done on Windows in Ableton Live; Mac still to verify).
- [x] Set up the VS Code workspace: CMake Tools and build tasks.
- [x] Add the project brief file for Qwen in `.clinerules/`.
- [ ] Set up a GitHub Actions workflow that builds on both platforms (carried over, also needed for AU).

**Done when:** an empty plugin loads and passes validation on Mac and Windows. Windows is done; Mac is pending the CI workflow.

### Lessons from Phase 1

- A synth needs an **output-only stereo bus**, with no input bus, or Ableton rejects it.
- The CMake `juce_add_plugin` call needs **`NEEDS_MIDI_INPUT TRUE`** (alongside `IS_SYNTH TRUE`). Without it, Live reports "plugin has instrument category, but no valid event input bus."
- `processBlock` must clear the output buffer and use `juce::ScopedNoDenormals`, or pluginval fails with NaNs and subnormals.
- Pluginval passing does not guarantee a DAW accepts the plugin, so test in Live too.
- **Windows Smart App Control** blocks unsigned plugin DLLs. It had to be off for local testing. Code signing is a Phase 7 item.
- Install by copying the whole `Mutable.vst3` folder into `C:\Program Files\Common Files\VST3` (needs admin). Close Live first, and rescan with Ctrl+Alt+Rescan.

---

## Phase 2: Plaits Integration, Single Voice (in progress, branch `phase-2-plaits-voice`)

- [x] Vendor the Plaits and stmlib sources into `third_party/`, unmodified.
- [ ] Add them to CMake as a static library that compiles on Windows and macOS.
- [ ] Write a thin wrapper class around the Plaits voice with a clean render function.
- [ ] Add sample rate conversion between the host rate and 48 kHz.
- [ ] Map engine, harmonics, timbre, and morph to plugin parameters.
- [ ] Trigger a single note from MIDI and play a sound.

**Done when:** one note plays through one engine with three or four working knobs.

---

## Phase 3: Polyphony and Envelopes

- Wrap each Plaits voice in a JUCE synthesiser voice.
- Start with an eight-voice pool using JUCE's synthesiser class for allocation and voice stealing.
- Add an ADSR envelope per voice driving level, giving a low-pass-gate style response.
- Add pitch bend, mod wheel, and sustain pedal handling.
- Add a user-selectable voice count.
- Profile CPU early, especially the heavier engines like physical modelling and speech.

**Done when:** eight voices play stably with envelopes and no clicks on steal or release.

---

## Phase 4: Full Parameter Set and Placeholder UI

- Expose all engines and the remaining parameters (frequency, level, and the modulation amounts).
- Add optional unison and detune.
- Build a plain functional UI with stock JUCE components bound to the parameter tree.
- Add a basic preset save and load system.

**Done when:** the whole synth is playable and tweakable with an ugly but working UI.

---

## Phase 5: Visualizer Layer

**Priority one and two:**

- **Oscilloscope.** The audio thread pushes the summed output into a lock-free ring buffer. The UI finds a rising zero crossing to trigger on so the waveform holds steady. Include a zoom control for the time window, and optionally a mode that follows the most recent voice.
- **Envelope display.** Draw the ADSR curve from the envelope parameters so it is visible even with no notes held. Show a moving dot for each active voice, driven by the current stage and position exposed through a thread-safe path.

**Later candidates:**

- Spectrum analyser using JUCE's FFT class
- Per-voice activity meters
- Stereo or phase view
- Engine name and parameter readouts on the faux display

**Performance notes:** repaint only the regions that change, keep paint code lean, and consider the OpenGL context if the display gets heavy.

**Done when:** the scope and envelope displays run smoothly at 60 frames per second with all voices active.

---

## Phase 6: Custom Graphics Layer

- Design the artwork in Figma or similar.
- Choose the asset approach: film strip sprite sheets for knobs, SVG, or custom paint code.
- Export at 2x or as vectors so it stays sharp on high-DPI displays on both platforms.
- Embed assets as binary data in the plugin.
- Build custom look-and-feel classes and the faux display skin around the visualizers.
- Support resizable scaling.

**Done when:** the placeholder UI is fully replaced by the final artwork with no changes to audio code.

---

## Phase 7: Polish, Packaging, and Release

- Finalize the preset library and factory presets.
- Validate AU with Apple's auval, and test in Logic and GarageBand.
- Test VST3 in several DAWs on both platforms.
- Build Mac binaries as universal (Intel and Apple Silicon).
- Code sign and notarize the Mac builds for both formats, otherwise Gatekeeper blocks them.
- Sign the Windows build with a trusted certificate, so Smart App Control and SmartScreen do not block it.
- Create installers for each platform.
- Include the Plaits MIT notice, and do not use Mutable Instruments branding.

---

## Git and GitHub Workflow

Git is the safety net for working with a local model: small commits mean any bad change can be rolled back in seconds.

- **Day zero:** create the repo, add a `.gitignore` (build folder, IDE files, OS files), and push to a private GitHub repository.
- **Commits:** commit after every finished bullet, with a short message such as "Phase 1: CMake scaffold with JUCE."
- **Branching:** `main` always builds and works. Each phase gets its own branch, for example `phase-2-plaits-voice`. Merge into `main` only when it builds and passes the phase's "Done when" test.
- **Tags:** tag the end of each phase (v0.1, v0.2, and so on) as an easy restore point.
- **Roll back instead of repair:** if Qwen breaks something, discard the change and re-prompt with a smaller task.
- **GitHub Actions:** add a workflow that builds on macOS and Windows runners on every push, so platform problems show up early.
- **Licenses:** the Plaits and stmlib MIT notice is in the headers of the source files, and the Mutable repo README states that STM32F-project code is MIT. Leave those headers untouched, and add a `THIRD_PARTY_LICENSES.md` at the repo root.
- **Credentials:** never put tokens or passwords in any file in the repo or anything Qwen can read. Sign in through Git Credential Manager.
- **Status file:** keep `STATUS.md` in the repo listing what is finished, known issues, and what is next, so a fresh Qwen chat can pick up where the last one stopped.

---

## Working With Qwen

- Start a new Cline task for each bullet, and read the brief and `STATUS.md` at the start of each one.
- Give it one small, well-scoped task at a time, matching a single bullet above.
- End every task prompt with "update STATUS.md when done," and check the file afterward.
- Keep the JUCE docs and the Plaits headers handy as reference, since smaller models may invent function names. Tell Qwen to ask instead of guessing.
- Save files in VS Code (turn on Auto Save) before building or committing.
- Build and test after every step, and commit often.
- Keep the project brief updated as the plan evolves, and keep it in sync with this plan.

---

## Open Questions

- Final voice count default and maximum
- Whether to include a filter after the Plaits output
- Which additional visualizers to add after the scope and envelopes
- Installer tooling for each platform
- Plaits engine count: 16, or more in the current source