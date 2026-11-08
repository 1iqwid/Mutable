# STATUS

## Phase 1: CMake + JUCE scaffold (nearly done)

Done:
- CMake project with JUCE 8 via FetchContent; VST3 target, AU only on macOS.
- Builds on Windows with Visual Studio 2026 (MSVC), Release.
- Removed the input bus, so the synth is output-only (stereo).
- processBlock clears the buffer and uses ScopedNoDenormals.
- Passes pluginval at strictness level 5 (SUCCESS).
- Fixed Ableton Live load failure ("plugin has instrument category, but no valid event
  input bus"): added NEEDS_MIDI_INPUT TRUE to juce_add_plugin — without it, JUCE's
  VST3 module reports zero event input buses even though IS_SYNTH TRUE is set.
  Confirmed acceptsMidi() already returns true. Rebuilt Mutable_VST3 (Release) OK.

Known issues:
- Not yet re-tested in Ableton Live with the rebuilt plugin (needs a Live plugin rescan).
- Windows Smart App Control blocks unsigned plugins; it had to be turned off for local testing. Code signing is a Phase 7 item.

Next:
1. Rescan plugins in Live, confirm Mutable loads and accepts MIDI, then commit and tag v0.1.
2. Phase 2: Plaits integration (not started).