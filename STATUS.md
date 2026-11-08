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

Next:
1. Phase 2: Plaits integration. Copy plaits + stmlib into third_party/ with license
   files, wrap the Plaits voice, handle 48 kHz, play one note from MIDI.